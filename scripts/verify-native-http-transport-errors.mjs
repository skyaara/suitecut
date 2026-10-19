// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { inspect } from 'node:util'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeAPIRequestContext,
  NativeHTTPTransportError,
  NativeTimeoutError,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-http-transport-errors',
)
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const diagnostics = await captureNativeStderr(directory)
/** @type {string[]} */
const wire = [],
  checks = []
/** @type {import('../src/untrusted.js').UntrustedInput[]} */
const errors = []
const server = createServer((request, response) => {
  const path = request.url ?? ''
  wire.push(path)
  if (path === '/reset') request.socket.resetAndDestroy()
  else if (path === '/eof') request.socket.end()
  else if (path === '/status') {
    response.writeHead(404)
    response.end('ECONNRESET')
  } else if (path !== '/wait') response.end('actual warm response')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/** @type {import('../src/native-browser.js').NativeBrowser | undefined} */
let source
/** @type {{code:number|null,signal:NodeJS.Signals|null}|undefined} */
let exit
/** @type {import('../src/native-http.js').NativeAPIRequestContext | undefined} */
let api
/** @type {import('../src/untrusted.js').UntrustedInput[]} */
const observations = []
/** @type {import('../src/untrusted.js').UntrustedInput} */
let final
/**
 * Retain the actual native rejection, rather than testing text from a substitute transport.
 * @param {Promise<import('../src/native-http.js').NativeAPIResponse>} operation
 * @returns {Promise<Error>}
 */
async function rejected(operation) {
  try {
    const response = await operation
    await response.dispose()
  } catch (error) {
    assert(error instanceof Error)
    return error
  }
  throw new Error('Actual native request unexpectedly succeeded')
}
try {
  source = await launchNativeBrowser({
    executablePath,
    headless: true,
    width: 240,
    height: 180,
    onDiagnostic: diagnostics.observe,
  })
  api = await NativeAPIRequestContext.create(source)
  const warm = await api.get(origin + '/warm')
  assert.equal(warm.status(), 200)
  await warm.dispose()
  const reset = await rejected(api.get(origin + '/reset'))
  assert(reset instanceof NativeHTTPTransportError)
  assert.equal(reset.curlCode, 56)
  assert.equal(reset.osErrno, 104)
  assert.equal(reset.code, 'ECONNRESET')
  assert.match(reset.message, /ECONNRESET/u)
  observations.push({
    kind: 'actual TCP reset',
    message: reset.message,
    curlCode: reset.curlCode,
    osErrno: reset.osErrno,
    code: reset.code,
  })
  checks.push(
    'actual native curl reset preserves its observed Linux errno and attributed code through the framed failure payload',
  )

  const eof = await rejected(api.get(origin + '/eof'))
  assert(eof instanceof NativeHTTPTransportError)
  assert.equal(eof.curlCode, 52)
  assert.equal(eof.osErrno, 0)
  assert.equal(eof.code, undefined)
  observations.push({
    kind: 'actual orderly EOF',
    message: eof.message,
    curlCode: eof.curlCode,
    osErrno: eof.osErrno,
  })
  checks.push(
    'actual EOF preserves curl failure and absent OS errno without inventing a socket error code',
  )

  const status = await rejected(api.get(origin + '/status', { failOnStatusCode: true }))
  assert(!(status instanceof NativeHTTPTransportError))
  assert.match(status.message, /404/u)
  assert.equal('code' in status, false)
  checks.push('origin response text cannot manufacture native transport metadata or error identity')

  const timeout = await rejected(api.get(origin + '/wait', { timeout: 100 }))
  assert(timeout instanceof NativeTimeoutError)
  assert(!(timeout instanceof NativeHTTPTransportError))
  assert(wire.includes('/wait'))
  checks.push(
    'original native HTTP deadline remains a TimeoutError for a genuinely dispatched unanswered request',
  )

  await api.dispose()
  const state = z
    .object({
      httpContexts: z.number(),
      httpTransfers: z.number(),
      httpActive: z.number(),
      httpResponses: z.number(),
      httpRetainedBodyBytes: z.number(),
      httpActiveBodyBytes: z.number(),
      httpActiveUploadBytes: z.number(),
      httpCookieVisits: z.number(),
      httpCookieCallbacks: z.number(),
      httpScopeCommands: z.number(),
      httpProfilePhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  final = state
} catch (error) {
  errors.push(error)
} finally {
  if (api) {
    try {
      await api.dispose()
    } catch (error) {
      errors.push(error)
    }
  }
  server.closeAllConnections()
  try {
    await new Promise((resolve, reject) =>
      server.close((error) => (error ? reject(error) : resolve(undefined))),
    )
  } catch (error) {
    errors.push(error)
  }
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: errors.length === 0, checks, observations, wire, final }, null, 2) +
      '\n',
  )
  if (source) {
    try {
      await finishNativeProof(source, diagnostics, directory, errors)
    } catch (error) {
      errors.push(error)
    }
    exit = source.processExit?.()
    try {
      assert.deepEqual(exit, { code: 0, signal: null })
    } catch (error) {
      errors.push(error)
    }
  }
}
if (errors.length) {
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        checks,
        observations,
        wire,
        final,
        exit,
        errors: errors.map((error) =>
          error instanceof Error
            ? { message: error.message, stack: error.stack }
            : inspect(error, {
                customInspect: false,
                getters: false,
                depth: 3,
                maxStringLength: 4096,
              }),
        ),
      },
      null,
      2,
    ) + '\n',
  )
  throw new AggregateError(errors, 'Actual native HTTP transport error proof failed')
}
console.log(JSON.stringify({ checks: checks.length, directory, exit }))
