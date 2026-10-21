// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import {
  launchNativeBrowser,
  createNativePage,
  NativeRoute,
  NativeRequest,
} from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-network-fallback')
await mkdir(directory, { recursive: true })
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const ownershipSamples = []
const executableSha256 = await hash(),
  started = new Date().toISOString(),
  checks = [],
  stderr = []
const server = createServer((request, response) => {
  void (async () => {
    const chunks = []
    for await (const chunk of request) {
      assert.ok(Buffer.isBuffer(chunk))
      chunks.push(chunk)
    }
    if (request.url === '/page') {
      response.setHeader('Content-Type', 'text/html')
      response.end('<!doctype html><body>native fallback author</body>')
      return
    }
    response.setHeader('Content-Type', 'application/json')
    response.end(
      JSON.stringify({
        method: request.method,
        url: request.url,
        headers: request.headers,
        body: Buffer.concat(chunks).toString('base64'),
      }),
    )
  })().catch((error) =>
    response.destroy(error instanceof Error ? error : new Error('Native fallback origin failed')),
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
const Echo = z.object({
  method: z.string(),
  url: z.string(),
  headers: z.record(z.string(), z.string()),
  body: z.string(),
})
let source, failure, diagnostics
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (message) => stderr.push(message),
  })
  const page = createNativePage(source),
    order = []
  await page.goto(`${origin}/page`)
  const lower = z
    .function({ input: [z.instanceof(NativeRoute), z.instanceof(NativeRequest)], output: z.void() })
    .implementAsync(async (route, request) => {
      order.push('lower')
      assert.equal(request.url(), `${origin}/changed`)
      assert.equal(request.method(), 'POST')
      assert.deepEqual(request.postDataBuffer(), Buffer.from([0, 255, 13, 10]))
      assert.equal(request.headers()['x-upper'], 'actual')
      await route.fallback({ headers: { ...request.headers(), 'X-Lower': 'actual' } })
    })
  await page.route('**/fallback', lower, { times: 1 })
  await page.route('**/changed', async (route) => {
    order.push('wrong rewritten match')
    await route.abort()
  })
  await page.route(
    '**/fallback',
    async (route) => {
      order.push('upper')
      await route.fallback({
        url: `${origin}/changed`,
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream', 'X-Upper': 'actual' },
        postData: Buffer.from([0, 255, 13, 10]),
      })
    },
    { times: 1 },
  )
  const response = Echo.parse(
    await page.evaluate(
      `fetch(${JSON.stringify(`${origin}/fallback`)}).then(response=>response.json())`,
    ),
  )
  assert.deepEqual(order, ['upper', 'lower'])
  assert.equal(response.url, '/changed')
  assert.equal(response.method, 'POST')
  assert.equal(response.body, 'AP8NCg==')
  assert.equal(response.headers['x-upper'], 'actual')
  assert.equal(response.headers['x-lower'], 'actual')
  checks.push(
    'native reverse frozen match plan forwards exact binary URL/method/header overrides through two handlers',
  )
  await page.evaluate(
    `fetch(${JSON.stringify(`${origin}/fallback`)}).then(response=>response.text())`,
  )
  assert.deepEqual(order, ['upper', 'lower'])
  checks.push('native finite invocation counts remove only exhausted handlers')
  await page.unroute('**/changed')
  // Supported large bodies now use the bounded chunked upload path. Verify
  // real fallback handoff and origin bytes before testing the rejection bound.
  const largeBody = Buffer.alloc(900000, 255)
  await page.route(
    '**/large',
    async (route, request) => {
      assert.deepEqual(request.postDataBuffer(), largeBody)
      await route.continue()
    },
    { times: 1 },
  )
  await page.route(
    '**/large',
    async (route) => {
      await route.fallback({ method: 'POST', postData: largeBody })
    },
    { times: 1 },
  )
  const large = Echo.parse(
    await page.evaluate(
      `fetch(${JSON.stringify(`${origin}/large`)}).then(response=>response.json())`,
    ),
  )
  assert.equal(large.method, 'POST')
  assert.deepEqual(Buffer.from(large.body, 'base64'), largeBody)
  checks.push(
    'Supported 900 KB body survives native chunked fallback, next-handler admission and actual origin echo',
  )
  let nativeCapacityError = ''
  await page.route(
    '**/retry',
    async (route, request) => {
      assert.deepEqual(request.postDataBuffer(), Buffer.from([0, 255, 13, 10]))
      await route.continue()
    },
    { times: 1 },
  )
  await page.route(
    '**/retry',
    async (route) => {
      try {
        await route.fallback({ method: 'POST', postData: Buffer.alloc(2 * 1024 * 1024 + 1, 255) })
      } catch (error) {
        nativeCapacityError = error instanceof Error ? error.message : 'Unknown native rejection'
      }
      assert.match(nativeCapacityError, /body exceeds 2 MiB/u)
      await route.fallback({ method: 'POST', postData: Buffer.from([0, 255, 13, 10]) })
    },
    { times: 1 },
  )
  const retried = Echo.parse(
    await page.evaluate(
      `fetch(${JSON.stringify(`${origin}/retry`)}).then(response=>response.json())`,
    ),
  )
  assert.equal(retried.body, 'AP8NCg==')
  assert.equal(retried.method, 'POST')
  checks.push(
    'actual out-of-bound fallback rejects before consuming the next handler and permits retry on the same physical pause',
  )
  // Fetch completion and the DevTools continuation ACK cross separate channels.
  // Require actual physical retirement; never infer it from the response receipt.
  const ownershipSchema = z.object({
    networkPaused: z.number(),
    networkRejectedPauses: z.number(),
    pendingNetworkCommands: z.number(),
    networkPausedMetadataBytes: z.number(),
    networkRoutes: z.number(),
  })
  const retirementDeadline = performance.now() + 5000
  let state
  for (;;) {
    diagnostics = await source.runAutomation('diagnostics', {})
    state = ownershipSchema.parse(diagnostics)
    ownershipSamples.push(state)
    assert.ok(
      performance.now() < retirementDeadline,
      `Physical network ownership did not retire within 5 seconds: ${JSON.stringify(state)}`,
    )
    if (Object.values(state).every((value) => value === 0)) break
    await delay(20)
  }
  assert.equal(state.networkPaused, 0)
  assert.equal(state.networkRejectedPauses, 0)
  assert.equal(state.pendingNetworkCommands, 0)
  assert.equal(state.networkPausedMetadataBytes, 0)
  assert.equal(state.networkRoutes, 0)
  checks.push('all physical interception owners and paused bytes release after genuine dispatch')
} catch (error) {
  failure = error instanceof Error ? error : new Error('Native fallback proof failed')
} finally {
  if (source)
    await source.close().catch((error) => {
      failure ??= error instanceof Error ? error : new Error('Native shutdown failed')
    })
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
  assert.equal(await hash(), executableSha256)
  await writeFile(resolve(directory, 'native-stderr.log'), stderr.join(''))
  const report = {
    ok: !failure,
    executableSha256,
    started,
    ended: new Date().toISOString(),
    checks,
    diagnostics,
    ownershipSamples,
    error: failure?.stack,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
if (failure) throw failure
