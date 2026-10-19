// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage, NativeRequest } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_RESPONSE_EXTRAS_DIRECTORY ??
    '.suitecut/native-network-response-extras',
)
await mkdir(directory, { recursive: true })
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([])
const observations = z
  .array(
    z.object({
      chain: z.array(z.object({ url: z.string(), header: z.string().optional() })),
      intercepted: z.boolean(),
    }),
  )
  .parse([])
const wire = z.array(z.string()).parse([])
const headersReady = new Map(),
  finished = new Set(),
  lateReceipts = z
    .array(
      z.object({
        requestId: z.string(),
        url: z.string(),
        responseURL: z.string(),
        header: z.string().optional(),
      }),
    )
    .parse([]),
  lateAssociationErrors = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  const path = request.url ?? ''
  wire.push(path)
  response.setHeader('sec-test-header', path)
  response.setHeader('content-type', 'text/html')
  if (path.endsWith('/one') || path.endsWith('/two')) {
    response.statusCode = 302
    response.setHeader('location', path.endsWith('/one') ? './two' : './final')
  }
  response.end('<body>real native redirect response</body>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (message) => diagnostics.push(message),
})
if (!source.onNetworkMessage) throw new Error('Native response metadata transport is unavailable')
source.onNetworkMessage((message) => {
  const event = z
    .object({
      event: z.string(),
      request: z.object({ id: z.string(), url: z.string() }),
      response: z
        .object({
          headersReady: z.boolean().optional(),
          status: z.number(),
          url: z.string(),
          headers: z.record(z.string(), z.string()),
        })
        .optional(),
    })
    .parse(message)
  const id = event.request.id,
    ready = event.response?.headersReady
  if (
    ready === true &&
    headersReady.get(id) === false &&
    finished.has(id) &&
    event.response &&
    event.response.status >= 300 &&
    event.response.status < 400
  ) {
    const header = event.response.headers['sec-test-header']
    lateReceipts.push({
      requestId: id,
      url: event.request.url,
      responseURL: event.response.url,
      header,
    })
    if (event.response.url !== event.request.url || header !== new URL(event.request.url).pathname)
      lateAssociationErrors.push(`Native late raw headers do not belong to logical request ${id}`)
  }
  if (ready !== undefined) headersReady.set(id, ready)
  if (event.event === 'requestfinished') finished.add(id)
})
try {
  const page = createNativePage(source)
  page.setDefaultTimeout(5000)
  for (const intercepted of [false, true]) {
    if (intercepted) await page.route('**/*', (route) => route.continue())
    const response = await page.goto(
      `http://127.0.0.1:${address.port}/${intercepted ? 'routed' : 'direct'}/one`,
    )
    assert.ok(response)
    const requests = z.array(z.instanceof(NativeRequest)).parse([])
    let request = z.instanceof(NativeRequest).nullable().parse(response.request())
    for (; request; request = request.redirectedFrom()) requests.unshift(request)
    assert.equal(requests.length, 3)
    const chain = []
    for (const request of requests) {
      const observed = await request.response()
      assert.ok(observed)
      assert.equal(observed.request(), request)
      const headers = await observed.allHeaders()
      chain.push({ url: request.url(), header: headers['sec-test-header'] })
    }
    assert.deepEqual(
      chain.map((entry) => entry.header),
      chain.map((entry) => new URL(entry.url).pathname),
    )
    observations.push({ chain, intercepted })
    checks.push(
      `Actual three-hop raw headers and canonical identities after completion (${intercepted ? 'native interception' : 'direct'})`,
    )
    if (intercepted) await page.unrouteAll()
  }
  assert.deepEqual(
    lateAssociationErrors,
    [],
    'Actual late receipts must preserve logical request/response/header association',
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
        observations,
        lateReceipts,
        lateOrderObserved: lateReceipts.length > 0,
        lateOrderCoverage:
          lateReceipts.length > 0
            ? 'Genuine native late transition observed and association asserted'
            : 'Not exercised by this live run; legal late order has separate controlled native component coverage',
        lateAssociationErrors,
        wire,
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(diagnostics, [], 'Native stderr must remain empty')
assert.deepEqual(failures, [])
assert.equal(checks.length, 2)
