// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-developer-frame-evaluation',
)
await mkdir(directory, { recursive: true })
const diagnostic = await captureNativeStderr(directory)
const authorSource = `globalThis.actualFrameEffect=0;
function actualFrameTarget(seed){
  let actualLocal=seed;
  debugger;
  globalThis.actualFrameEffect=actualLocal;
  return actualLocal;
}
globalThis.actualTransfer=new DataTransfer();globalThis.actualDrops=0;
document.querySelector('#target').addEventListener('drop',event=>{if(event.dataTransfer===globalThis.actualTransfer)++globalThis.actualDrops});
`
const server = createServer((request, response) => {
  if (request.url === '/author.js') {
    response.setHeader('Content-Type', 'text/javascript; charset=utf-8')
    response.end(authorSource)
  } else {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.end(
      '<!doctype html><button id="target">Real frame evaluation</button><script src="/author.js"></script>',
    )
  }
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic: diagnostic.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(15_000)
const stateSchema = z.object({
  nativeDeveloperDebuggerEvaluations: z.number().int().nonnegative(),
  nativeDeveloperDebuggerProcessEvaluations: z.number().int().nonnegative(),
  nativeDeveloperDebuggerBreakpoints: z.number().int().nonnegative(),
  nativeDeveloperDebuggerProcessBreakpoints: z.number().int().nonnegative(),
  nativeDeveloperDebuggerSessions: z.number().int().nonnegative(),
  nativeDeveloperDebuggerWaiters: z.number().int().nonnegative(),
  nativeDeveloperDebuggerPhysical: z.number().int().nonnegative(),
  nativeDebuggerDeveloperReceipts: z.number().int().nonnegative(),
  nativeDebuggerPhysical: z.number().int().nonnegative(),
  nativeDebuggerPauses: z.number().int().nonnegative(),
  nativeDebuggerLeases: z.number().int().nonnegative(),
  nativeDebuggerUnownedPauses: z.number().int().nonnegative(),
  activeHandles: z.number().int().nonnegative(),
})
/**
 * Observe actual native ownership without manufacturing execution events.
 * @param {(state:z.output<typeof stateSchema>)=>boolean} predicate
 * @param {string} message
 */
async function observed(predicate, message) {
  const end = performance.now() + 15_000
  while (true) {
    const value = stateSchema.parse(await source.runAutomation('diagnostics'))
    if (predicate(value)) return value
    assert(performance.now() < end, message)
  }
}
/** @type {Promise<unknown>[]} */ const drains = []
/**
 * Observe each authored promise immediately, including its failure path.
 * @param {Promise<unknown>} pending
 */
function tracked(pending) {
  const result = pending.then(
    (value) => ({ ok: true, value }),
    (/** @type {unknown} */ error) => ({ ok: false, error }),
  )
  drains.push(result)
  return result
}
/**
 * Return a genuine completion or propagate its original observed error.
 * @param {Awaited<ReturnType<typeof tracked>>} result
 */
function value(result) {
  if ('error' in result) throw result.error
  return result.value
}
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
/** @type {import('../dist/native.js').NativeDebuggerSession|undefined} */ let session
/** @type {import('../dist/native.js').NativeJSHandle|undefined} */ let transfer
try {
  await page.goto(`http://127.0.0.1:${address.port}/`)
  session = await page.createDebuggerSession()
  const opened = stateSchema.parse(await source.runAutomation('diagnostics'))
  const firstWait = session.waitForPause({ timeout: 15_000 }),
    firstObserved = tracked(firstWait)
  const firstAuthor = tracked(page.evaluate('actualFrameTarget(5)'))
  const first = await firstWait
  value(await firstObserved)
  const frame = first.callFrames[0]
  assert(frame)
  assert.equal(frame.functionName, 'actualFrameTarget')
  assert.equal(await session.scriptSource(frame.location.scriptId), authorSource)
  checks.push(
    'Fresh no-coverage root: actual author paused frame and exact source bytes before evaluation',
  )

  const primitives = [
    ['actualLocal', 5],
    ['undefined', undefined],
    ['null', null],
    ['true', true],
    ['-0', -0],
    ['NaN', Number.NaN],
    ['Infinity', Number.POSITIVE_INFINITY],
    ['-Infinity', Number.NEGATIVE_INFINITY],
    ['9007199254740993n', 9007199254740993n],
    ["'\\ud800'", '\ud800'],
  ]
  for (const [expression, expected] of primitives) {
    assert.equal(typeof expression, 'string')
    assert(typeof expression === 'string')
    const result = await session.evaluateOnCallFrame(first, frame, expression)
    assert.equal(result.kind, 'value')
    assert(result.kind === 'value')
    assert(Object.is(result.value, expected), `Actual primitive mismatch for ${expression}`)
  }
  const afterPrimitives = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(afterPrimitives.nativeDeveloperDebuggerEvaluations, 0)
  assert.equal(afterPrimitives.nativeDeveloperDebuggerProcessEvaluations, 0)
  assert.equal(afterPrimitives.nativeDebuggerPauses, 1)
  checks.push(
    'Actual local values, undefined/null/boolean, special numbers, bigint and isolated UTF16 string remain primitive by-value results; every native result group releases',
  )

  const exception = await session.evaluateOnCallFrame(
    first,
    frame,
    "(()=>{throw new Error('actual paused exception')})()",
  )
  assert(exception.kind === 'exception')
  assert(exception.exception.description?.includes('actual paused exception'))
  const thrown = await session.evaluateOnCallFrame(first, frame, "(()=>{throw '\\udfff'})()")
  assert(thrown.kind === 'exception')
  assert.equal(thrown.exception.thrown, '\udfff')
  assert.equal(
    stateSchema.parse(await source.runAutomation('diagnostics')).nativeDeveloperDebuggerEvaluations,
    0,
  )
  checks.push(
    'Genuine thrown Error description/location and isolated-unit thrown primitive survive actual exception and group-release receipts',
  )

  await assert.rejects(
    session.evaluateOnCallFrame(first, frame, '({actualObject:1})'),
    /primitive|remote objects/iu,
  )
  await assert.rejects(
    session.evaluateOnCallFrame(first, frame, '(()=>1)'),
    /primitive|remote objects/iu,
  )
  await assert.rejects(
    session.evaluateOnCallFrame({ ...first }, frame, '1'),
    /actual pause and frame/iu,
  )
  await assert.rejects(
    session.evaluateOnCallFrame(first, { ...frame }, '1'),
    /actual pause and frame/iu,
  )
  const noObjects = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(noObjects.nativeDeveloperDebuggerEvaluations, 0)
  assert.equal(noObjects.nativeDebuggerPauses, 1)
  assert.equal(noObjects.activeHandles, 0)
  checks.push(
    'Unsupported real object/function results release actual groups without remote adoption; forged pause/frame objects reject while the real pause remains',
  )

  const local = await session.evaluateOnCallFrame(first, frame, 'actualLocal += 7')
  assert.deepEqual(local, { kind: 'value', value: 12 })
  const preabort = new AbortController(),
    originalReason = new Error('actual paused evaluation caller abort')
  preabort.abort(originalReason)
  await assert.rejects(
    session.evaluateOnCallFrame(first, frame, 'actualLocal++', { signal: preabort.signal }),
    (error) => error === originalReason,
  )
  await session.resume(first)
  assert.equal(value(await firstAuthor), 12)
  assert.equal(await page.evaluate('actualFrameEffect'), 12)
  await assert.rejects(session.evaluateOnCallFrame(first, frame, '1'), /actual pause and frame/iu)
  checks.push(
    'Direct evaluation mutates the genuine local to12; pre-aborted admission preserves it and real resume produces actual effect12; consumed frame cannot be reused',
  )

  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  const coveredWait = session.waitForPause({ timeout: 15_000 }),
    coveredObserved = tracked(coveredWait)
  const coveredAuthor = tracked(page.evaluate('actualFrameTarget(20)'))
  const covered = await coveredWait
  value(await coveredObserved)
  const coveredFrame = covered.callFrames[0]
  assert(coveredFrame)
  assert.deepEqual(await session.evaluateOnCallFrame(covered, coveredFrame, 'actualLocal+1'), {
    kind: 'value',
    value: 21,
  })
  const coverage = await page.coverage.stopJSCoverage()
  assert(coverage.some((entry) => entry.source === authorSource))
  assert.deepEqual(await session.evaluateOnCallFrame(covered, coveredFrame, 'actualLocal'), {
    kind: 'value',
    value: 20,
  })
  await session.resume(covered)
  assert.equal(value(await coveredAuthor), 20)
  await session.close()
  session = undefined
  transfer = await page.evaluateHandle('actualTransfer')
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('actualDrops'), 1)
  checks.push(
    'Actual coverage start/stop preserves paused-frame evaluation and exact author source; developer close preserves genuine factory opaque dispatch',
  )

  const owner = new AbortController(),
    reason = new Error('actual frame-evaluation owner canceled')
  session = await createNativePage(source, owner.signal).createDebuggerSession({ timeout: 0 })
  const ownerWait = session.waitForPause({ timeout: 15_000 }),
    ownerObserved = tracked(ownerWait)
  const ownerAuthor = tracked(page.evaluate('actualFrameTarget(30)'))
  const ownerPause = await ownerWait
  value(await ownerObserved)
  const ownerFrame = ownerPause.callFrames[0]
  assert(ownerFrame)
  assert.deepEqual(await session.evaluateOnCallFrame(ownerPause, ownerFrame, 'actualLocal'), {
    kind: 'value',
    value: 30,
  })
  owner.abort(reason)
  await assert.rejects(
    session.evaluateOnCallFrame(ownerPause, ownerFrame, 'actualLocal++'),
    (error) => error === reason,
  )
  assert.equal(value(await ownerAuthor), 30)
  const afterAbort = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerEvaluations === 0 &&
      state.nativeDeveloperDebuggerProcessEvaluations === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0,
    'Actual evaluation/session owner cleanup did not settle',
  )
  await session.close()
  session = undefined
  assert.equal(afterAbort.nativeDebuggerLeases, opened.nativeDebuggerLeases - 1)
  checks.push(
    'Constructor abort after real evaluation rejects the original reason, automatically resumes actual effect30 and completes canonical native cleanup before public close',
  )

  await transfer.dispose()
  transfer = undefined
  const beforeClose = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(beforeClose.activeHandles, 0)
  await source.close()
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  checks.push(
    'Actual helper process exits0 after no evaluation groups/sessions/opaque handles remain',
  )
  assert.equal(checks.length, 8)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        checks,
        binarySHA256: createHash('sha256')
          .update(await readFile(executablePath))
          .digest('hex'),
        opened,
        afterPrimitives,
        noObjects,
        afterAbort,
        beforeClose,
        actualProcessExit: source.processExit?.(),
        scope:
          'actual current-root primitive paused-frame evaluation only; no remote objects, promise awaiting, child/worker frame or replacement recovery claim; held eval-ACK cancellation and actual realm-death receipt cases remain controlled native proof',
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, error: String(error) }, null, 2) + '\n',
  )
  throw error
} finally {
  try {
    if (session && source.state !== 'closed')
      await session.close({ timeout: 15_000 }).catch((error) => errors.push(error))
    if (transfer && source.state !== 'closed')
      await transfer.dispose().catch((error) => errors.push(error))
    await finishNativeProof(source, diagnostic, directory, errors)
    await Promise.all(drains)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native actual paused-frame evaluation8 passed')
