// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import console from 'node:console'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:https'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The caller selects an independently hashed compiled SDK matching these public source types.
const { launchNativeBrowser, NativeAPIRequestContext, NativeHTTPTransportError } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
const output = resolve(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '/tmp/suitecut-http-extra-ca')
await mkdir(output, { recursive: true })
const directory = await mkdtemp(join(tmpdir(), 'suitecut-http-extra-ca-certs-'))
const diagnostics = await captureNativeStderr(output)
/** @type {string[]} */
const phases = []
/** @type {string[]} */
const wire = []
/** @type {Error[]} */
const errors = []
/** @type {import('../src/native-browser.js').NativeBrowser | undefined} */
let source
/** @type {import('../src/native-http.js').NativeAPIRequestContext[]} */
const contexts = []
/** @type {{code:number|null,signal:NodeJS.Signals|null}|undefined} */
let exit
/** @type {Record<string,number>|undefined} */
let final
/**
 * Create actual certificates through the system OpenSSL implementation.
 * @param {string[]} args
 */
const openssl = (...args) => execFileSync('openssl', args, { cwd: directory, stdio: 'ignore' })
for (const name of ['root', 'wrong'])
  openssl(
    'req',
    '-x509',
    '-newkey',
    'rsa:2048',
    '-nodes',
    '-keyout',
    name + '.key',
    '-out',
    name + '.pem',
    '-days',
    '1',
    '-subj',
    '/CN=SuiteCut native ' + name,
    '-addext',
    'basicConstraints=critical,CA:TRUE',
  )
openssl(
  'req',
  '-newkey',
  'rsa:2048',
  '-nodes',
  '-keyout',
  'server.key',
  '-out',
  'server.csr',
  '-subj',
  '/CN=localhost',
)
await writeFile(
  join(directory, 'server.ext'),
  'subjectAltName=DNS:localhost\nbasicConstraints=critical,CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n',
)
openssl(
  'x509',
  '-req',
  '-in',
  'server.csr',
  '-CA',
  'root.pem',
  '-CAkey',
  'root.key',
  '-CAcreateserial',
  '-out',
  'server.pem',
  '-days',
  '1',
  '-extfile',
  'server.ext',
)
const server = createServer(
  {
    key: await readFile(join(directory, 'server.key')),
    cert: await readFile(join(directory, 'server.pem')),
  },
  (request, response) => {
    wire.push(request.url ?? '')
    response.end('genuine trusted TLS')
  },
)
await new Promise((resolveListen) => server.listen(0, '127.0.0.1', () => resolveListen(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const url = `https://localhost:${address.port}/`
/**
 * Reject a real native TLS request without inventing a transport result.
 * @param {Promise<import('../src/native-http.js').NativeAPIResponse>} pending
 */
async function rejected(pending) {
  try {
    const response = await pending
    await response.dispose()
  } catch (error) {
    assert(error instanceof NativeHTTPTransportError)
    assert.equal(error.curlCode, 60)
    return
  }
  throw new Error('Actual TLS request unexpectedly succeeded')
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 64,
    height: 64,
    onDiagnostic: diagnostics.observe,
  })
  const strict = await NativeAPIRequestContext.create(source)
  contexts.push(strict)
  const trusted = await NativeAPIRequestContext.create(source, {
    extraCACertificates: [await readFile(join(directory, 'root.pem'), 'utf8')],
  })
  contexts.push(trusted)
  const unrelated = await NativeAPIRequestContext.create(source, {
    extraCACertificates: [await readFile(join(directory, 'wrong.pem'), 'utf8')],
  })
  contexts.push(unrelated)
  await rejected(strict.get(url))
  phases.push('Default native verification rejects the private CA')
  for (let index = 0; index < 5; ++index) {
    const response = await trusted.get(url + index)
    assert.equal(response.status(), 200)
    assert.equal(await response.text(), 'genuine trusted TLS')
    await response.dispose()
    await rejected(strict.get(url))
  }
  phases.push(
    'Five genuine trusted transfers alternate with strict-context rejection on the same host',
  )
  await rejected(unrelated.get(url))
  await rejected(trusted.get(`https://127.0.0.1:${address.port}/wrong-host`))
  assert.equal(wire.length, 5)
  phases.push('Unrelated anchors and hostname mismatch remain rejected before HTTP dispatch')
  for (const context of contexts) await context.dispose()
  final = z
    .object({
      httpContexts: z.number(),
      httpTransfers: z.number(),
      httpActive: z.number(),
      httpResponses: z.number(),
      httpScopeCommands: z.number(),
      httpProfilePhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(final)) assert.equal(value, 0, key)
  phases.push('Actual API profile and transfer disposal settle all selected native ownership')
} catch (error) {
  errors.push(error instanceof Error ? error : new Error(String(error)))
} finally {
  for (const context of contexts) {
    try {
      await context.dispose()
    } catch (error) {
      errors.push(error instanceof Error ? error : new Error(String(error)))
    }
  }
  server.closeAllConnections()
  await new Promise((resolveClose) => server.close(() => resolveClose(undefined)))
  if (source) {
    try {
      await finishNativeProof(source, diagnostics, output, errors)
    } catch (error) {
      errors.push(error instanceof Error ? error : new Error(String(error)))
    }
    exit = source.processExit?.()
    try {
      assert.deepEqual(exit, { code: 0, signal: null })
    } catch (error) {
      errors.push(error instanceof Error ? error : new Error(String(error)))
    }
  }
  await rm(directory, { recursive: true, force: true })
  await writeFile(
    join(output, 'report.json'),
    JSON.stringify(
      {
        completed: !errors.length,
        phases,
        wire,
        final,
        exit,
        errors: errors.map((error) => ({ message: error.message, stack: error.stack })),
      },
      null,
      2,
    ) + '\n',
  )
}
if (errors.length) throw new AggregateError(errors, 'Native extra CA proof failed')
console.log(JSON.stringify({ phases: phases.length, output, exit }))
