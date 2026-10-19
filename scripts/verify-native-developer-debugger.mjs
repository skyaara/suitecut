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

import { createNativePage, launchNativeBrowser, NativeTimeoutError } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-developer-debugger',
)
await mkdir(directory, { recursive: true })
const diagnostic = await captureNativeStderr(directory)
const authorSource = `globalThis.actualStepValue=0;
function actualInner(){
  globalThis.actualStepValue+=2;
  return 7;
}
function actualOuter(){
  debugger;
  globalThis.actualStepValue+=1;
  const actualResult=actualInner();
  globalThis.actualStepValue+=4;
  return actualResult;
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
      '<!doctype html><button id="target">Real debugger</button><script src="/author.js"></script>',
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
  // Deliberately NO coverage API call before this first actual pause/source.
  // Script identity must come from the native inventory populated by the real
  // shared Debugger, independently of the lazy coverage controller.
  session = await page.createDebuggerSession({ timeout: 15_000 })
  const opened = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(opened.nativeDeveloperDebuggerSessions, 1)
  const waiting = session.waitForPause({ timeout: 15_000 })
  const waitingObserved = tracked(waiting)
  const author = tracked(page.evaluate('actualOuter()'))
  const first = await waiting
  value(await waitingObserved)
  assert.equal(first.callFrames[0]?.functionName, 'actualOuter')
  const firstFrame = first.callFrames[0]
  assert(firstFrame?.callFrameId)
  assert(firstFrame.location.scriptId)
  assert(firstFrame.scopeChain.some((scope) => scope.type === 'local' && scope.object.objectId))
  assert.equal(await session.scriptSource(firstFrame.location.scriptId), authorSource)
  checks.push(
    'Fresh page without coverage API: actual author pause/frame/scope IDs and exact served script bytes',
  )

  await assert.rejects(
    page.sendDevToolsCommand('Debugger.disable'),
    /Debugger.*(?:lease|owner|conflict)|(?:lease|owner|conflict).*Debugger/iu,
  )
  const stillPaused = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(stillPaused.nativeDeveloperDebuggerSessions, 1)
  assert(stillPaused.nativeDebuggerPauses > 0)
  checks.push(
    'Conflicting raw Debugger.disable rejects while the actual developer pause and native factory remain owned',
  )

  // Starting/stopping coverage must not require resuming the independently
  // owned author pause or disable the factory/developer Debugger domain.
  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  let current = await session.stepOver(first, { timeout: 15_000 })
  assert.equal(current.callFrames[0]?.functionName, 'actualOuter')
  const afterStepOver = current
  for (let steps = 0; current.callFrames[0]?.functionName !== 'actualInner' && steps < 8; ++steps)
    current = await session.stepInto(current, { timeout: 15_000 })
  assert.equal(current.callFrames[0]?.functionName, 'actualInner')
  current = await session.stepOut(current, { timeout: 15_000 })
  assert.equal(current.callFrames[0]?.functionName, 'actualOuter')
  const coverage = await page.coverage.stopJSCoverage()
  assert(
    coverage.some((entry) =>
      entry.functions.some(
        (fn) => fn.functionName === 'actualInner' && fn.ranges.some((range) => range.count > 0),
      ),
    ),
  )
  const pausedAfterCoverage = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert(
    pausedAfterCoverage.nativeDebuggerPauses > 0 &&
      pausedAfterCoverage.nativeDeveloperDebuggerSessions === 1,
  )
  checks.push(
    'Real stepOver/stepInto/stepOut and executed inner-function coverage; coverage start/stop preserves the sustained developer pause',
  )

  await assert.rejects(session.resume(afterStepOver), /stale|another session/iu)
  await assert.rejects(session.resume({ ...current }), /stale|another session/iu)
  await session.resume(current, { timeout: 15_000 })
  assert.equal(value(await author), 7)
  assert.equal(await page.evaluate('globalThis.actualStepValue'), 7)
  checks.push(
    'Consumed/forged pauses reject and real continuation ACKs precede the completed observable author effects',
  )

  transfer = await page.evaluateHandle('globalThis.actualTransfer')
  await session.close({ timeout: 15_000 })
  session = undefined
  const withoutDeveloper = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0,
    'Actual developer release did not settle',
  )
  assert.equal(withoutDeveloper.nativeDebuggerLeases, opened.nativeDebuggerLeases - 1)
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops'), 1)
  checks.push(
    'Actual developer cleanup preserves the native factory and the same genuine opaque DataTransfer',
  )

  assert.equal(await page.evaluate('(()=>{debugger;return 42})()'), 42)
  const unowned = await observed(
    (state) =>
      state.nativeDebuggerUnownedPauses > withoutDeveloper.nativeDebuggerUnownedPauses &&
      state.nativeDebuggerPauses === 0,
    'Actual unowned pause receipt did not auto-resume',
  )
  checks.push(
    'Default unowned author debugger pause still auto-resumes from a genuine native paused receipt',
  )

  session = await page.createDebuggerSession()
  const requested = session.pause({ timeout: 15_000 })
  const requestObserved = tracked(requested)
  const nextAuthor = tracked(page.evaluate('globalThis.actualRequestedEffect=59;59'))
  const requestPause = await requested
  value(await requestObserved)
  assert(requestPause.callFrames[0]?.callFrameId)
  await session.resume(requestPause)
  assert.equal(value(await nextAuthor), 59)
  await session.close()
  session = undefined
  checks.push(
    'Explicit Debugger.pause completes only with its real ACK and next-statement pause, then real resume permits author effect59',
  )

  session = await page.createDebuggerSession()
  await assert.rejects(
    session.waitForPause({ timeout: 20 }),
    (error) => error instanceof NativeTimeoutError,
  )
  await session.close()
  session = undefined
  checks.push(
    'No-pause observer expires with public TimeoutError without inventing a paused event or retiring a physical session',
  )

  session = await page.createDebuggerSession()
  const abort = new AbortController(),
    reason = new Error('original actual debugger pause cancellation')
  const canceled = tracked(session.pause({ timeout: 15_000, signal: abort.signal }))
  const armed = await observed(
    (state) =>
      state.nativeDeveloperDebuggerPhysical === 1 &&
      state.nativeDebuggerDeveloperReceipts === 1 &&
      state.nativeDebuggerPauses === 0,
    'Actual idle pause-command ACK/retained intent gate was not observed',
  )
  abort.abort(reason)
  const canceledResult = await canceled
  assert('error' in canceledResult && canceledResult.error === reason)
  const retained = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(retained.nativeDeveloperDebuggerPhysical, 1)
  assert.equal(retained.nativeDebuggerDeveloperReceipts, 1)
  const cleanup = tracked(session.close({ timeout: 15_000 }))
  const cleanupAuthor = tracked(page.evaluate('globalThis.actualCanceledEffect=73;73'))
  value(await cleanup)
  assert.equal(value(await cleanupAuthor), 73)
  session = undefined
  const afterCancel = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0,
    'Actual canceled pause/resume/lease cleanup did not settle',
  )
  assert.equal(afterCancel.nativeDebuggerLeases, unowned.nativeDebuggerLeases)
  checks.push(
    'Caller abort preserves acknowledged next-statement intent physically until real late pause/resume and canonical cleanup; actual effect73 succeeds',
  )

  const constructorAbortStates = []
  for (const paused of [false, true]) {
    const owner = new AbortController(),
      reason = new Error(`actual post-open constructor cancellation paused=${String(paused)}`)
    const projection = createNativePage(source, owner.signal)
    session = await projection.createDebuggerSession({ timeout: 0 })
    let actualAuthor
    if (paused) {
      const actualPause = tracked(session.waitForPause({ timeout: 0 }))
      actualAuthor = tracked(
        page.evaluate('debugger;globalThis.actualConstructorAbortEffect=97;97'),
      )
      value(await actualPause)
      assert.equal(
        stateSchema.parse(await source.runAutomation('diagnostics')).nativeDebuggerPauses,
        1,
      )
    }
    owner.abort(reason)
    await assert.rejects(session.pause(), (error) => error === reason)
    if (actualAuthor) assert.equal(value(await actualAuthor), 97)
    // This observation precedes public close: only the constructor-abort hook
    // can have admitted the canonical cleanup command.
    const retired = await observed(
      (state) =>
        state.nativeDeveloperDebuggerSessions === 0 &&
        state.nativeDeveloperDebuggerPhysical === 0 &&
        state.nativeDebuggerDeveloperReceipts === 0 &&
        state.nativeDebuggerPauses === 0,
      'Post-open constructor abort did not really retire native ownership',
    )
    await session.close()
    session = undefined
    constructorAbortStates.push({ paused, retired })
    checks.push(
      `Actual post-open constructor cancellation automatically closes ${paused ? 'paused' : 'idle'} session through real native cleanup; public close joins and original reason is preserved`,
    )
  }

  await transfer.dispose()
  transfer = undefined
  session = await page.createDebuggerSession()
  const finalWait = tracked(session.waitForPause({ timeout: 0 }))
  const beforeClose = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(beforeClose.nativeDeveloperDebuggerSessions, 1)
  assert.equal(beforeClose.activeHandles, 0)
  await source.close()
  assert.equal((await finalWait).ok, false)
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  session = undefined
  checks.push(
    'Actual helper process exit closes a live developer lease and zero-disabled observer; opaque user handles were disposed before closure',
  )
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
        armed,
        retained,
        afterCancel,
        beforeClose,
        constructorAbortStates,
        actualProcessExit: source.processExit?.(),
        scope: 'actual root document only; no renderer replacement/child/worker debugger/UI claim',
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
  // All authored promises were observed immediately, including the real author
  // Runtime calls which stay pending while the developer owns execution.
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
console.info('Native explicit developer debugger actual12 passed')
