// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { gzipSync } from 'node:zlib'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_PRIVATE_IMAGE_DIRECTORY ?? '.suitecut/native-network-private-images',
)
await mkdir(directory, { recursive: true })
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([]),
  wire = z.array(z.string()).parse([])
const image = Buffer.from('R0lGODlhAQABAAAAACw=', 'base64')
const server = createServer((request, response) => {
  const path = request.url ?? ''
  wire.push(path)
  if (path.startsWith('/pixel')) {
    response.setHeader('content-type', 'image/gif')
    const bytes = path === '/pixel-gzip.gif' ? gzipSync(image) : image
    if (path === '/pixel-gzip.gif') response.setHeader('content-encoding', 'gzip')
    // Match the genuine zero-delay parser image case: one actual write,
    // without timers, artificial chunks or protocol receipt manipulation.
    response.end(bytes)
  } else {
    response.setHeader('content-type', 'text/html')
    response.end(
      path === '/page-plain.html'
        ? '<html><body><img src="/pixel.gif"></body></html>'
        : path === '/page-gzip.html'
          ? '<html><body><img src="/pixel-gzip.gif"></body></html>'
          : '<body>Actual document</body>',
    )
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const archive = resolve(directory, 'actual-private-images.har')
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 1280,
  height: 720,
  onDiagnostic: (message) => diagnostics.push(message),
})
try {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Native context transport is unavailable')
  const contexts = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const context = await contexts.newContext({ recordHar: { path: archive, content: 'embed' } })
  const page = await context.newPage()
  page.setDefaultTimeout(5000)
  for (const path of ['/pixel.gif', '/pixel-gzip.gif']) {
    const [response] = await Promise.all([
      page.waitForResponse(origin + path),
      page.goto(origin + (path === '/pixel.gif' ? '/page-plain.html' : '/page-gzip.html')),
    ])
    assert.deepEqual(await response.body(), image)
    assert.equal(response.request().existingResponse(), response)
    checks.push(`Actual zero-delay decoded image bytes survive eviction (${path})`)
  }
  checks.push('Both actual image responses retain their genuine canonical request identity')
  const first = await page.goto(origin + '/old-document')
  assert.ok(first)
  await page.goto(origin + '/new-document')
  let actual, error
  try {
    actual = await page.source.sendDevToolsCommand('Network.getResponseBody', {
      requestId: first.info.requestId,
    })
  } catch (cause) {
    error = String(cause)
  }
  if (error) await assert.rejects(first.body())
  else {
    const result = z.object({ body: z.string(), base64Encoded: z.boolean() }).parse(actual)
    assert.deepEqual(
      await first.body(),
      Buffer.from(result.body, result.base64Encoded ? 'base64' : 'utf8'),
    )
  }
  checks.push(
    'Ordinary Document body lifecycle still agrees with the genuine primary Inspector session',
  )
  await context.close()
  const har = z
    .object({
      log: z.object({
        entries: z.array(
          z.object({
            request: z.object({ url: z.string() }),
            response: z.object({
              content: z.object({ text: z.string().optional(), encoding: z.string().optional() }),
            }),
          }),
        ),
      }),
    })
    .parse(JSON.parse(await readFile(archive, 'utf8')))
  for (const path of ['/pixel.gif', '/pixel-gzip.gif']) {
    const entry = har.log.entries.find((entry) => entry.request.url === origin + path)
    assert.ok(entry)
    assert.deepEqual(
      Buffer.from(
        entry.response.content.text ?? '',
        entry.response.content.encoding === 'base64' ? 'base64' : 'utf8',
      ),
      image,
    )
  }
  for (const path of ['/pixel.gif', '/pixel-gzip.gif'])
    assert.equal(wire.filter((request) => request === path).length, 1)
  checks.push('Native HAR retains both exact decoded image bodies with no HTTP refetch')
  const state = z
    .object({
      nativePrivateBodyInnerReceipts: z.number(),
      nativePrivateBodyQueuedReads: z.number(),
      nativePrivateBodyRecords: z.number(),
      nativePrivateBodySessionReservations: z.number(),
      nativeImageStreamPhysical: z.number(),
      nativeImageStreamRetainedBytes: z.number(),
      nativeImageStreamEntries: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(state, {
    nativePrivateBodyInnerReceipts: 0,
    nativePrivateBodyQueuedReads: 0,
    nativePrivateBodyRecords: 0,
    nativePrivateBodySessionReservations: 1,
    nativeImageStreamPhysical: 0,
    nativeImageStreamRetainedBytes: 0,
    nativeImageStreamEntries: 0,
  })
  checks.push(
    'Actual context closure returns secondary and stream ownership to the default-page baseline',
  )
} catch (error) {
  failures.push(String(error))
} finally {
  await source.close()
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        mechanism:
          'Existing native streaming or exact dual-agent secondary fallback; no timing-specific path is forced.',
        wireDelivery: 'One response.end per real image, without artificial delay.',
        checks,
        failures,
        diagnostics,
        wire,
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(diagnostics, [])
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
