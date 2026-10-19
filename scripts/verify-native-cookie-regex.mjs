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
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects a locally hashed immutable SDK matching these public source types.
const { launchNativeBrowser, NativeAPIRequestContext, NativeBrowserContexts } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
/** @type {{phases:string[],wire:string[],stderr:string,error?:string,stack?:string|undefined,cleanup?:string,shutdown?:string,exit?:{code:number|null,signal:NodeJS.Signals|null}|undefined}} */
const report = { phases: [], wire: [], stderr: '' }
const server = createServer((request, response) => {
  report.wire.push(request.headers.cookie ?? '')
  response.end('actual native cookie deletion')
})
await new Promise((resolve) => server.listen(0, () => resolve(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const base = `http://localhost:${address.port}`
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 64,
  height: 64,
  onDiagnostic: (text) => {
    report.stderr += text
  },
}).catch(async (error) => {
  const failure = error instanceof Error ? error : new Error('Native browser startup failed')
  report.error = failure.message
  report.stack = failure.stack
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  const output = resolve(
    process.env.SUITECUT_NATIVE_COOKIE_REGEX_REPORT ??
      '/tmp/suitecut-cookie-regex-stage/wire-report.json',
  )
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  throw failure
})
const contexts = new NativeBrowserContexts(source)
/** @type {import('../src/native-context.js').NativeBrowserContext | undefined} */
let browserContext
/** @type {import('../src/native-http.js').NativeAPIRequestContext | undefined} */
let request
/** @type {Error | undefined} */
let failure
try {
  browserContext = await contexts.newContext()
  request = await NativeAPIRequestContext.create(source)
  const seeds = [
    { name: 'session', value: 'one', domain: 'localhost', path: '/api' },
    { name: 'session', value: 'two', domain: 'localhost', path: '/api/v1' },
    { name: 'session', value: 'three', domain: 'localhost', path: '/apix' },
    { name: 'other', value: 'four', domain: 'localhost', path: '/api' },
  ]
  await request.addCookies(seeds)
  await browserContext.addCookies(seeds)
  const name = /^sess/gy
  name.lastIndex = 7
  await request.clearCookies({ name, domain: /LOCALHOST/i, path: /^\/api(?:\/|$)/ })
  assert.equal(name.lastIndex, 7)
  assert.deepEqual((await request.cookies()).map(({ value }) => value).sort(), ['four', 'three'])
  assert.equal((await browserContext.cookies()).length, 4)
  const response = await request.get(base + '/api/v1')
  assert.equal(await response.text(), 'actual native cookie deletion')
  assert.equal(report.wire.length, 1)
  assert.equal(report.wire[0], 'other=four')
  await response.dispose()
  report.phases.push(
    'API compound/global/sticky/case regex deletes genuine cookies and changes actual wire without SDK matching',
  )

  await browserContext.clearCookies({ name: /^sess/g, domain: /localhost/, path: /^\/api/ })
  assert.deepEqual(
    (await browserContext.cookies()).map(({ value }) => value),
    ['four'],
  )
  await browserContext.addCookies([{ name: '', value: 'nameless', domain: 'localhost', path: '/' }])
  await browserContext.clearCookies({ name: '' })
  assert.deepEqual(
    (await browserContext.cookies()).map(({ value }) => value),
    ['four'],
  )
  assert.deepEqual((await request.cookies()).map(({ value }) => value).sort(), ['four', 'three'])
  report.phases.push(
    'Browser context regex and empty literal filters use exact native scope and preserve independent API jar',
  )

  const before = await browserContext.cookies()
  await assert.rejects(
    source.runContext('clearCookies', {
      contextId: browserContext.id,
      filter: { name: { sourceJSON: JSON.stringify('['), flags: '' } },
    }),
    /regular expression evaluation failed/,
  )
  assert.deepEqual(await browserContext.cookies(), before)
  const diagnostics = z
    .object({ nativeCookiePhysical: z.number(), pendingCookieOperations: z.number() })
    .parse(await source.runContext('list', {}))
  assert.equal(diagnostics.nativeCookiePhysical, 0)
  assert.equal(diagnostics.pendingCookieOperations, 0)
  report.phases.push(
    'Actual private V8 syntax rejection performs no partial delete and leaves native cookie ownership zero',
  )
} catch (error) {
  failure = error instanceof Error ? error : new Error('Native cookie regex verification failed')
  report.error = error instanceof Error ? error.message : 'Native cookie regex verification failed'
  if (error instanceof Error) report.stack = error.stack
} finally {
  try {
    await request?.dispose()
    await browserContext?.close()
  } catch (error) {
    report.cleanup = error instanceof Error ? error.message : 'Native cookie cleanup failed'
    failure ??= error instanceof Error ? error : new Error('Native cookie cleanup failed')
  }
  try {
    await source.close()
  } catch (error) {
    report.shutdown = error instanceof Error ? error.message : 'Native helper shutdown failed'
    failure ??= error instanceof Error ? error : new Error('Native cookie cleanup failed')
  }
  report.exit = source.processExit?.()
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  const output = resolve(
    process.env.SUITECUT_NATIVE_COOKIE_REGEX_REPORT ??
      '/tmp/suitecut-cookie-regex-stage/wire-report.json',
  )
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
}
if (failure) throw failure
assert.equal(report.stderr, '')
assert.ok(report.exit)
assert.equal(report.exit.code, 0)
assert.equal(report.exit.signal, null)
console.log(JSON.stringify(report, null, 2))
