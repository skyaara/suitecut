// SPDX-License-Identifier: MIT
/* global document, Image */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout } from 'node:timers'
import { gzipSync } from 'node:zlib'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_IMAGE_STREAM_DIRECTORY ?? '.suitecut/native-network-image-streams',
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
    // Real delayed wire chunks exercise both a buffered prefix and streamed
    // spans without changing any browser protocol receipt or scheduling hook.
    response.write(bytes.subarray(0, 4))
    setTimeout(() => response.end(bytes.subarray(4)), 20)
  } else {
    response.setHeader('content-type', 'text/html')
    response.end('<body></body>')
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const archive = resolve(directory, 'actual-image-streams.har')
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
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
  await page.goto(origin + '/document')
  for (const path of ['/pixel.gif', '/pixel-gzip.gif']) {
    const responseReady = page.waitForResponse(origin + path)
    await page.evaluate(async (path) => {
      await new Promise((done) => {
        const image = new Image()
        image.onload = image.onerror = () => done(undefined)
        image.src = path
        document.body.append(image)
      })
    }, path)
    const response = await responseReady
    assert.deepEqual(await response.body(), image)
    checks.push(`Actual Chromium streamed image bytes survive decoder eviction (${path})`)
  }
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
  checks.push('Native HAR retains exact genuine streamed image bytes for both real wire encodings')
  const state = z
    .object({
      nativeImageStreamPhysical: z.number(),
      nativeImageStreamRetainedBytes: z.number(),
      nativeImageStreamEntries: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(state, {
    nativeImageStreamPhysical: 0,
    nativeImageStreamRetainedBytes: 0,
    nativeImageStreamEntries: 0,
  })
  checks.push(
    'Actual context closure returns all image-stream physical and retained-byte counters to zero',
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
assert.equal(checks.length, 5)
