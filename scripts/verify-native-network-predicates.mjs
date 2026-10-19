// SPDX-License-Identifier: MIT
/* global fetch, setTimeout */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { nativeEventPredicatePhysicalCount } from '../dist/native-event-predicate.js'
import { launchNativeBrowser, createNativePage, NativeRequest } from '../dist/native.js'

const directory = resolve('.suitecut/native-network-predicates')
await mkdir(directory, { recursive: true })
const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const binaryHash = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const started = new Date().toISOString(),
  checks = [],
  diagnostics = []
let diagnosticBytes = 0,
  browser
const releases = z.array(z.function({ input: [z.boolean()], output: z.void() })).parse([])
const server = createServer((request, response) => {
  request.resume()
  response.setHeader('Content-Type', request.url === '/' ? 'text/html' : 'text/plain')
  response.end(
    request.url === '/'
      ? '<!doctype html><body>genuine native predicate fixture</body>'
      : 'actual native response',
  )
})
await new Promise((ready, reject) => {
  server.once('error', reject)
  server.listen(0, '127.0.0.1', ready)
})
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
const State = z
  .object({
    pendingEventPredicates: z.number().int(),
    networkWaiters: z.number().int(),
    otherOperations: z.number().int(),
  })
  .passthrough()
const drainMicrotasks = async () => {
  for (let i = 0; i < 20; ++i) await Promise.resolve()
}
try {
  browser = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    framesPerSecond: 30,
    headless: true,
    onDiagnostic: (value) => {
      if (diagnosticBytes < 256 * 1024) {
        diagnostics.push(value)
        diagnosticBytes += Buffer.byteLength(value)
      }
    },
  })
  const page = createNativePage(browser)
  await page.goto(origin)
  const regex = page.waitForRequest(/\/regex$/u)
  await page.evaluate(async () => await (await fetch('/regex')).text())
  const actual = await regex
  assert.ok(actual instanceof NativeRequest)
  assert.equal(actual.url(), `${origin}/regex`)
  checks.push('regex network wait decodes actual native request inside native predicate owner')

  await page.evaluate(() => {
    setTimeout(() => {
      void fetch('/unresolved')
    }, 50)
    return true
  })
  const waits = Array.from({ length: 64 }, () =>
    page.waitForRequest(() => new Promise((settle) => releases.push(settle)), { timeout: 500 }),
  )
  const all = Promise.allSettled(waits)
  const results = await all
  assert.equal(releases.length, 64)
  for (const result of results) {
    assert.equal(result.status, 'rejected')
    if (result.status === 'rejected') {
      assert.ok(result.reason instanceof Error)
      assert.match(result.reason.message, /Timeout|timed out/u)
    }
  }
  assert.equal(nativeEventPredicatePhysicalCount(page.source), 64)
  let state = State.parse(await browser.runAutomation('diagnostics'))
  assert.equal(state.pendingEventPredicates, 0)
  assert.equal(state.networkWaiters, 0)
  assert.equal(state.otherOperations, 0)
  checks.push(
    '64 actually unresolved host closures survive native timeout while all native owners retire',
  )

  let excessCalls = 0
  const excess = page.waitForRequest(
    () => {
      excessCalls++
      return true
    },
    { timeout: 2000 },
  )
  const excessRejected = assert.rejects(excess, /physical host callback capacity/u)
  await page.evaluate(async () => await (await fetch('/excess')).text())
  await excessRejected
  assert.equal(excessCalls, 0)
  assert.equal(nativeEventPredicatePhysicalCount(page.source), 64)
  state = State.parse(await browser.runAutomation('diagnostics'))
  assert.equal(state.pendingEventPredicates, 0)
  assert.equal(state.otherOperations, 0)
  checks.push(
    '65th actual host callback rejected at capacity without releasing unresolved closures',
  )

  for (const release of releases) release(false)
  await drainMicrotasks()
  assert.equal(nativeEventPredicatePhysicalCount(page.source), 0)
  releases.length = 0
  const cancellation = new AbortController()
  const canceled = page.waitForEvent('request', {
    timeout: 0,
    signal: cancellation.signal,
    predicate: () => new Promise((settle) => releases.push(settle)),
  })
  const canceledRejected = assert.rejects(canceled, /actual network host predicate cancel/u)
  await page.evaluate(async () => await (await fetch('/cancellation')).text())
  assert.equal(releases.length, 1)
  cancellation.abort(new Error('actual network host predicate cancel'))
  await canceledRejected
  assert.equal(nativeEventPredicatePhysicalCount(page.source), 1)
  state = State.parse(await browser.runAutomation('diagnostics'))
  assert.equal(state.pendingEventPredicates, 0)
  assert.equal(state.otherOperations, 0)
  for (const release of releases) release(false)
  await drainMicrotasks()
  assert.equal(nativeEventPredicatePhysicalCount(page.source), 0)
  checks.push(
    'actual AbortSignal cancels native journal owner and late host settlement releases physical closure',
  )
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binaryHash,
  )
  const report = {
    passed: true,
    started,
    completed: new Date().toISOString(),
    binaryHash,
    checks,
    diagnostics: state,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} catch (error) {
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        passed: false,
        started,
        binaryHash,
        checks,
        error: error instanceof Error ? error.stack : 'Unknown native network predicate failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  for (const release of releases) release(false)
  await browser?.close()
  server.closeAllConnections()
  await new Promise((ready) => server.close(ready))
  await writeFile(resolve(directory, 'native-stderr.log'), diagnostics.join(''))
}
