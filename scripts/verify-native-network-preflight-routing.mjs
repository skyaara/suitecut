// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
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
  process.env.SUITECUT_NATIVE_PREFLIGHT_ROUTING_DIRECTORY ?? '.suitecut/native-preflight-routing',
)
await mkdir(directory, { recursive: true })
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z.array(z.object({ path: z.string(), method: z.string() })).parse([])
const routes = z.array(z.object({ url: z.string(), method: z.string() })).parse([])
const requests = z.array(z.object({ url: z.string(), method: z.string() })).parse([])
const server = createServer((request, response) => {
  wire.push({ path: request.url ?? '', method: request.method ?? '' })
  if (request.url === '/empty.html') {
    response.setHeader('content-type', 'text/html')
    response.end('<body>Actual preflight routing</body>')
    return
  }
  response.setHeader('Access-Control-Allow-Origin', request.headers.origin ?? '*')
  response.setHeader('Access-Control-Allow-Credentials', 'true')
  response.setHeader('Access-Control-Allow-Methods', 'OPTIONS, GET, DELETE')
  response.setHeader(
    'Access-Control-Allow-Headers',
    request.headers['access-control-request-headers'] ?? '*',
  )
  response.setHeader('Access-Control-Max-Age', '0')
  response.setHeader('Cache-Control', 'no-store')
  response.statusCode = request.method === 'OPTIONS' ? 204 : 200
  response.end(request.method === 'OPTIONS' ? undefined : 'done')
})
await new Promise((done) => server.listen(0, () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const main = `http://127.0.0.1:${address.port}`
const cross = `http://localhost:${address.port}`
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
  const context = await browser.newContext()
  const page = await context.newPage()
  page.setDefaultTimeout(5000)
  page.on('request', (request) => requests.push({ url: request.url(), method: request.method() }))
  await page.goto(main + '/empty.html')
  /**
   * Perform a real author fetch across origins.
   * @param {string} path
   */
  const fetchActual = (path, method = 'DELETE') =>
    page.evaluate(
      async ({ url, method }) => {
        const response = await globalThis.fetch(url, {
          method,
          headers: { 'X-Genuine-Author': 'actual' },
        })
        return { status: response.status, text: await response.text() }
      },
      { url: cross + path, method },
    )
  /**
   * Read actual methods observed by this HTTP server.
   * @param {string} path
   */
  const methods = (path) =>
    wire.filter((request) => request.path === path).map((request) => request.method)
  assert.deepEqual(await fetchActual('/without-routing'), { status: 200, text: 'done' })
  assert.deepEqual(methods('/without-routing'), ['OPTIONS', 'DELETE'])
  checks.push(
    'Without interception Chromium sends the genuine automatic OPTIONS and DELETE on the wire',
  )
  await page.route(
    '**/intercepted',
    async (route, request) => {
      routes.push({ url: request.url(), method: request.method() })
      await route.continue()
    },
    { times: 1 },
  )
  assert.deepEqual(await fetchActual('/intercepted'), { status: 200, text: 'done' })
  assert.deepEqual(methods('/intercepted'), ['DELETE'])
  assert.deepEqual(
    routes
      .filter((request) => request.url === cross + '/intercepted')
      .map((request) => request.method),
    ['DELETE'],
  )
  checks.push(
    'An actual intercepted parent authorizes native preflight fulfillment without an SDK Route or HTTP OPTIONS',
  )
  assert.deepEqual(await fetchActual('/intercepted'), { status: 200, text: 'done' })
  assert.deepEqual(methods('/intercepted'), ['DELETE', 'OPTIONS', 'DELETE'])
  assert.equal(routes.filter((request) => request.url === cross + '/intercepted').length, 1)
  checks.push(
    'Automatic preflight does not consume the real route times and its zero-age policy does not survive exhaustion',
  )
  await page.route('**/authored-options', async (route, request) => {
    routes.push({ url: request.url(), method: request.method() })
    await route.continue()
  })
  assert.deepEqual(await fetchActual('/authored-options', 'OPTIONS'), { status: 204, text: '' })
  assert.deepEqual(methods('/authored-options'), ['OPTIONS'])
  assert.deepEqual(
    routes
      .filter((request) => request.url === cross + '/authored-options')
      .map((request) => request.method),
    ['OPTIONS'],
  )
  assert.deepEqual(
    requests
      .filter((request) => request.url === cross + '/authored-options')
      .map((request) => request.method),
    ['OPTIONS'],
  )
  checks.push(
    'A real author-issued OPTIONS retains its genuine handler, public request and HTTP wire identity',
  )
  assert.deepEqual(await fetchActual('/unmatched-parent'), { status: 200, text: 'done' })
  assert.deepEqual(methods('/unmatched-parent'), ['OPTIONS', 'DELETE'])
  assert.equal(routes.filter((request) => request.url === cross + '/unmatched-parent').length, 0)
  checks.push(
    'An unmatched parent retains genuine browser preflight and does not inherit an unrelated route policy',
  )
  await context.close()
  const state = z
    .object({ nativeClassifyingPauses: z.number(), nativeClassifyingBytes: z.number() })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(state, { nativeClassifyingPauses: 0, nativeClassifyingBytes: 0 })
  checks.push(
    'Actual native context channel close releases all classification metadata and physical control reservations',
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
        wire,
        routes,
        requests,
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(diagnostics, [])
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
