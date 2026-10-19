// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE,
  sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
assert.ok(executable && sdk, 'Provide immutable compiled credential-update executable and SDK')
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects a locally hashed immutable SDK matching these public source types.
const { launchNativeBrowser, NativeBrowserContexts } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
const output = resolve(
  process.env.SUITECUT_NATIVE_AUTH_UPDATE_REPORT ??
    '/tmp/suitecut-context-auth-update-stage/wire-report.json',
)
/** @type {{ phases: string[], wire: {path: string, host: string, authorization: string}[], stderr: string, final?: import('../src/untrusted.js').UntrustedInput, exit?: {code: number|null,signal: NodeJS.Signals|null}, error?: string, stack?: string|undefined, cleanup?: string, shutdown?: string }} */
const report = { phases: [], wire: [], stderr: '' }
/** @type {import('../src/native-context.js').NativeBrowserContext[]} */
const owned = []
/** @type {import('../src/native-browser.js').NativeBrowser|undefined} */
let source
let expected = 'Basic ' + Buffer.from('first:secret-one').toString('base64')
/**
 * Encodes the actual loopback server's challenge credentials.
 * @param {string} name
 * @param {string} password
 */
const basic = (name, password) => 'Basic ' + Buffer.from(name + ':' + password).toString('base64')
const server = createServer((request, response) => {
  const authorization = request.headers.authorization ?? ''
  report.wire.push({ path: request.url ?? '', host: request.headers.host ?? '', authorization })
  if (authorization !== expected) {
    response.writeHead(401, {
      'WWW-Authenticate': 'Basic realm="SuiteCut actual mutable context"',
      Connection: 'close',
    })
    response.end('unauthorized')
    return
  }
  response.setHeader('content-type', 'text/plain')
  response.end('authorized:' + authorization)
})
await new Promise((resolve) => server.listen(0, resolve))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = 'http://localhost:' + address.port,
  cross = 'http://127.0.0.1:' + address.port
const targetSchema = z.object({ contextAuthenticationTargets: z.number() })
const zeroFields = [
  'contextAuthenticationUpdates',
  'contextAuthenticationPhysicalFences',
  'httpContexts',
  'httpProfilePhysical',
  'httpActive',
  'httpTransfers',
  'httpCookieVisits',
  'httpCookieCallbacks',
  'httpScopeCommands',
]
const diagnosticSchema = targetSchema.extend(
  Object.fromEntries(zeroFields.map((field) => [field, z.number()])),
)
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 240,
    height: 180,
    onDiagnostic: (value) => (report.stderr += value),
  })
  const browser = new NativeBrowserContexts(source)
  const baseline = targetSchema.parse(await source.runAutomation('diagnostics', {}))
  const context = await browser.newContext()
  owned.push(context)
  const api = context.request
  let response = await api.get(origin + '/api-before')
  assert.equal(response.status(), 401)
  await response.dispose()
  await context.setHTTPCredentials({ username: 'first', password: 'secret-one' })
  assert.equal(context.request, api)
  response = await api.get(origin + '/api-after')
  assert.equal(response.status(), 200)
  await response.dispose()
  const page = await context.newPage()
  await page.goto(origin + '/browser-first')
  assert.equal(await page.locator('body').innerText(), 'authorized:' + expected)
  report.phases.push(
    'actual context update configures same retained API and future browser before its first authenticated navigation',
  )
  expected = basic('second', 'secret-two')
  const changedAt = report.wire.length
  await context.setHTTPCredentials({ username: 'second', password: 'secret-two' })
  const status = z
    .number()
    .parse(
      await page.evaluate(
        `fetch(${JSON.stringify(origin + '/browser-second')}).then(response=>response.status)`,
      ),
    )
  assert.equal(status, 200)
  response = await api.get(origin + '/api-second')
  assert.equal(response.status(), 200)
  await response.dispose()
  assert.ok(
    report.wire
      .slice(changedAt)
      .every((value) => !value.authorization || value.authorization === expected),
  )
  report.phases.push(
    'genuine CEF auth cache clear and acknowledged existing actor update prevent old credentials on actual browser and API wire',
  )
  await context.setHTTPCredentials({ username: 'wrong', password: 'wrong' })
  response = await api.get(origin + '/wrong')
  assert.equal(response.status(), 401)
  await response.dispose()
  await context.setHTTPCredentials(null)
  const clearedAt = report.wire.length
  response = await api.get(origin + '/cleared')
  assert.equal(response.status(), 401)
  await response.dispose()
  const clearedStatus = z
    .number()
    .parse(
      await page.evaluate(
        `fetch(${JSON.stringify(origin + '/browser-cleared')}).then(response=>response.status)`,
      ),
    )
  assert.equal(clearedStatus, 401)
  assert.ok(report.wire.slice(clearedAt).every((value) => value.authorization === ''))
  await context.setHTTPCredentials([
    { username: 'second', password: 'secret-two', origin, send: 'always' },
    { username: 'wrong', password: 'wrong', origin: cross },
  ])
  response = await api.get(origin + '/specific')
  assert.equal(response.status(), 200)
  await response.dispose()
  response = await api.get(cross + '/other-origin')
  assert.equal(response.status(), 401)
  await response.dispose()
  report.phases.push(
    'wrong credentials and null clearing affect genuine wire outcomes; subsequent origin-scoped array selects actual origin',
  )
  await context.close()
  for (let cycle = 0; cycle < 20; ++cycle) {
    const profile = await browser.newContext()
    owned.push(profile)
    const request = profile.request
    await profile.setHTTPCredentials({ username: 'second', password: 'secret-two', send: 'always' })
    response = await request.get(origin + '/cycle-' + cycle)
    assert.equal(response.status(), 200)
    await response.dispose()
    await profile.setHTTPCredentials(null)
    await profile.close()
    const d = diagnosticSchema.parse(await source.runAutomation('diagnostics', {}))
    for (const field of zeroFields) assert.equal(d[field], 0, field)
    assert.equal(d.contextAuthenticationTargets, baseline.contextAuthenticationTargets)
  }
  report.phases.push(
    'twenty actual native context update/clear/close cycles retain no auth fences or actor targets and no HTTP work',
  )
  report.final = await source.runAutomation('diagnostics', {})
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
  for (const context of owned)
    await context.close().catch((error) => {
      report.cleanup = String(error)
      process.exitCode = 1
    })
  await source?.close().catch((error) => {
    report.shutdown = String(error)
    process.exitCode = 1
  })
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
}
