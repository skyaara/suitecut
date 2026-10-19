// SPDX-License-Identifier: MIT
// Actual native budget receipts. No replacement clock, browser driver or sleeps.
/* global setTimeout, XMLHttpRequest, location */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
const sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
const modulePath = process.env.SUITECUT_NATIVE_RUN_FOR_MODULE ?? `${sdk}/native-virtual-time.js`
assert(executable && sdk, 'Set the actual native executable and sealed SDK directory')
const directory = resolve(
  process.env.SUITECUT_NATIVE_RUN_FOR_PROOF_DIRECTORY ?? '/tmp/suitecut-native-run-for-proof',
)
await mkdir(directory, { recursive: true })
const inputPaths = [executable, modulePath, new URL(import.meta.url).pathname]
for (const name of await readdir(sdk)) if (name.endsWith('.js')) inputPaths.push(`${sdk}/${name}`)
const inputs = await Promise.all(
  [...new Set(inputPaths)].map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Actual dynamically selected SDK files are hashed before execution.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
/** @type {typeof import('../src/native-virtual-time.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The staged clock module is independently typechecked and hashed.
const clockModule = await import(pathToFileURL(modulePath).href)
/** @type {(()=>void)|undefined} */
let releaseHeld
/** @type {(()=>void)|undefined} */
let announceHeld
const server = createServer((request, response) => {
  if (request.url === '/held') {
    releaseHeld = () => response.end('released')
    announceHeld?.()
  } else {
    response.setHeader('content-type', 'text/html')
    response.end('<!doctype html><title>Native runFor</title>')
  }
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
let stderr = ''
let stderrObservedUtf8Bytes = 0
const stderrBound = 2 * 1024 * 1024
/** @type {{code:number|null,signal:NodeJS.Signals|null}|undefined} */
let nativeProcessExit
/** @type {Error[]} */
const behaviorErrors = []
const source = await native.launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (line) => {
    const data = Buffer.from(line, 'utf8')
    stderrObservedUtf8Bytes += data.length
    const retained = data.subarray(0, Math.max(0, stderrBound - Buffer.byteLength(stderr)))
    stderr += retained.toString('utf8')
    if (retained.length !== data.length)
      throw new Error('Actual native runFor diagnostic text exceeded its 2 MiB proof bound')
  },
})
assert(
  typeof source.runContext === 'function' &&
    typeof source.pageSource === 'function' &&
    typeof source.onContextEvent === 'function',
)
const browser = new native.NativeBrowserContexts({
  ...source,
  runContext: source.runContext.bind(source),
  pageSource: source.pageSource.bind(source),
  onContextEvent: source.onContextEvent.bind(source),
})
/** @type {import('../src/untrusted.js').UntrustedInput[]} */
const checks = []
/** Retains actual shutdown, stderr and helper-exit failures alongside behavior errors. */
async function finishActualNativeClockProof() {
  releaseHeld?.()
  /** @type {Error[]} */
  const finalizationErrors = []
  try {
    await source.close()
  } catch (error) {
    finalizationErrors.push(
      error instanceof Error
        ? error
        : new Error('Native runFor finalization rejected a non-Error value', { cause: error }),
    )
  }
  nativeProcessExit = source.processExit?.()
  server.closeAllConnections()
  await new Promise((accept) => server.close(accept))
  await writeFile(`${directory}/native-stderr.log`, stderr)
  await writeFile(
    `${directory}/native-finalization.json`,
    JSON.stringify(
      {
        actualNativeProcessExit: nativeProcessExit ?? null,
        stderrObservedUtf8Bytes,
        stderrRetainedUtf8Bytes: Buffer.byteLength(stderr),
        stderrOverflow: stderrObservedUtf8Bytes > stderrBound,
        strictStderrClean: stderrObservedUtf8Bytes === 0,
        failures: finalizationErrors.map((error) => error.stack),
      },
      null,
      2,
    ) + '\n',
  )
  if (stderrObservedUtf8Bytes !== 0)
    finalizationErrors.push(
      new Error('Actual native runFor process emitted diagnostic text; see native-stderr.log'),
    )
  if (nativeProcessExit?.code !== 0 || nativeProcessExit.signal !== null)
    finalizationErrors.push(
      new Error(
        'Actual native runFor helper did not exit with code 0 and no signal; see native-finalization.json',
      ),
    )
  if (finalizationErrors.length)
    throw new AggregateError(
      [...behaviorErrors, ...finalizationErrors],
      'Actual native runFor behavior or finalization failed',
    )
}
try {
  const context = await browser.newContext()
  const page = await context.newPage()
  const clock = new clockModule.NativeVirtualTime(page.source)
  await page.goto(prefix)
  await clock.installPaused({ time: 946684800000 })
  for (const method of ['Page.disable', 'Runtime.disable'])
    await assert.rejects(
      page.source.sendDevToolsCommand(method),
      /native current-document virtual-time ownership/u,
    )
  checks.push({ nativeClockDocumentObservationDomainsProtected: true })
  /** @type {[number|string,number,number][]} */
  const durationCases = [
    [100, 25, 4],
    ['08', 4000, 2],
    ['01:00', 6000, 10],
    ['02:34:10', 10000, 925],
    // One 25-hour native jump, with only two actual timer callbacks.
    ['25:00:00', 45000000, 2],
  ]
  for (const [duration, interval, expected] of durationCases) {
    const before = z.number().parse(await page.evaluate(() => Date.now()))
    await page.evaluate(
      `globalThis.nativeRunForCalls=[];globalThis.nativeRunForTimer=setInterval(()=>globalThis.nativeRunForCalls.push(Date.now()),${interval})`,
    )
    const status = await clock.runFor(duration)
    assert.equal(status.activeOwner, false)
    assert.equal(status.pendingPhysicalCommand, false)
    assert.equal(status.lastAcknowledgedPaused, true)
    const calls = z
      .array(z.number())
      .parse(
        await page.evaluate(
          'clearInterval(globalThis.nativeRunForTimer);globalThis.nativeRunForCalls',
        ),
      )
    assert.deepEqual(
      calls,
      Array.from({ length: expected }, (_, index) => before + (index + 1) * interval),
    )
    checks.push({
      duration,
      expected,
      actual: calls.length,
      nativePauseAcknowledged: true,
    })
  }
  const beforeInvalid = z.number().parse(await page.evaluate(() => Date.now()))
  for (const duration of [
    '8',
    '12:02:34:10',
    '00:60',
    '100:00:00',
    '08junk',
    ' 08',
    '',
    0,
    0.5,
    -1,
    1125899906843,
  ]) {
    await assert.rejects(clock.runFor(duration))
    assert.equal(await page.evaluate(() => Date.now()), beforeInvalid)
    const status = await clock.status()
    assert.equal(status.activeOwner, false)
    assert.equal(status.queued, 0)
  }
  checks.push({
    nativeRejectedMalformedOrUnsupportedDurations: 11,
    clockUnchanged: true,
  })
  await page.close()
  for (const mode of ['deadline', 'abort']) {
    const target = await context.newPage()
    const controlled = new clockModule.NativeVirtualTime(target.source)
    await target.goto(prefix)
    await controlled.installPaused({ time: 946684800000 })
    const held = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    await target.evaluate((url) => {
      setTimeout(() => {
        const request = new XMLHttpRequest()
        request.open('GET', url, false)
        request.send()
      }, 10)
    }, `${prefix}/held`)
    const abort = new AbortController()
    const reason = new Error('Exact native runFor cancellation cause')
    const failed = assert.rejects(
      controlled.runFor(
        '08',
        mode === 'deadline' ? { timeout: 150 } : { timeout: 2000, signal: abort.signal },
      ),
      mode === 'deadline' ? /Timeout|timed out/iu : (error) => error === reason,
    )
    await held
    if (mode === 'abort') abort.abort(reason)
    await failed
    const retained = await controlled.status()
    assert(retained.activeOwner && retained.pendingPhysicalCommand)
    assert(releaseHeld)
    releaseHeld()
    const settled = await controlled.runFor('01', { timeout: 2000 })
    assert.equal(settled.activeOwner, false)
    assert.equal(settled.pendingPhysicalCommand, false)
    assert.equal(settled.lastAcknowledgedPaused, true)
    await target.close()
    checks.push({ mode, nativeOwnerRetainedUntilActualPauseReceipt: true })
  }
  {
    const target = await context.newPage()
    const controlled = new clockModule.NativeVirtualTime(target.source)
    await target.goto(prefix)
    await controlled.installPaused({ time: 946684800000 })
    const closed = target.waitForEvent('close', { timeout: 3000 })
    await target.evaluate((url) => {
      setTimeout(() => {
        location.href = url
      }, 10)
    }, `${prefix}/after-clock-document`)
    const failed = assert.rejects(controlled.runFor('01'), /scope changed|closed|renderer/iu)
    await Promise.all([closed, failed])
    assert(target.isClosed())
    checks.push({ actualAuthorNavigationClosedDocumentOnlyClock: true })
  }
  {
    const target = await context.newPage()
    const controlled = new clockModule.NativeVirtualTime(target.source)
    await target.goto(prefix)
    await controlled.installPaused({ time: 946684800000 })
    const closed = target.waitForEvent('close', { timeout: 3000 })
    const crashed = target.waitForEvent('crash', { timeout: 3000 })
    const failed = assert.rejects(
      target.source.sendDevToolsCommand('Page.crash'),
      /scope changed|closed|renderer|crash/iu,
    )
    await Promise.all([closed, crashed, failed])
    assert(target.isClosed())
    await assert.rejects(target.reload(), /closed|current.document/iu)
    const healthy = z
      .object({ result: z.object({ value: z.number() }) })
      .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
    assert.equal(healthy.result.value, 42)
    checks.push({
      actualRendererDeathClosedDocumentOnlyClock: true,
      clockOwnedRecoveryUnsupported: true,
      healthyRoot: healthy.result.value,
    })
  }
  await context.close()
} catch (error) {
  behaviorErrors.push(
    error instanceof Error
      ? error
      : new Error('Native runFor behavior rejected a non-Error value', { cause: error }),
  )
  throw error
} finally {
  await finishActualNativeClockProof()
}
for (const input of inputs)
  assert.equal(
    createHash('sha256')
      .update(await readFile(input.path))
      .digest('hex'),
    input.sha256,
  )
await writeFile(
  `${directory}/report.json`,
  JSON.stringify(
    {
      checks,
      inputs,
      inputsUnchanged: true,
      actualNativeProcessExit: nativeProcessExit,
      strictStderrClean: stderrObservedUtf8Bytes === 0,
      scope:
        'Native current-document positive integer runFor budgets bounded by safe native microsecond targets, full two-digit-hour strings and C++ duration parsing; no zero/fractional budgets, animation/idle control, callback exception aggregation, context-wide clock or pauseAt/fastForward parity',
    },
    null,
    2,
  ),
)
console.log(`Actual native runFor: ${checks.length} groups passed`)
