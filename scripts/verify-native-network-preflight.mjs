// SPDX-License-Identifier: MIT
/* global fetch */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts, NativeRequest } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve('.suitecut/native-network-preflight')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const wire = z
  .array(z.object({ method: z.string(), url: z.string(), corsMethod: z.string().optional() }))
  .parse([])
const requests = z.array(z.instanceof(NativeRequest)).parse([])
const contextRequests = z.array(z.instanceof(NativeRequest)).parse([])
const routes = z
  .array(z.object({ method: z.string(), url: z.string(), resourceType: z.string() }))
  .parse([])
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  const corsMethod = request.headers['access-control-request-method']
  wire.push({
    method: request.method ?? '',
    url: request.url ?? '',
    corsMethod: typeof corsMethod === 'string' ? corsMethod : undefined,
  })
  response.setHeader('access-control-allow-origin', request.headers.origin ?? '*')
  response.setHeader('access-control-allow-methods', 'POST, OPTIONS')
  response.setHeader('access-control-allow-headers', 'X-Suitecut-Probe, Content-Type')
  response.setHeader('access-control-max-age', '0')
  request.resume()
  if (request.method === 'OPTIONS') {
    response.writeHead(204)
    response.end()
    return
  }
  if (request.url === '/cors') {
    response.end('actual POST response')
    return
  }
  response.setHeader('content-type', 'text/html')
  response.end('<body>actual cross-origin transport</body>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://localhost:${address.port}`
const url = `http://127.0.0.1:${address.port}/cors`
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
  const context = new NativeBrowserContexts(source).defaultContext()
  const page = context.pages()[0]
  assert.ok(page)
  context.on('request', (request) => contextRequests.push(request))
  page.on('request', (request) => requests.push(request))
  await page.goto(`${origin}/parent`)
  await page.route('**/cors', async (route) => {
    const request = route.request()
    routes.push({
      method: request.method(),
      url: request.url(),
      resourceType: request.resourceType(),
    })
    await route.continue()
  })
  const result = await page.evaluate(
    async (url) =>
      await (
        await fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json', 'X-Suitecut-Probe': 'actual' },
          body: '{"actual":true}',
        })
      ).text(),
    url,
  )
  assert.equal(result, 'actual POST response')
  assert.equal(
    await page.evaluate(async (url) => (await fetch(url, { method: 'OPTIONS' })).status, url),
    204,
  )
  await page.waitForLoadState('networkidle', { timeout: 3000 })
  const actual = requests.filter((request) => request.url() === url)
  assert.deepEqual(
    actual.map((request) => request.method()),
    ['POST', 'OPTIONS'],
  )
  assert.ok(actual.every((request) => request.info.resourceType === 'Fetch'))
  checks.push('Same-URL authored POST and OPTIONS remain public with genuine Fetch provenance')
  const physical = wire.filter((request) => request.url === '/cors')
  assert.equal(physical.filter((request) => request.method === 'POST').length, 1)
  assert.deepEqual(
    physical.filter((request) => request.corsMethod).map((request) => request.corsMethod),
    ['POST', 'OPTIONS'],
  )
  assert.equal(
    physical.filter((request) => request.method === 'OPTIONS' && !request.corsMethod).length,
    1,
  )
  checks.push('Both automatic CORS handshakes and authored OPTIONS actually execute on the wire')
  for (const request of actual)
    assert.equal(
      contextRequests.find((candidate) => candidate.info.id === request.info.id),
      request,
    )
  checks.push('Context and Page project the same canonical authored request objects')
  const native = z
    .object({ activeRequests: z.literal(0), networkRequests: z.number().int().min(6) })
    .parse(await page.source.runAutomation('diagnostics', {}))
  assert.equal(native.activeRequests, 0)
  checks.push(
    'Private native request records retain preflights and actual terminal ownership settles',
  )
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
  const report = {
    binarySHA256,
    checks,
    failures,
    wire,
    requests: requests.map((request) => ({
      id: request.info.id,
      url: request.url(),
      method: request.method(),
      resourceType: request.info.resourceType,
    })),
    routes,
    remainingGap:
      'Early Fetch interception still observes automatic preflights before Network initiator metadata exists; no early routing heuristic is used.',
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
  if (failures.length) process.exitCode = 1
}
