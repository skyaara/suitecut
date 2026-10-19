// SPDX-License-Identifier: MIT
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
const directory = resolve(
  process.env.SUITECUT_NATIVE_PRIVATE_BODY_DIRECTORY ?? '.suitecut/native-network-private-bodies',
)
await mkdir(directory, { recursive: true })
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z.array(z.string()).parse([])
const observations = z.array(z.unknown()).parse([])
const expected = '// Scripts will be pre-fetched'
const server = createServer((request, response) => {
  wire.push(request.url ?? '')
  if (request.url === '/prefetch.js') {
    response.setHeader('content-type', 'application/javascript')
    response.end(expected)
  } else {
    response.setHeader('content-type', 'text/html')
    response.end(
      request.url === '/prefetch.html'
        ? '<link rel="prefetch" href="/prefetch.js"><body></body>'
        : '<body>Actual document</body>',
    )
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const archive = resolve(directory, 'actual-prefetch.har')
const State = z.object({
  nativePrivateBodyInnerReceipts: z.number(),
  nativePrivateBodySessionReservations: z.number(),
  nativePrivateBodyQueuedReads: z.number(),
  nativePrivateBodyRecords: z.number(),
})
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (message) => diagnostics.push(message),
})
try {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Native context transport unavailable')
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const baseline = State.parse(await source.runAutomation('diagnostics', {}))
  assert.equal(baseline.nativePrivateBodyInnerReceipts, 0)
  assert.equal(baseline.nativePrivateBodyQueuedReads, 0)
  const context = await browser.newContext({
    recordHar: { path: archive, content: 'embed' },
  })
  const page = await context.newPage()
  page.setDefaultTimeout(5000)
  const target = z
    .object({ targetInfo: z.object({ targetId: z.string() }) })
    .parse(await page.source.sendDevToolsCommand('Target.getTargetInfo', {}))
  const actual = z
    .object({
      privateBodySessionReady: z.boolean(),
      privateBodyTargetId: z.string(),
      privateBodyInspectorSessionId: z.string(),
    })
    .parse(await page.source.runAutomation('diagnostics', {}))
  assert.equal(actual.privateBodySessionReady, true)
  assert.equal(actual.privateBodyTargetId, target.targetInfo.targetId)
  observations.push({ actualPrivateOwnership: actual })
  const responseReady = page.waitForResponse(origin + '/prefetch.js')
  const document = await page.goto(origin + '/prefetch.html')
  assert.ok(document)
  const response = await responseReady
  assert.equal(await response.text(), expected)
  assert.equal(response.request().existingResponse(), response)
  checks.push('Native public prefetched bytes use the genuine canonical request/response identity')
  let primary, primaryError
  try {
    primary = await page.source.sendDevToolsCommand('Network.getResponseBody', {
      requestId: response.info.requestId,
    })
  } catch (error) {
    primaryError = String(error)
  }
  if (!primaryError) assert.equal(z.object({ body: z.string() }).parse(primary).body, '')
  observations.push({
    primary,
    primaryError,
    requestId: response.info.requestId,
  })
  checks.push(
    'Genuine primary prefetch body remains unavailable while the private native reader recovers exact bytes',
  )
  await page.goto(origin + '/next.html')
  let oldPrimary, oldPrimaryError
  try {
    oldPrimary = await page.source.sendDevToolsCommand('Network.getResponseBody', {
      requestId: document.info.requestId,
    })
  } catch (error) {
    oldPrimaryError = String(error)
  }
  if (oldPrimaryError) await assert.rejects(document.body())
  else {
    const value = z.object({ body: z.string(), base64Encoded: z.boolean() }).parse(oldPrimary)
    assert.deepEqual(
      await document.body(),
      Buffer.from(value.body, value.base64Encoded ? 'base64' : 'utf8'),
    )
  }
  observations.push({
    documentRequestId: document.info.requestId,
    oldPrimary,
    oldPrimaryError,
  })
  checks.push(
    'Ordinary Document reads preserve the genuine primary session lifecycle after navigation',
  )
  await context.close()
  const har = z
    .object({
      log: z.object({
        entries: z.array(
          z.object({
            request: z.object({ url: z.string() }),
            response: z.object({
              content: z.object({
                text: z.string().optional(),
                encoding: z.string().optional(),
              }),
            }),
          }),
        ),
      }),
    })
    .parse(JSON.parse(await readFile(archive, 'utf8')))
  const entry = har.log.entries.find((entry) => entry.request.url === origin + '/prefetch.js')
  assert.ok(entry)
  assert.equal(
    Buffer.from(
      entry.response.content.text ?? '',
      entry.response.content.encoding === 'base64' ? 'base64' : 'utf8',
    ).toString(),
    expected,
  )
  assert.equal(wire.filter((path) => path === '/prefetch.js').length, 1)
  checks.push(
    'Native HAR retains exact prefetched bytes with one actual HTTP request and no refetch',
  )
  const final = State.parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(final, baseline)
  observations.push({ baseline, final })
  checks.push(
    'Actual context channel closure returns private receipt, queue, record and session reservations to their baseline',
  )
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
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
        observations,
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
