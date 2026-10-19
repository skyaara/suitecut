// SPDX-License-Identifier: MIT
/* global fetch, getComputedStyle, document */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve('.suitecut/native-context-har')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z
  .array(z.object({ method: z.string(), url: z.string(), base64: z.string() }))
  .parse([])
const bytes = Buffer.from(Array.from({ length: 256 }, (_, index) => index))
let childOrigin = ''
const server = createServer((request, response) => {
  void (async () => {
    const parts = []
    for await (const chunk of request) {
      assert.ok(Buffer.isBuffer(chunk))
      parts.push(chunk)
    }
    const body = Buffer.concat(parts)
    const url = request.url ?? ''
    wire.push({ method: request.method ?? '', url, base64: body.toString('base64') })
    if (url === '/echo') {
      response.setHeader('content-type', 'application/octet-stream')
      response.end(body)
    } else if (url === '/theme.css') {
      response.setHeader('content-type', 'text/css')
      response.end('body { color: rgb(1, 2, 3); }')
    } else {
      response.setHeader('content-type', 'text/html')
      response.end(
        '<link rel="stylesheet" href="/theme.css"><body>native context HAR ' +
          url +
          (url === '/document' ? `<iframe src="${childOrigin}/child"></iframe>` : '') +
          '</body>',
      )
    }
  })().catch((error) => {
    failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
    response.destroy()
  })
})
await new Promise((done) => server.listen(0, '0.0.0.0', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
childOrigin = `http://127.0.0.2:${address.port}`
let closeSource = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (message) => diagnostics.push(message),
  })
  closeSource = () => source.close()
  if (
    typeof source.runContext !== 'function' ||
    typeof source.pageSource !== 'function' ||
    typeof source.onContextEvent !== 'function'
  )
    throw new Error('Native context transport unavailable')
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const archivePath = resolve(directory, 'context-updated.har')
  const recording = await browser.newContext()
  recording.setDefaultTimeout(5000)
  await recording.routeFromHAR(archivePath, {
    update: true,
    updateContent: 'embed',
    updateMode: 'full',
  })
  const recordedPage = await recording.newPage()
  await recordedPage.goto(origin + '/document')
  const child = recordedPage.frames().find((frame) => frame.url() === childOrigin + '/child')
  assert.ok(child)
  assert.equal(await child.evaluate(() => getComputedStyle(document.body).color), 'rgb(1, 2, 3)')
  assert.deepEqual(
    await recordedPage.evaluate(async () =>
      Array.from(
        new Uint8Array(
          await (
            await fetch('/echo', {
              method: 'POST',
              body: new Uint8Array(Array.from({ length: 256 }, (_, index) => index)),
            })
          ).arrayBuffer(),
        ),
      ),
    ),
    Array.from(bytes),
  )
  await recording.close()
  const archive = z
    .object({
      log: z.object({ entries: z.array(z.object({ request: z.object({ url: z.string() }) })) }),
    })
    .parse(JSON.parse(await readFile(archivePath, 'utf8')))
  assert.ok(archive.log.entries.some((entry) => entry.request.url === childOrigin + '/child'))
  checks.push(
    'Context update captures real future Page/OOPIF and exact binary POST before committed close',
  )

  await new Promise((done) => server.close(() => done(undefined)))
  const wireCount = wire.length
  const replay = await browser.newContext()
  replay.setDefaultTimeout(5000)
  await replay.routeFromHAR(archivePath)
  const page = await replay.newPage()
  await page.goto(origin + '/document')
  const replayChild = page.frames().find((frame) => frame.url() === childOrigin + '/child')
  assert.ok(replayChild)
  assert.equal(
    await replayChild.evaluate(() => getComputedStyle(document.body).color),
    'rgb(1, 2, 3)',
  )
  assert.deepEqual(
    await page.evaluate(async () =>
      Array.from(
        new Uint8Array(
          await (
            await fetch('/echo', {
              method: 'POST',
              body: new Uint8Array(Array.from({ length: 256 }, (_, index) => index)),
            })
          ).arrayBuffer(),
        ),
      ),
    ),
    Array.from(bytes),
  )
  assert.equal(wire.length, wireCount)
  checks.push(
    'Context replay installed before creation serves actual future Page/OOPIF with server offline',
  )

  const priorityContext = await browser.newContext()
  priorityContext.setDefaultTimeout(5000)
  const priority = await priorityContext.newPage()
  let pageRoutes = 0
  await priority.route('**/theme.css', async (route) => {
    ++pageRoutes
    await route.fulfill({ contentType: 'text/css', body: 'body { color: rgb(7, 8, 9); }' })
  })
  await priorityContext.routeFromHAR(archivePath)
  await priority.goto(origin + '/document')
  assert.equal(await priority.evaluate(() => getComputedStyle(document.body).color), 'rgb(7, 8, 9)')
  assert.ok(pageRoutes > 0)
  await priority.unrouteAll()
  await priority.reload()
  assert.equal(await priority.evaluate(() => getComputedStyle(document.body).color), 'rgb(1, 2, 3)')
  checks.push(
    'Earlier Page handler takes precedence over later Context HAR; Page.unrouteAll retains Context HAR',
  )

  const isolatedContext = await browser.newContext()
  isolatedContext.setDefaultTimeout(1500)
  const isolated = await isolatedContext.newPage()
  await assert.rejects(
    isolated.goto(origin + '/document'),
    /ERR_CONNECTION_REFUSED|ERR_FAILED|Failed|failed/,
  )
  await isolatedContext.close()
  checks.push('A distinct actual CEF request context never receives another context archive')

  const defaultPage = browser.defaultContext().pages()[0]
  assert.ok(defaultPage)
  await defaultPage.close()
  const future = await replay.newPage()
  await future.goto(origin + '/document')
  assert.equal(await future.evaluate(() => getComputedStyle(document.body).color), 'rgb(1, 2, 3)')
  assert.equal(wire.length, wireCount)
  checks.push('Actual future-page Context replay survives closing default Page 1')

  const observerContext = await browser.newContext()
  const observer = await observerContext.newPage()
  await priorityContext.close()
  await replay.close()
  const native = z
    .object({
      nativeContextHARPhysical: z.literal(0),
      nativeContextHARPending: z.literal(0),
      nativeHARReplayBytes: z.literal(0),
    })
    .parse(await observer.source.runAutomation('diagnostics', {}))
  assert.equal(native.nativeContextHARPhysical, 0)
  await observerContext.close()
  checks.push('Actual context close releases physical replay callbacks and global archive leases')
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await closeSource().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  if (
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex') !== binarySHA256
  )
    failures.push('Native executable changed during proof')
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, checks, failures, diagnostics, wire }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
