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
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects the immutable locally hashed SDK.
const { launchNativeBrowser, NativeAPIRequestContext } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
/** @type {{phases:string[],wire:{path:string,value:string,tab:string}[],stderr:string,error?:string,stack?:string|undefined,cleanup?:string,shutdown?:string,exit?:{code:number|null,signal:NodeJS.Signals|null}}} */
const report = { phases: [], wire: [], stderr: '' }
const server = createServer((request, response) => {
  const observed = {
    path: request.url ?? '',
    value: String(request.headers['x-latin'] ?? ''),
    tab: String(request.headers['x-tab'] ?? ''),
  }
  report.wire.push(observed)
  response.setHeader('content-type', 'application/json')
  response.end(JSON.stringify(observed))
})
await new Promise((resolve) => server.listen(0, resolve))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = 'http://localhost:' + address.port
/** @type {import('../src/native-browser.js').NativeBrowser|undefined} */
let source
/** @type {import('../src/native-http.js').NativeAPIRequestContext|undefined} */
let api
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
  api = await NativeAPIRequestContext.create(source, {
    extraHTTPHeaders: { 'X-Latin': 'caféÿ', 'X-Tab': 'alpha\tbeta' },
  })
  let response = await api.get(origin + '/defaults')
  assert.equal(response.status(), 200)
  assert.deepEqual(z.object({ value: z.string(), tab: z.string() }).parse(await response.json()), {
    value: 'caféÿ',
    tab: 'alpha\tbeta',
  })
  await response.dispose()
  report.phases.push(
    'actual native curl wire maps Latin1 default header units to exact HTTP octets and preserves allowed HTAB',
  )
  response = await api.get(origin + '/override', { headers: { 'x-latin': 'olé' } })
  assert.equal(z.object({ value: z.string() }).parse(await response.json()).value, 'olé')
  await response.dispose()
  report.phases.push('case-insensitive request override preserves exact Latin1 wire bytes')
  const before = report.wire.length
  await assert.rejects(
    api.get(origin + '/invalid', { headers: { foo: 'недопустимое значение' } }),
    /Invalid character in header content/u,
  )
  assert.equal(report.wire.length, before)
  await assert.rejects(
    NativeAPIRequestContext.create(source, { extraHTTPHeaders: { foo: '日本語' } }),
    /Invalid character in header content/u,
  )
  assert.equal(report.wire.length, before)
  report.phases.push(
    'native request/context ingress rejects non-byte header units before any actual origin request',
  )
  await api.dispose()
  const d = z
    .object({
      httpContexts: z.number(),
      httpActive: z.number(),
      httpTransfers: z.number(),
      httpProfilePhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  for (const [key, value] of Object.entries(d)) assert.equal(value, 0, key)
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
  await api?.dispose().catch((error) => {
    report.cleanup = String(error)
    process.exitCode = 1
  })
  await source?.close().catch((error) => {
    report.shutdown = String(error)
    process.exitCode = 1
  })
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  const output = resolve(
    process.env.SUITECUT_NATIVE_REQUEST_HEADERS_REPORT ??
      '/tmp/suitecut-http-request-header-stage/wire-report.json',
  )
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
