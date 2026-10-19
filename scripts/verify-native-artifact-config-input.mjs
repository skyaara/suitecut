// SPDX-License-Identifier: MIT
/* global document, getComputedStyle */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash, randomUUID } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'
import { promisify } from 'node:util'

import image from 'sharp'
import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeBrowserContexts,
  NativePage,
  NativeTimeoutError,
} from '../dist/native.js'

// Run from an isolated runtime root containing this script and its exact SDK.
// This script never manufactures native replies or substitutes image/PDF bytes.
const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-artifact-config-input',
)
await mkdir(directory, { recursive: true })
const html =
  '<!doctype html><style>html,body{margin:0;background:white}#box{position:absolute;left:10px;top:10px;width:40px;height:30px;background:rgb(10,20,30)}#secret{position:absolute;left:80px;top:10px;width:20px;height:20px;background:blue}</style><div id="box"></div><div id="secret"></div><p style="position:absolute;top:80px">Actual PDF body</p>'
/** @type {Map<string,import('node:http').ServerResponse>} */
const held = new Map()
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  if (request.url?.startsWith('/font-')) {
    held.set(request.url, response)
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end(html)
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const url = `http://127.0.0.1:${address.port}/actual`
const style =
  '#box{background:rgb(200,30,70)!important;width:45px!important}/*' + 'x'.repeat(140000) + '*/'
const State = z.object({
  nativeScreenshotInputLogical: z.number().int().nonnegative(),
  nativeScreenshotInputPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonBytes: z.number().int().nonnegative(),
  nativeArtifactInputRetained: z.number().int().nonnegative(),
  nativeArtifactInputReceipts: z.number().int().nonnegative(),
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
/** @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,maxCommandBytes:number,appendCommands:number,pdfSHA256?:string,diagnostics?:z.infer<typeof State>}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
  maxCommandBytes: 0,
  appendCommands: 0,
}
/** @type {Promise<unknown>[]} */
const observed = []
/**
 * Wait for actual native or server observations within the verifier budget.
 * @param {()=>Promise<boolean>|boolean} predicate The observed condition.
 * @param {string} description The failure diagnostic.
 */
async function until(predicate, description) {
  const end = performance.now() + 5000
  while (!(await predicate())) {
    assert(performance.now() < end, description)
    await setImmediate()
  }
}
/**
 * Observe a real failure immediately and require its native diagnostic.
 * @param {Promise<unknown>} promise The actual operation.
 * @param {RegExp} diagnostic The required diagnostic.
 */
async function rejects(promise, diagnostic) {
  try {
    await promise
  } catch (error) {
    assert(error instanceof Error)
    assert.match(error.message, diagnostic)
    return error
  }
  throw new Error('Actual native operation unexpectedly succeeded')
}
/**
 * Independently decode one real PNG pixel.
 * @param {Buffer} bytes The actual image.
 * @param {number} x The pixel column.
 * @param {number} y The pixel row.
 */
async function pixel(bytes, x, y) {
  const { data, info } = await image(bytes)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  return [...data.subarray((y * info.width + x) * 4, (y * info.width + x) * 4 + 4)]
}
/**
 * Measure actual envelopes without replacing native results.
 * @param {import('../dist/native.js').NativeBrowser} owner The actual source.
 */
function observe(owner) {
  const run = owner.runAutomation.bind(owner),
    control = owner.runContext?.bind(owner)
  assert(control)
  /**
   * Require the existing transport frame budget.
   * @param {object} value The actual command envelope.
   */
  const wire = (value) => {
    const count = Buffer.byteLength(JSON.stringify(value)) + 1
    report.maxCommandBytes = Math.max(report.maxCommandBytes, count)
    assert(count <= 65536, `Actual command exceeds64KiB: ${count}`)
  }
  owner.runAutomation = (command, params = {}, signal) => {
    wire({ ...params, command, pageId: owner.pageId, id: 2147483647, version: 1 })
    return run(command, params, signal)
  }
  owner.runContext = (action, params = {}, signal) => {
    wire({ command: 'context', action, params, pageId: 1, id: 2147483647, version: 1 })
    if (action === 'screenshotInput' && params.action === 'append') ++report.appendCommands
    return control(action, params, signal)
  }
}
/**
 * Start a real font request whose server response remains held.
 * @param {import('../dist/native.js').NativePage} page The actual page.
 * @param {string} name The unique ASCII font identity.
 */
async function startFont(page, name) {
  await page.evaluate((name) => {
    const rule = document.createElement('style')
    rule.textContent = `@font-face{font-family:held${name};src:url(/font-${name})}body{font-family:held${name}}`
    document.head.append(rule)
    void document.fonts.load('16px held' + name).catch(() => undefined)
  }, name)
  await until(() => held.has('/font-' + name), 'Actual font request was not observed')
  assert.equal(await page.evaluate(() => document.fonts.status), 'loading')
}
/**
 * Release the actual held HTTP response with an explicit font failure.
 * @param {string} name The observed font request identity.
 */
function releaseFont(name) {
  const response = held.get('/font-' + name)
  assert(response, 'Actual held font response is missing')
  held.delete('/font-' + name)
  response.statusCode = 404
  response.end()
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 160,
    height: 140,
    headless: true,
    onDiagnostic: (message) => {
      stderr += message
      assert(Buffer.byteLength(stderr) < 2 * 1024 * 1024)
    },
  })
  const active = source,
    page = createNativePage(active)
  observe(active)
  if (page.source !== active) observe(page.source)
  const control = page.source.runContext?.bind(page.source)
  assert(control)
  const state = async () => State.parse(await active.runAutomation('diagnostics'))
  const empty = async () => {
    await until(
      async () => Object.values(await state()).every((value) => value === 0),
      'Actual artifact/input receipts did not retire',
    )
    return state()
  }
  await page.goto(url)
  phase = 'large screenshot CSS, real mask and restoration'
  const before = report.appendCommands
  const captured = await page.screenshot({
    style,
    mask: [page.locator('#secret')],
    scale: 'css',
    timeout: 0,
    path: resolve(directory, 'large-style-mask.png'),
  })
  assert(report.appendCommands - before > 5)
  assert.deepEqual(await pixel(captured, 20, 20), [200, 30, 70, 255])
  assert.deepEqual(await pixel(captured, 85, 15), [255, 0, 255, 255])
  assert.equal(await page.locator('#box').evaluate((node) => getComputedStyle(node).width), '40px')
  assert.deepEqual(await pixel(await page.screenshot({ scale: 'css' }), 20, 20), [10, 20, 30, 255])
  await empty()
  report.groups.push(phase)

  phase = 'large locator and fixed-handle configuration retains genuine geometry'
  const locator = await page.locator('#box').screenshot({ style, scale: 'css' })
  assert.deepEqual(
    await image(locator)
      .metadata()
      .then(({ width, height }) => [width, height]),
    [45, 30],
  )
  assert.deepEqual(await pixel(locator, 5, 5), [200, 30, 70, 255])
  const handle = await page.locator('#box').elementHandle()
  assert(handle)
  try {
    const fixed = await handle.screenshot({ style, scale: 'css' })
    assert.deepEqual(
      await image(fixed)
        .metadata()
        .then(({ width, height }) => [width, height]),
      [45, 30],
    )
    assert.deepEqual(await pixel(fixed, 5, 5), [200, 30, 70, 255])
  } finally {
    await handle.dispose()
  }
  assert.equal(await page.locator('#box').evaluate((node) => getComputedStyle(node).width), '40px')
  await empty()
  report.groups.push(phase)

  phase = 'large PDF header/footer bytes and independent text decode'
  const pdfPath = resolve(directory, 'large-templates.pdf')
  const pdf = await page.pdf({
    path: pdfPath,
    displayHeaderFooter: true,
    headerTemplate:
      '<div style="font-size:10px">Actual prepared header</div><!--' + 'h'.repeat(150000) + '-->',
    footerTemplate:
      '<div style="font-size:10px">Actual prepared footer</div><!--' + 'f'.repeat(160000) + '-->',
    margin: { top: '1in', bottom: '1in' },
    timeout: 0,
  })
  assert(pdf.subarray(0, 5).equals(Buffer.from('%PDF-')))
  assert.deepEqual(await readFile(pdfPath), pdf)
  const { stdout: text } = await promisify(execFile)('pdftotext', [pdfPath, '-'])
  assert.match(text, /Actual prepared header/)
  assert.match(text, /Actual prepared footer/)
  assert.match(text, /Actual PDF body/)
  report.pdfSHA256 = createHash('sha256').update(pdf).digest('hex')
  await empty()
  report.groups.push(phase)

  phase = 'native purpose, chunk order and original preparation deadline'
  const body = Buffer.from(JSON.stringify({ action: 'screenshot', format: 'png', style }))
  /**
   * Prepare an actual configuration-only native lease.
   * @param {number} timeoutMs The original native timeout.
   */
  const prepare = async (timeoutMs) => {
    const clientToken = randomUUID()
    return {
      ...Ticket.parse(
        await page.source.runAutomation('artifact', {
          action: 'capturePrepare',
          kind: 'screenshot',
          clientToken,
          configBytes: body.length,
          timeoutMs,
        }),
      ),
      clientToken,
    }
  }
  const ticket = await prepare(0),
    identity = { ...ticket, timeoutMs: 0 }
  try {
    await rejects(
      page.source.runAutomation('artifact', {
        action: 'captureCommit',
        kind: 'pdf',
        inputToken: ticket.token,
        timeoutMs: 0,
      }),
      /purpose mismatch/,
    )
    await rejects(
      control('screenshotInput', {
        ...identity,
        action: 'append',
        section: 'config',
        offset: 1,
        data: body.subarray(0, 10).toString('base64'),
      }),
      /offset|order/,
    )
    await control('screenshotInput', {
      ...identity,
      action: 'append',
      section: 'config',
      offset: 0,
      data: body.subarray(0, 10).toString('base64'),
    })
    await rejects(
      control('screenshotInput', {
        ...identity,
        action: 'append',
        section: 'config',
        offset: 0,
        data: body.subarray(0, 10).toString('base64'),
      }),
      /duplicate|offset|order/,
    )
  } finally {
    await control('screenshotInput', { ...identity, action: 'discard' })
  }
  const expired = await prepare(30)
  await rejects(
    control('screenshotInput', { ...expired, action: 'watch', timeoutMs: 0 }),
    /original deadline/,
  )
  await rejects(
    page.source.runAutomation('artifact', {
      action: 'captureCommit',
      kind: 'screenshot',
      inputToken: expired.token,
      timeoutMs: 0,
    }),
    /stale|expired/,
  )
  await empty()
  report.groups.push(phase)

  phase =
    'constructor projection abort stops captures, PDF and explicit-baseline matchers after the first genuine chunk ACK'
  const baseline = await page.screenshot()
  for (const kind of ['page', 'locator', 'pdf', 'comparePage', 'compareLocator']) {
    const controller = new AbortController(),
      reason = new Error('actual constructor ' + kind + ' abort after native append ACK')
    const projection = new NativePage(page.source, controller.signal),
      owner = page.source
    assert(typeof owner.runContext === 'function')
    const originalControl = owner.runContext.bind(owner),
      originalRun = owner.runAutomation.bind(owner)
    let appends = 0,
      commits = 0
    owner.runContext = async (action, params = {}, signal) => {
      const actual = await originalControl(action, params, signal)
      if (action === 'screenshotInput' && params.action === 'append') {
        ++appends
        if (appends === 1) controller.abort(reason)
      }
      return actual
    }
    owner.runAutomation = (command, params = {}, signal) => {
      if (
        command === 'artifact' &&
        (params.action === 'captureCommit' || params.action === 'compareCommit')
      )
        ++commits
      return originalRun(command, params, signal)
    }
    try {
      const actual =
        kind === 'pdf'
          ? projection.pdf({
              headerTemplate: '<div>projection</div><!--' + 'x'.repeat(100000) + '-->',
              timeout: 0,
            })
          : kind === 'locator'
            ? projection.locator('#box').screenshot({ style, timeout: 0 })
            : kind === 'comparePage'
              ? expectNative(projection).toHaveScreenshot(baseline, { style, timeout: 0 })
              : kind === 'compareLocator'
                ? expectNative(projection.locator('#box')).toHaveScreenshot(baseline, {
                    style,
                    timeout: 0,
                  })
                : projection.screenshot({ style, timeout: 0 })
      assert.equal(await rejects(actual, /actual constructor/), reason)
      await empty()
      assert.equal(appends, 1, 'Constructor abort must prevent every later configuration chunk')
      assert.equal(commits, 0, 'Constructor abort must prevent native artifact commit')
      assert.equal(
        owner.signal.aborted,
        false,
        'The genuine source remains healthy after projection abort',
      )
    } finally {
      owner.runContext = originalControl
      owner.runAutomation = originalRun
    }
  }
  assert.equal(await page.evaluate(() => 7 * 6), 42)
  report.groups.push(phase)

  phase = 'original screenshot timeout retains actual pending font reply and bytes'
  await startFont(page, 'timeout')
  const timed = rejects(
    page.screenshot({ style, timeout: 250 }),
    /original deadline|timed out|Timeout/,
  )
  observed.push(timed)
  await until(
    async () => (await state()).nativeArtifactInputReceipts > 0,
    'Actual font callback was not admitted before its original timeout',
  )
  const error = await timed
  assert(error instanceof NativeTimeoutError)
  const retained = await state()
  assert.equal(retained.nativeScreenshotInputLogical, 0)
  assert.equal(retained.nativeArtifactInputRetained, 1)
  assert(retained.nativeArtifactInputReceipts > 0)
  assert(retained.nativeScreenshotComparisonBytes > 0)
  assert.equal(retained.artifacts, 0)
  assert.equal(await page.evaluate(() => 21 * 2), 42)
  releaseFont('timeout')
  await empty()
  assert.equal(await page.locator('#box').evaluate((node) => getComputedStyle(node).width), '40px')
  report.groups.push(phase)

  phase = 'caller abort retains genuine font callback until its real response'
  await startFont(page, 'abort')
  const aborted = new AbortController(),
    reason = new Error('actual prepared artifact caller abort')
  const pending = page.screenshot({ style, timeout: 0, signal: aborted.signal }),
    checked = rejects(pending, /actual prepared artifact caller abort/)
  observed.push(checked)
  await until(
    async () => (await state()).nativeArtifactInputReceipts > 0,
    'Actual prepared font DevTools call was not admitted',
  )
  aborted.abort(reason)
  assert.equal(await checked, reason)
  assert.equal((await state()).nativeArtifactInputRetained, 1)
  releaseFont('abort')
  await empty()
  assert.equal(await page.locator('#box').evaluate((node) => getComputedStyle(node).width), '40px')
  report.groups.push(phase)

  phase = 'native navigation retry preserves prepared configuration and original clock'
  await startFont(page, 'navigation')
  const navigationCapture = page.screenshot({ style, timeout: 5000 }),
    navigationObserved = navigationCapture.then(
      (value) => ({ value }),
      (/** @type {unknown} */ error) => ({ error }),
    )
  observed.push(navigationObserved)
  await until(
    async () => (await state()).nativeArtifactInputReceipts > 0,
    'Actual font preparation was not admitted before navigation',
  )
  await page.goto(url + '?navigated')
  releaseFont('navigation')
  const outcome = await navigationObserved
  if ('error' in outcome) throw outcome.error
  assert.deepEqual(await pixel(outcome.value, 20, 20), [200, 30, 70, 255])
  await empty()
  assert.equal(await page.locator('#box').evaluate((node) => getComputedStyle(node).width), '40px')
  report.groups.push(phase)

  phase = 'actual context close retires prepared callback on genuine channel close'
  assert(
    typeof active.runContext === 'function' &&
      typeof active.pageSource === 'function' &&
      typeof active.onContextEvent === 'function',
  )
  const contexts = new NativeBrowserContexts({
      signal: active.signal,
      runContext: active.runContext.bind(active),
      pageSource: active.pageSource.bind(active),
      onContextEvent: active.onContextEvent.bind(active),
    }),
    context = await contexts.newContext(),
    child = await context.newPage()
  try {
    await child.goto(url)
    await startFont(child, 'closed')
    const closingShot = rejects(child.screenshot({ style, timeout: 0 }), /closed|cancel|Target/)
    observed.push(closingShot)
    await until(
      async () => (await state()).nativeArtifactInputReceipts > 0,
      'Child prepared callback not admitted',
    )
    await context.close()
    await closingShot
    await empty()
    assert.equal(await page.evaluate(() => 6 * 7), 42)
  } finally {
    releaseFont('closed')
    await context.close()
  }
  report.groups.push(phase)
  report.diagnostics = await empty()
  report.result = 'passed'
} catch (error) {
  report.result = 'failed'
  report.phase = phase
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
  process.exitCode = 1
} finally {
  for (const response of held.values()) {
    response.statusCode = 404
    response.end()
  }
  held.clear()
  if (source)
    try {
      await source.close()
      report.nativeShutdown = 'clean'
    } catch (error) {
      report.nativeShutdown = String(error)
      report.result = 'failed'
      process.exitCode = 1
    }
  await Promise.allSettled(observed)
  await new Promise((done) => server.close(() => done(undefined)))
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(resolve(directory, 'native-stderr.log'), stderr)
  if (stderr) {
    report.result = 'failed'
    report.phase ??= 'strict native stderr'
    process.exitCode = 1
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
console.log(JSON.stringify(report))
