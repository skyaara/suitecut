// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeRequest,
  NativeResponse,
} from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve('.suitecut/native-network-session-handoff')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z.array(z.object({ url: z.string(), host: z.string() })).parse([])
const events = z
  .array(
    z.object({
      event: z.string(),
      id: z.string(),
      native: z.string(),
      session: z.string(),
      url: z.string(),
    }),
  )
  .parse([])
const server = createServer((request, response) => {
  const url = request.url ?? ''
  wire.push({ url, host: request.headers.host ?? '' })
  response.setHeader('content-type', 'text/html')
  response.end(`<body>actual ${url}</body>`)
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://localhost:${address.port}`
const cross = `http://127.0.0.1:${address.port}`
let closeSource = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  closeSource = () => source.close()
  const context = await new NativeBrowserContexts(source).newContext()
  const page = await context.newPage()
  page.setDefaultTimeout(3000)
  page.setDefaultNavigationTimeout(3000)
  const record = (event, value) => {
    const name = z.enum(['request', 'response', 'requestfinished', 'requestfailed']).parse(event)
    const item = z.union([z.instanceof(NativeRequest), z.instanceof(NativeResponse)]).parse(value)
    const request = item instanceof NativeResponse ? item.request() : item
    events.push({
      event: name,
      id: request.info.id,
      native: request.info.requestId,
      session: request.info.session,
      url: request.url(),
    })
  }
  page.on('request', (request) => record('request', request))
  page.on('response', (response) => record('response', response))
  page.on('requestfinished', (request) => record('requestfinished', request))
  page.on('requestfailed', (request) => record('requestfailed', request))
  await page.goto(`${origin}/parent`)
  const firstResponse = page.waitForResponse(`${cross}/child`)
  const firstFinish = page.waitForEvent('requestfinished', {
    predicate: (value) => value instanceof NativeRequest && value.url() === `${cross}/child`,
  })
  const firstFrame = page.waitForEvent('framenavigated', {
    predicate: (frame) => frame.url() === `${cross}/child`,
  })
  void firstResponse.catch(() => undefined)
  void firstFinish.catch(() => undefined)
  void firstFrame.catch(() => undefined)
  await page.evaluate((url) => {
    const frame = document.createElement('iframe')
    frame.src = url
    document.body.append(frame)
  }, `${cross}/child`)
  const [response, finished, child] = await Promise.all([firstResponse, firstFinish, firstFrame])
  assert.equal(response.request(), finished)
  assert.equal(await response.text(), '<body>actual /child</body>')
  assert.equal(response.frame(), child)
  checks.push('Parent to OOPIF preserves canonical finished identity and actual response bytes')
  await child.waitForLoadState('networkidle', { timeout: 3000 })
  checks.push('Initial OOPIF completion retires parent active request ownership')
  const mergeResponse = page.waitForResponse(`${origin}/merged`)
  const mergeFinish = page.waitForEvent('requestfinished', {
    predicate: (value) => value instanceof NativeRequest && value.url() === `${origin}/merged`,
  })
  void mergeResponse.catch(() => undefined)
  void mergeFinish.catch(() => undefined)
  const navigation = await child.goto(`${origin}/merged`, { timeout: 3000 })
  const [merged, ended] = await Promise.all([mergeResponse, mergeFinish])
  assert.equal(merged.request(), ended)
  assert.equal(navigation?.request(), ended)
  assert.equal(navigation, merged)
  assert.equal(await merged.text(), '<body>actual /merged</body>')
  assert.equal(merged.frame(), child)
  checks.push(
    'OOPIF to parent merge preserves navigation/response/finished identity and actual new-session body',
  )
  await child.waitForLoadState('networkidle', { timeout: 3000 })
  const native = z
    .object({ activeRequests: z.literal(0), bodyWaits: z.literal(0) })
    .parse(await page.source.runAutomation('diagnostics', {}))
  assert.equal(native.activeRequests, 0)
  assert.equal(native.bodyWaits, 0)
  for (const url of [`${cross}/child`, `${origin}/merged`]) {
    const observed = events.filter((event) => event.url === url)
    assert.equal(observed.filter((event) => event.event === 'request').length, 1)
    assert.equal(observed.filter((event) => event.event === 'response').length, 1)
    assert.equal(observed.filter((event) => event.event === 'requestfinished').length, 1)
    assert.equal(observed.filter((event) => event.event === 'requestfailed').length, 0)
    assert.equal(new Set(observed.map((event) => event.id)).size, 1)
  }
  checks.push(
    'Real terminal events settle native active/body waits without duplicate requests or fabricated failure',
  )
  await context.close()
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await closeSource().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  const report = { binarySHA256, checks, failures, wire, events }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
  if (failures.length) process.exitCode = 1
}
