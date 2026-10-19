// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE,
  sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
assert.ok(executable && sdk)
/** @type {typeof import('/workspace/suitecut/src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Explicit hashed immutable SDK is selected at runtime.
const { launchNativeBrowser, NativeBrowserContexts } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
/** @type {{phases:string[],wire:{path:string,authorization:string}[],stderr:string,error?:string,stack?:string|undefined,shutdown?:string,exit?:{code:number|null,signal:NodeJS.Signals|null}}} */
const report = { phases: [], wire: [], stderr: '' }
const server = createServer((request, response) => {
  report.wire.push({ path: request.url ?? '', authorization: request.headers.authorization ?? '' })
  response.writeHead(401, {
    'WWW-Authenticate': 'Basic realm="actual default context denial"',
    Connection: 'close',
  })
  response.end('actual denied default authentication')
})
await new Promise((resolve) => server.listen(0, resolve))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = 'http://localhost:' + address.port
/** @type {import('/workspace/suitecut/src/native-browser.js').NativeBrowser|undefined} */
let source
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 240,
    height: 180,
    onDiagnostic: (value) => {
      report.stderr += value
    },
  })
  const browser = new NativeBrowserContexts(source),
    context = browser.defaultContext(),
    initial = context.pages()[0]
  assert.ok(initial)
  const initialResponse = await initial.goto(origin + '/initial-default')
  assert.equal(initialResponse?.status(), 401)
  assert.equal(await initial.locator('body').innerText(), 'actual denied default authentication')
  report.phases.push(
    'actual initial default Source cancels its unconfigured server authentication and commits genuine401 document',
  )
  const future = await context.newPage(),
    futureResponse = await future.goto(origin + '/future-default')
  assert.equal(futureResponse?.status(), 401)
  assert.equal(await future.locator('body').innerText(), 'actual denied default authentication')
  report.phases.push(
    'actual future page in default context inherits the native empty cancellation policy',
  )
  assert.ok(report.wire.length >= 2)
  assert.ok(report.wire.every((value) => value.authorization === ''))
  await future.close()
  const d = z
    .object({
      networkAuthChallenges: z.number(),
      networkAuthAttempts: z.number(),
      networkAuthCorrelations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.equal(d.networkAuthChallenges, 0)
  assert.equal(d.networkAuthAttempts, 0)
  assert.equal(d.networkAuthCorrelations, 0)
  await source.close()
  report.exit = source.processExit?.()
  assert.ok(report.exit)
  assert.equal(report.exit.code, 0)
  assert.equal(report.stderr, '')
} catch (error) {
  report.error = String(error)
  report.stack = error instanceof Error ? error.stack : undefined
  process.exitCode = 1
} finally {
  await source?.close().catch((error) => {
    report.shutdown = String(error)
    process.exitCode = 1
  })
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  const output = resolve(
    process.env.SUITECUT_NATIVE_DEFAULT_AUTH_REPORT ??
      '/tmp/suitecut-context-auth-cancel-stage/default-wire-report.json',
  )
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
