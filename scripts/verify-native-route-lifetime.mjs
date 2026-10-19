// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { nativeRouteCallbackPhysicalCount } from '../dist/native-route-callback.js'
import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-route-lifetime')
await mkdir(directory, { recursive: true })
const sha256 = () =>
  readFile(executable).then((bytes) => createHash('sha256').update(bytes).digest('hex'))
const executableSha256 = await sha256(),
  started = new Date().toISOString(),
  checks = [],
  failures = [],
  stderr = [],
  wire = []
const Resolve = z.function({ input: [], output: z.void() })
const Close = z.function({ input: [], output: z.promise(z.void()) })
let releaseSlow = () => undefined,
  beganSlow = () => undefined
const slowStarted = new Promise((resolve) => {
  beganSlow = Resolve.parse(resolve)
})
const slowRelease = new Promise((resolve) => {
  releaseSlow = Resolve.parse(resolve)
})
const server = createServer((request, response) => {
  wire.push(request.url)
  if (request.url === '/slow') {
    beganSlow()
    void slowRelease.then(() => {
      response.end('actual-in-flight-body')
    })
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end('<!doctype html><body>real route callback lifetime</body>')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
const resolvers = []
const sources = []
async function until(candidate, messageCandidate) {
  const predicate = z.function({ input: [], output: z.boolean() }).parse(candidate)
  const message = z.string().parse(messageCandidate)
  const deadline = Date.now() + 10000
  while (!predicate()) {
    if (Date.now() > deadline) throw new Error(message)
    await delay(5)
  }
}
async function start() {
  const source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (message) => stderr.push(message),
  })
  sources.push(source.close.bind(source))
  return { source, browser: new NativeBrowserContexts(source) }
}
async function heldPage(browserCandidate, countCandidate, labelCandidate) {
  const browser = z.instanceof(NativeBrowserContexts).parse(browserCandidate)
  const count = z.number().int().min(1).max(128).parse(countCandidate)
  const label = z
    .string()
    .regex(/^[a-z-]+$/u)
    .parse(labelCandidate)
  const context = await browser.newContext(),
    page = await context.newPage()
  await page.goto(`${origin}/page`)
  const held = []
  await page.route(`**/hold/${label}/*`, async () => {
    await new Promise((resolve) => {
      held.push(Resolve.parse(resolve))
      resolvers.push(Resolve.parse(resolve))
    })
  })
  await page.evaluate(
    `(() => { globalThis.actualHeldFetches = Promise.allSettled(Array.from({length:${count}},(_,id)=>fetch('/hold/${label}/'+id)));return ${count}; })()`,
  )
  await until(
    () => held.length === count,
    `Only ${held.length}/${count} actual native route handlers entered`,
  )
  return { context, page, held }
}
const Diagnostics = z.object({
  networkRouteCallbacks: z.number(),
  networkRunningRouteCallbacks: z.number(),
  networkPaused: z.number(),
})
let first, second
try {
  first = await start()
  const firstSignal = first.source.processSignal ?? first.source.signal
  const diagnostic = first.browser.defaultContext().pages()[0]
  assert.ok(diagnostic)
  const held = await heldPage(first.browser, 128, 'first')
  assert.equal(nativeRouteCallbackPhysicalCount(firstSignal), 128)
  let counts = Diagnostics.parse(await diagnostic.source.runAutomation('diagnostics', {}))
  assert.equal(counts.networkRouteCallbacks, 128)
  assert.equal(counts.networkRunningRouteCallbacks, 128)
  await held.page.close()
  counts = Diagnostics.parse(await diagnostic.source.runAutomation('diagnostics', {}))
  assert.equal(counts.networkRouteCallbacks, 128)
  assert.equal(counts.networkRunningRouteCallbacks, 128)
  assert.equal(nativeRouteCallbackPhysicalCount(firstSignal), 128)
  checks.push(
    '128 actual native started author callbacks remain physically owned after page closure',
  )
  const probe = await held.context.newPage()
  await probe.goto(`${origin}/page`)
  let invoked = 0
  await probe.route('**/hold/overflow', () => {
    ++invoked
  })
  assert.equal(await probe.evaluate(`fetch('/hold/overflow').then(()=>false,()=>true)`), true)
  assert.equal(invoked, 0)
  assert.ok(!wire.includes('/hold/overflow'))
  checks.push(
    'native global callback capacity rejects the 129th real request before author invocation or origin',
  )
  held.held.forEach((resolve) => Resolve.parse(resolve)())
  await until(
    () => nativeRouteCallbackPhysicalCount(firstSignal) === 0,
    'Closed-page author acknowledgments did not settle',
  )
  counts = Diagnostics.parse(await diagnostic.source.runAutomation('diagnostics', {}))
  assert.equal(counts.networkRouteCallbacks, 0)
  checks.push(
    'actual authored settlement acknowledges global leases after its source page is closed',
  )
  // A different native context owns this actual transfer; default page closure
  // must not replace its process-scoped pending request lifetime.
  try {
    const api = held.context.request
    const initialization = await api.get(`${origin}/initialize`)
    await initialization.dispose()
    const pending = api.get(`${origin}/slow`)
    void pending.catch(() => undefined)
    await slowStarted
    await diagnostic.close()
    releaseSlow()
    const response = await pending
    assert.equal(await response.text(), 'actual-in-flight-body')
    await response.dispose()
    checks.push('actual in-flight context HTTP transfer survives unrelated default page 1 closure')
  } catch (error) {
    releaseSlow()
    failures.push({
      group: 'in-flight context HTTP',
      error: error instanceof Error ? error.stack : String(error),
    })
  }
  const prior = await heldPage(first.browser, 64, 'restart-prior')
  await first.source.close()
  assert.equal(nativeRouteCallbackPhysicalCount(firstSignal), 64)
  second = await start()
  const secondSignal = second.source.processSignal ?? second.source.signal
  const current = await heldPage(second.browser, 64, 'restart-current')
  assert.equal(nativeRouteCallbackPhysicalCount(secondSignal), 64)
  checks.push(
    'actual unresolved host callbacks remain counted across native helper process replacement',
  )
  let overflowInvoked = 0
  await current.page.route('**/hold/restart-overflow', () => {
    ++overflowInvoked
  })
  assert.equal(
    await current.page.evaluate(`fetch('/hold/restart-overflow').then(()=>false,()=>true)`),
    true,
  )
  assert.equal(overflowInvoked, 0)
  assert.ok(!wire.includes('/hold/restart-overflow'))
  checks.push(
    'host physical capacity remains 128 across old and new genuine native helper processes',
  )
  prior.held.forEach((resolve) => Resolve.parse(resolve)())
  current.held.forEach((resolve) => Resolve.parse(resolve)())
  await until(
    () =>
      nativeRouteCallbackPhysicalCount(firstSignal) === 0 &&
      nativeRouteCallbackPhysicalCount(secondSignal) === 0,
    'Helper replacement callbacks did not physically finish',
  )
  const diagnosticSecond = second.browser.defaultContext().pages()[0]
  assert.ok(diagnosticSecond)
  counts = Diagnostics.parse(await diagnosticSecond.source.runAutomation('diagnostics', {}))
  assert.equal(counts.networkRouteCallbacks, 0)
  checks.push(
    'actual external resolution frees both SDK physical work and remaining native callback lease owners',
  )
} catch (error) {
  failures.push({
    group: 'route callback lifetime',
    error: error instanceof Error ? error.stack : String(error),
  })
} finally {
  releaseSlow()
  resolvers.forEach((resolve) => Resolve.parse(resolve)())
  await Promise.all(sources.map((close) => Close.parse(close)().catch(() => undefined)))
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  assert.equal(await sha256(), executableSha256, 'Native executable changed during immutable proof')
  await writeFile(resolve(directory, 'stderr.log'), stderr.join('\n'))
  const report = {
    ok: failures.length === 0,
    started,
    ended: new Date().toISOString(),
    executableSha256,
    checks,
    failures,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
  if (failures.length) process.exitCode = 1
}
