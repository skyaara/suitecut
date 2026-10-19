// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { chmod, mkdir, open, readFile, unlink, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeTimeoutError,
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(z.string().min(1).parse(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY))
const shim = resolve(z.string().min(1).parse(process.env.SUITECUT_SCREENSHOT_GATE_LIBRARY))
await mkdir(directory, { recursive: true })
const baselinePath = join(directory, 'readonly-baseline.png')
const releasePath = join(directory, 'release')
const wrapper = join(directory, 'native-wrapper')
/**
 * Quote an exact shell argument.
 * @param {string} value Exact shell argument.
 */
const quote = (value) => "'" + value.replaceAll("'", "'\\''") + "'"
await writeFile(
  wrapper,
  '#!/bin/sh\n' +
    [
      ['LD_PRELOAD', shim],
      ['SUITECUT_SCREENSHOT_GATE_PATH', baselinePath],
      ['SUITECUT_SCREENSHOT_GATE_EXECUTABLE', executable],
      ['SUITECUT_SCREENSHOT_GATE_DIRECTORY', directory],
    ]
      .map(([key, value]) => `export ${key}=${quote(z.string().parse(value))}\n`)
      .join('') +
    `exec ${quote(executable)} "$@"\n`,
  { mode: 0o700 },
)
const Counters = z.object({
  nativeScreenshotInputLogical: z.number().int().nonnegative(),
  nativeScreenshotInputPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonLogical: z.number().int().nonnegative(),
  nativeScreenshotComparisonPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonBytes: z.number().nonnegative(),
  nativeTraceBackgroundPhysical: z.number().int().nonnegative(),
})
const Marker = z.object({
  pid: z.number().int().positive(),
  tid: z.number().int().positive(),
  serial: z.number().int().positive(),
  fd: z.number().int().nonnegative(),
  result: z.number().int(),
  error: z.number().int(),
  statResult: z.number().int(),
  device: z.number(),
  inode: z.number(),
  size: z.number(),
  flags: z.number().int(),
  descriptorFlags: z.number().int(),
})
/** @type {{result:string,groups:string[],binarySHA256:string,shimSHA256:string,markers:z.infer<typeof Marker>[],held:z.infer<typeof Counters>[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number}} */
const report = {
  result: 'running',
  groups: [],
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  shimSHA256: createHash('sha256')
    .update(await readFile(shim))
    .digest('hex'),
  markers: [],
  held: [],
}
/** @type {import('../dist/native.js').NativeBrowser|undefined} */
let source
let stderr = '',
  phase = 'launch'
/** @type {Promise<unknown>[]} */
const operations = []
/**
 * Observe the actual operation immediately.
 * @template T
 * @param {Promise<T>} promise Real operation.
 * @returns {Promise<{ok:true,value:T}|{ok:false,error:Error}>} Actual receipt.
 */
function outcome(promise) {
  const observed = promise.then(
    (value) => ({ ok: true, value }),
    (error) => ({ ok: false, error: z.instanceof(Error).parse(error) }),
  )
  operations.push(observed)
  return observed
}
/**
 * Poll actual proof observations.
 * @template T
 * @param {()=>Promise<T|undefined>} inspect Real state observation.
 */
async function until(inspect) {
  const end = performance.now() + 5000
  for (;;) {
    const found = await inspect()
    if (found !== undefined) return found
    assert(performance.now() < end, 'Actual filesystem/ownership receipt missing')
    await setTimeout(5)
  }
}
/**
 * Read an actual syscall receipt marker.
 * @param {string} kind Marker phase.
 * @param {number} serial Actual native descriptor generation.
 */
async function marker(kind, serial) {
  return until(async () => {
    try {
      const result = Marker.parse(
        JSON.parse(await readFile(join(directory, `${kind}-${serial}.json`), 'utf8')),
      )
      report.markers.push(result)
      return result
    } catch (error) {
      if (z.object({ code: z.literal('ENOENT') }).safeParse(error).success) return undefined
      throw error
    }
  })
}
const release = () => writeFile(releasePath, 'actual read may proceed\n')
const hold = async () => {
  try {
    await unlink(releasePath)
  } catch (error) {
    if (!z.object({ code: z.literal('ENOENT') }).safeParse(error).success) throw error
  }
}
try {
  source = await launchNativeBrowser({
    executablePath: wrapper,
    width: 80,
    height: 60,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
      assert(Buffer.byteLength(stderr) < 2 * 1024 * 1024)
    },
  })
  const active = source,
    page = createNativePage(active)
  await page.setContent('<style>html,body{margin:0;background:#149cee}</style>')
  const baseline = await page.screenshot({ scale: 'css', animations: 'disabled' })
  await writeFile(baselinePath, baseline)
  await chmod(baselinePath, 0o444)
  const counters = async () => Counters.parse(await active.runAutomation('diagnostics'))
  const held = async (count = 1) => {
    const result = await until(async () => {
      const value = await counters()
      return value.nativeScreenshotInputPhysical === count ? value : undefined
    })
    assert(result.nativeScreenshotComparisonBytes >= baseline.length)
    report.held.push(result)
    return result
  }
  const empty = async () =>
    until(async () => {
      const value = await counters()
      return Object.values(value).every((number) => number === 0) ? value : undefined
    })
  /**
   * Validate a genuine descriptor generation.
   * @param {number} serial Real descriptor generation.
   */
  const entered = async (serial) => {
    const value = await marker('entered', serial)
    assert.equal(value.statResult, 0)
    assert.equal(value.flags & 3, 0)
    assert(value.flags & 2048, 'Actual baseline must use O_NONBLOCK')
    assert(value.descriptorFlags & 1, 'Actual baseline must use FD_CLOEXEC')
    assert.equal(value.size, baseline.length)
    await held()
    return value
  }
  /**
   * Validate a genuine descriptor generation.
   * @param {number} serial Real descriptor generation.
   */
  const retired = async (serial) => {
    await release()
    const value = await marker('closed', serial)
    assert.equal(value.result, 0)
    assert.equal(value.statResult, -1)
    await empty()
  }
  phase = 'original timeout retains actual read and descriptor until real close'
  await hold()
  const expired = outcome(
    expectNative(page).toHaveScreenshot(baselinePath, { timeout: 250, threshold: 0 }),
  )
  await entered(1)
  const expiration = await expired
  assert(!expiration.ok)
  assert(expiration.error instanceof NativeTimeoutError)
  assert.equal(expiration.error.name, 'TimeoutError')
  assert.match(expiration.error.message, /original deadline/)
  await held()
  await retired(1)
  report.groups.push(phase)
  phase = 'caller abort retains actual read after public rejection'
  await hold()
  const abort = new AbortController(),
    reason = new Error('actual screenshot file caller abort')
  const canceled = outcome(
    expectNative(page).toHaveScreenshot(baselinePath, { timeout: 0, signal: abort.signal }),
  )
  await entered(2)
  abort.abort(reason)
  const cancellation = await canceled
  assert(!cancellation.ok)
  assert.equal(cancellation.error, reason)
  await held()
  await retired(2)
  report.groups.push(phase)
  phase = 'readonly file mutation detected from actual fstat receipts'
  await hold()
  await chmod(baselinePath, 0o644)
  const author = await open(baselinePath, 'r+')
  await chmod(baselinePath, 0o444)
  const changed = outcome(expectNative(page).toHaveScreenshot(baselinePath, { timeout: 5000 }))
  await entered(3)
  await author.write(baseline.subarray(0, 1), 0, 1, 0)
  await author.sync()
  await author.close()
  await release()
  const mutation = await changed
  assert(!mutation.ok)
  assert.match(mutation.error.message, /changed while reading/)
  await marker('closed', 3)
  await empty()
  assert.deepEqual(await readFile(baselinePath), baseline)
  report.groups.push(phase)
  phase = 'context close waits actual baseline file ownership while unrelated page progresses'
  await hold()
  const contexts = new NativeBrowserContexts(active),
    context = await contexts.newContext(),
    child = await context.newPage()
  await child.setContent('<style>html,body{margin:0;background:#149cee}</style>')
  const closingAssertion = outcome(
    expectNative(child).toHaveScreenshot(baselinePath, { timeout: 0 }),
  )
  await entered(4)
  let closed = false
  const close = outcome(
    context.close().then(() => {
      closed = true
    }),
  )
  const closedAssertion = await closingAssertion
  assert(!closedAssertion.ok)
  await held()
  assert.equal(await page.evaluate(() => 42), 42)
  assert.equal(closed, false, 'Context resolved before actual retained file receipt')
  await retired(4)
  assert((await close).ok)
  report.groups.push(phase)
  report.result = 'pass'
} catch (error) {
  report.result = 'fail'
  report.phase = phase
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
} finally {
  await release()
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  }
  await Promise.all(operations)
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(join(directory, 'native-stderr.log'), stderr)
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(
  'Native held filesystem4 passed with actual read/close receipts, deadline, abort, readonly mutation and context lifetime',
)
