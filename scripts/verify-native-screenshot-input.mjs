// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash, randomUUID } from 'node:crypto'
import { chmod, mkdir, readFile, stat, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { promisify } from 'node:util'

import image from 'sharp'
import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeTimeoutError,
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-screenshot-input',
)
await mkdir(directory, { recursive: true })
const html = `<!doctype html><style>html,body{margin:0;background:white}canvas{display:block}</style><canvas width=320 height=240></canvas><script>const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d'),pixels=ctx.createImageData(320,240);let state=1234567;for(let i=0;i<pixels.data.length;i+=4){for(let j=0;j<3;++j){state^=state<<13;state^=state>>>17;state^=state<<5;pixels.data[i+j]=state&255}pixels.data[i+3]=255}ctx.putImageData(pixels,0,0)</script>`
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.setHeader('Cache-Control', 'no-store')
  response.end(html)
})
await new Promise((resolveListen) => server.listen(0, '127.0.0.1', () => resolveListen(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const url = `http://127.0.0.1:${address.port}/actual`
const counters = z.object({
  nativeScreenshotInputLogical: z.number().int().nonnegative(),
  nativeScreenshotInputPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonLogical: z.number().int().nonnegative(),
  nativeScreenshotComparisonPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonBytes: z.number().nonnegative(),
  nativeTraceBackgroundPhysical: z.number().int().nonnegative(),
  artifacts: z.number().int().nonnegative(),
  artifactPhysicalCapture: z.number().int().nonnegative(),
})
const Ticket = z.strictObject({
  token: z.string(),
  contextId: z.number().int().positive(),
  pageId: z.number().int().positive(),
})
/** @type {import('../dist/native.js').NativeBrowser|undefined} */
let source
let stderr = '',
  phase = 'launch'
/** @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,baselineBytes?:number,baselineSHA256?:string,maxInputCommandBytes:number,appendCommands:number,crashReceipt?:string,diagnostics?:z.infer<typeof counters>}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
  maxInputCommandBytes: 0,
  appendCommands: 0,
}
/** @type {Promise<void>[]} */
const observed = []
/**
 * Observe a real rejection immediately and validate its native diagnostic.
 * @param {Promise<unknown>} promise The actual native operation.
 * @param {RegExp} match The required actual diagnostic.
 */
async function rejects(promise, match) {
  try {
    await promise
  } catch (error) {
    assert(error instanceof Error)
    assert.match(error.message, match)
    return error
  }
  throw new Error('Actual native command unexpectedly succeeded')
}
/**
 * Independently decode actual PNG bytes.
 * @param {Buffer} bytes The actual image.
 */
async function pixels(bytes) {
  return image(bytes).ensureAlpha().raw().toBuffer({ resolveWithObject: true })
}
/**
 * Observe the genuine SDK transport without replacing native operation results.
 * @param {import('../dist/native.js').NativeBrowser} owner The actual helper source.
 */
function observeWire(owner) {
  const run = owner.runAutomation.bind(owner),
    context = owner.runContext?.bind(owner)
  assert(context)
  /**
   * Measure an actual outgoing command with the worst legal request ID.
   * @param {object} value The native command envelope.
   */
  const wire = (value) => {
    const bytes = Buffer.byteLength(JSON.stringify(value)) + 1
    report.maxInputCommandBytes = Math.max(report.maxInputCommandBytes, bytes)
    assert(bytes <= 65536, `Actual SDK input exceeds64KiB: ${bytes}`)
  }
  owner.runAutomation = (command, params = {}, signal) => {
    wire({ ...params, pageId: owner.pageId, command, id: 2147483647, version: 1 })
    return run(command, params, signal)
  }
  owner.runContext = (action, params = {}, signal) => {
    wire({
      pageId: 1,
      command: 'context',
      id: 2147483647,
      version: 1,
      action,
      params,
      ...(typeof params.timeoutMs === 'number' ? { timeoutMs: params.timeoutMs } : {}),
    })
    if (action === 'screenshotInput' && params.action === 'append') ++report.appendCommands
    return context(action, params, signal)
  }
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 360,
    height: 260,
    headless: true,
    onDiagnostic: (message) => {
      stderr += message
      assert(Buffer.byteLength(stderr) < 2 * 1024 * 1024)
    },
  })
  const active = source
  const page = createNativePage(active)
  observeWire(active)
  if (page.source !== active) observeWire(page.source)
  const control = page.source.runContext?.bind(page.source)
  assert(control)
  await page.goto(url)
  const empty = async () => {
    const end = performance.now() + 5000
    for (;;) {
      const state = counters.parse(await active.runAutomation('diagnostics'))
      if (Object.values(state).every((value) => value === 0)) return state
      assert(performance.now() < end, 'Native screenshot input/comparison ownership did not retire')
    }
  }
  phase = 'large real Buffer and disabled timeout'
  const baseline = await page.screenshot({ scale: 'css', animations: 'disabled' })
  assert(baseline.length > 65536 && baseline.length <= 2 * 1024 * 1024)
  report.baselineBytes = baseline.length
  report.baselineSHA256 = createHash('sha256').update(baseline).digest('hex')
  await writeFile(join(directory, 'actual-large-baseline.png'), baseline)
  await expectNative(page).toHaveScreenshot(baseline, { timeout: 0, threshold: 0 })
  await empty()
  report.groups.push(phase)
  phase = 'native readonly explicit PNG path'
  const path = join(directory, 'actual-readonly-baseline.png')
  await writeFile(path, baseline)
  await chmod(path, 0o444)
  await expectNative(page).toHaveScreenshot(path, { timeout: 5000, threshold: 0 })
  assert.deepEqual(await readFile(path), baseline)
  assert.equal((await stat(path)).mode & 0o222, 0)
  await empty()
  report.groups.push(phase)
  phase = 'large configuration real CSS effect and restoration'
  await page.evaluate(() => {
    const canvas = document.querySelector('canvas')
    if (canvas) canvas.style.visibility = 'hidden'
  })
  const white = await page.screenshot({ scale: 'css', animations: 'disabled' })
  await page.evaluate(() => {
    const canvas = document.querySelector('canvas')
    if (canvas) canvas.style.visibility = ''
  })
  const before = report.appendCommands
  await expectNative(page).toHaveScreenshot(white, {
    threshold: 0,
    style: 'canvas{visibility:hidden!important}/*' + 'x'.repeat(130001) + '*/',
  })
  assert(report.appendCommands - before > 5)
  const actual = await pixels(await page.screenshot({ scale: 'css', animations: 'disabled' })),
    expected = await pixels(baseline)
  assert.deepEqual(actual.info, expected.info)
  assert.deepEqual(actual.data, expected.data)
  await empty()
  report.groups.push(phase)
  /**
   * Prepare a real native lease for negative protocol checks.
   * @param {import('../dist/native.js').NativeBrowser} owner The actual source.
   * @param {number} timeout The original native timeout.
   * @param {number} configBytes The declared config byte count.
   */
  const prepare = async (owner, timeout = 0, configBytes = 2) => {
    const clientToken = randomUUID()
    return {
      ...Ticket.parse(
        await owner.runAutomation('artifact', {
          action: 'comparePrepare',
          clientToken,
          file: false,
          pngBytes: 33,
          configBytes,
          timeoutMs: timeout,
        }),
      ),
      clientToken,
    }
  }
  /**
   * Append actual bounded wire bytes.
   * @param {z.infer<typeof Ticket>} ticket The actual lease.
   * @param {number} offset The requested offset.
   * @param {Buffer} bytes The actual bytes.
   * @param {string} section The native section.
   */
  const append = (ticket, offset, bytes, section = 'config') =>
    control('screenshotInput', {
      ...ticket,
      action: 'append',
      section,
      offset,
      data: bytes.toString('base64'),
      timeoutMs: 0,
    })
  /**
   * Retire an actual lease by its preparation nonce.
   * @param {z.infer<typeof Ticket>&{clientToken:string}} ticket The actual lease.
   */
  const discard = (ticket) =>
    control('screenshotInput', {
      action: 'discard',
      pageId: ticket.pageId,
      clientToken: ticket.clientToken,
      timeoutMs: 0,
    })
  phase = 'actual duplicate incomplete malformed and stale native chunks'
  const ticket = await prepare(active)
  try {
    await append(ticket, 0, Buffer.from('{}'))
    await rejects(append(ticket, 0, Buffer.from('{}')), /duplicate|out of order/)
    await rejects(append(ticket, 0, Buffer.alloc(24577), 'png'), /24KiB|chunk/)
    await rejects(
      active.runAutomation('artifact', {
        action: 'compareCommit',
        inputToken: ticket.token,
        timeoutMs: 0,
      }),
      /incomplete/,
    )
  } finally {
    await discard(ticket)
  }
  await rejects(append(ticket, 0, Buffer.from('{}')), /stale|expired/)
  const malformed = await prepare(active, 0, 1)
  try {
    await append(malformed, 0, Buffer.from('{'))
    await append(malformed, 0, Buffer.alloc(33), 'png')
    await rejects(
      active.runAutomation('artifact', {
        action: 'compareCommit',
        inputToken: malformed.token,
        timeoutMs: 0,
      }),
      /configuration JSON/,
    )
  } finally {
    await discard(malformed)
  }
  await empty()
  report.groups.push(phase)
  phase = 'original native deadline expires before commit'
  const expiring = await prepare(active, 120)
  const expired = await rejects(
    control('screenshotInput', { ...expiring, action: 'watch', timeoutMs: 0 }),
    /original deadline expired/,
  )
  assert(expired instanceof NativeTimeoutError)
  assert.equal(expired.name, 'TimeoutError')
  await rejects(append(expiring, 0, Buffer.from('{}')), /stale|expired/)
  await discard(expiring)
  await empty()
  report.groups.push(phase)
  phase = 'genuine context and renderer generation ownership'
  const contexts = new NativeBrowserContexts(active),
    context = await contexts.newContext(),
    child = await context.newPage()
  await child.goto(url)
  const other = await prepare(child.source),
    rootTicket = await prepare(active)
  await rejects(
    append({ ...rootTicket, contextId: other.contextId }, 0, Buffer.from('{}')),
    /source\/context/,
  )
  await discard(rootTicket)
  const crashed = child.waitForEvent('crash', { timeout: 5000 }),
    crash = child.source.sendDevToolsCommand('Page.crash')
  observed.push(
    crash.then(
      () => {
        report.crashReceipt = 'acknowledged'
      },
      (error) => {
        report.crashReceipt = String(error)
      },
    ),
  )
  await crashed
  await child.reload()
  await rejects(append(other, 0, Buffer.from('{}')), /stale|expired/)
  await discard(other)
  await context.close()
  assert.equal(await page.evaluate(() => 40 + 2), 42)
  await empty()
  report.groups.push(phase)
  phase = 'real FIFO native file-type rejection without writer'
  const fifo = join(directory, 'actual-fifo')
  await promisify(execFile)('mkfifo', [fifo])
  await rejects(expectNative(page).toHaveScreenshot(fifo), /regular PNG file/)
  await empty()
  report.groups.push(phase)
  assert(report.appendCommands > 20)
  assert(report.maxInputCommandBytes < 65536)
  report.diagnostics = await empty()
  report.result = 'pass'
} catch (error) {
  report.result = 'fail'
  report.phase = phase
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  }
  await Promise.all(observed)
  await new Promise((resolveClose, reject) =>
    server.close((error) => (error ? reject(error) : resolveClose(undefined))),
  )
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(join(directory, 'native-stderr.log'), stderr)
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(
  'Actual native screenshot input7 passed: large PNG/path/config, real pixels, malformed/stale chunks, native original deadline, actual contexts/renderer generation and FIFO',
)
