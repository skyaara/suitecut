// SPDX-License-Identifier: MIT
/* global fetch, FormData, File, atob */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts, NativeRequest } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_POST_DIRECTORY ?? '.suitecut/native-network-post-bodies',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const archivePath = resolve(directory, 'actual-files.har')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const requests = z.array(z.instanceof(NativeRequest)).parse([])
const routedBodies = z.array(z.string()).parse([])
const wire = z
  .array(z.object({ url: z.string(), base64: z.string(), contentType: z.string() }))
  .parse([])
const server = createServer((request, response) => {
  void (async () => {
    const chunks = z.array(z.instanceof(Buffer)).parse([])
    for await (const chunk of request) {
      assert.ok(Buffer.isBuffer(chunk))
      chunks.push(chunk)
    }
    wire.push({
      url: request.url ?? '',
      base64: Buffer.concat(chunks).toString('base64'),
      contentType: request.headers['content-type'] ?? '',
    })
    response.setHeader('content-type', request.url === '/document' ? 'text/html' : 'text/plain')
    response.end(
      request.url === '/document' ? '<body>actual file POST</body>' : 'actual multipart wire',
    )
  })().catch((error) => {
    failures.push(String(error))
    response.destroy()
  })
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (value) => diagnostics.push(value),
})
try {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Native context transport is unavailable')
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const context = await browser.newContext({
    recordHAR: { path: archivePath, content: 'embed' },
  })
  const page = await context.newPage()
  page.on('request', (request) => {
    if (request.url() === origin + '/post' || request.url() === origin + '/routed')
      requests.push(request)
  })
  await page.goto(origin + '/document')
  for (const endpoint of ['/post', '/routed']) {
    if (endpoint === '/routed')
      await page.route('**/routed', (route) => {
        const body = route.request().postDataBuffer()
        assert.ok(body)
        routedBodies.push(body.toString('base64'))
        return route.continue()
      })
    await page.evaluate(async (path) => {
      const body = new FormData()
      body.set('name', 'actual value')
      body.set('text', new File(['actual file text'], 'actual.txt'))
      body.set('binary', new File([new Uint8Array([0, 255, 13, 10, 128, 1])], 'actual.bin'))
      const response = await fetch(path, { method: 'POST', body })
      return response.text()
    }, endpoint)
    const actual = requests.find((request) => request.url() === origin + endpoint)
    const received = wire.find((record) => record.url === endpoint)
    assert.ok(actual && received)
    const bytes = actual.postDataBuffer()
    assert.ok(bytes)
    assert.equal(bytes.toString('base64'), received.base64)
    assert.ok(bytes.includes(Buffer.from([0, 255, 13, 10, 128, 1])))
    assert.equal(actual.postData(), bytes.toString('utf8'))
    assert.equal((await actual.response())?.request(), actual)
    checks.push(
      `Actual File multipart bytes match native public request and real HTTP wire: ${endpoint}`,
    )
  }
  const routed = wire.find((record) => record.url === '/routed')
  assert.ok(routed)
  assert.deepEqual(routedBodies, [routed.base64])
  checks.push(
    'Public route receives original exact file bytes before continuing the genuine request',
  )
  await context.close()
  const archive = z
    .object({
      log: z.object({
        entries: z.array(
          z.object({
            request: z.object({
              url: z.string(),
              postData: z.object({ text: z.string(), _encoding: z.string().optional() }).optional(),
            }),
          }),
        ),
      }),
    })
    .parse(JSON.parse(await readFile(archivePath, 'utf8')))
  for (const endpoint of ['/post', '/routed']) {
    const entry = archive.log.entries.find((entry) => entry.request.url === origin + endpoint)
    const received = wire.find((record) => record.url === endpoint)
    assert.ok(entry?.request.postData && received)
    const body = entry.request.postData
    assert.equal(
      Buffer.from(body.text, body._encoding === 'base64' ? 'base64' : 'utf8').toString('base64'),
      received.base64,
    )
  }
  checks.push('Atomic public HAR preserves both genuine complete File multipart bodies')
  const replay = await browser.newContext()
  await replay.routeFromHAR(archivePath)
  await replay.setOffline(true)
  const offline = await replay.newPage()
  await offline.goto(origin + '/document')
  const before = wire.length
  const text = await offline.evaluate(
    async ({ base64, contentType }) => {
      const bytes = Uint8Array.from(atob(base64), (character) => character.charCodeAt(0))
      const response = await fetch('/routed', {
        method: 'POST',
        headers: { 'content-type': contentType },
        body: bytes,
      })
      return response.text()
    },
    { base64: routed.base64, contentType: routed.contentType },
  )
  assert.equal(text, 'actual multipart wire')
  assert.equal(wire.length, before)
  checks.push(
    'Native context HAR matches exact original multipart bytes while offline, with no second wire request',
  )
  await replay.close()
  const state = z
    .object({
      nativeRequestBodyPhysical: z.number(),
      pendingRequestBodyReads: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.equal(state.nativeRequestBodyPhysical, 0)
  assert.equal(state.pendingRequestBodyReads, 0)
  checks.push(
    'Every actual native request-post read receipt has retired after real context closure',
  )
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await source.close().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  if (
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex') !== binarySHA256
  )
    failures.push('Native executable changed during proof')
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, checks, failures, diagnostics, wire }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
