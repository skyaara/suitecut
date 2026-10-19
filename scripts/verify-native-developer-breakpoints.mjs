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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-developer-breakpoints',
)
await mkdir(directory, { recursive: true })
const diagnostic = await captureNativeStderr(directory)
const authorSource = `globalThis.actualBreakpointValue=0;
function actualBreakpointTarget(value){
  globalThis.actualBreakpointValue+=value;
  return globalThis.actualBreakpointValue;
}
function actualBreakpointSeed(){debugger;return 1;}
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
      '<!doctype html><button id="target">Real breakpoints</button><script src="/author.js"></script>',
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
  // No coverage call: script authority must exist before the lazy manager.
  session = await page.createDebuggerSession({ timeout: 15_000 })
  const opened = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(opened.nativeDeveloperDebuggerBreakpoints, 0)
  const seedWait = session.waitForPause({ timeout: 15_000 })
  const seedObserved = tracked(seedWait)
  const seedAuthor = tracked(page.evaluate('actualBreakpointSeed()'))
  const seed = await seedWait
  value(await seedObserved)
  const actualFrame = seed.callFrames[0]
  assert.equal(actualFrame?.functionName, 'actualBreakpointSeed')
  assert(actualFrame)
  const scriptId = actualFrame.location.scriptId
  assert.equal(await session.scriptSource(scriptId), authorSource)
  checks.push(
    'Fresh no-coverage page: genuine served author script ID, paused frame and exact source bytes',
  )

  let breakpoint = await session.setBreakpoint({ scriptId, lineNumber: 2, columnNumber: 0 })
  assert.equal(breakpoint.location.scriptId, scriptId)
  assert.equal(breakpoint.location.lineNumber, 2)
  assert(Object.isFrozen(breakpoint) && Object.isFrozen(breakpoint.location))
  const installed = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(installed.nativeDeveloperDebuggerBreakpoints, 1)
  assert.equal(installed.nativeDeveloperDebuggerProcessBreakpoints, 1)
  await session.resume(seed)
  assert.equal(value(await seedAuthor), 1)
  checks.push(
    'Actual current-script setBreakpoint resolves its real location and retains one native ID',
  )

  const firstWait = session.waitForPause({ timeout: 15_000 })
  const firstObserved = tracked(firstWait)
  const firstAuthor = tracked(page.evaluate('actualBreakpointTarget(17)'))
  const first = await firstWait
  value(await firstObserved)
  assert.equal(first.callFrames[0]?.functionName, 'actualBreakpointTarget')
  assert.equal(first.callFrames[0]?.location.scriptId, scriptId)
  assert.equal(first.callFrames[0]?.location.lineNumber, breakpoint.location.lineNumber)
  assert(first.hitBreakpoints.length > 0)
  assert.equal(stateSchema.parse(await source.runAutomation('diagnostics')).nativeDebuggerPauses, 1)
  checks.push(
    'Real author execution hits the installed breakpoint with actual frame, location and hit ID receipts',
  )

  await session.removeBreakpoint(breakpoint)
  const removed = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(removed.nativeDeveloperDebuggerBreakpoints, 0)
  assert.equal(removed.nativeDebuggerPauses, 1)
  await assert.rejects(session.removeBreakpoint(breakpoint), /stale/iu)
  await assert.rejects(session.removeBreakpoint({ ...breakpoint }), /another session/iu)
  await session.resume(first)
  assert.equal(value(await firstAuthor), 17)
  assert.equal(await page.evaluate('actualBreakpointTarget(5)'), 22)
  checks.push(
    'Real remove ACK retires only its ID while preserving the pause; consumed/forged handles reject and later author execution proceeds',
  )

  breakpoint = await session.setBreakpoint({ scriptId, lineNumber: 2 })
  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  const coveredWait = session.waitForPause({ timeout: 15_000 })
  const coveredObserved = tracked(coveredWait)
  const coveredAuthor = tracked(page.evaluate('actualBreakpointTarget(3)'))
  const coveredPause = await coveredWait
  value(await coveredObserved)
  const coverage = await page.coverage.stopJSCoverage()
  assert(coverage.some((entry) => entry.source === authorSource))
  const covered = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(covered.nativeDeveloperDebuggerBreakpoints, 1)
  assert.equal(covered.nativeDebuggerPauses, 1)
  await session.resume(coveredPause)
  assert.equal(value(await coveredAuthor), 25)
  checks.push(
    'Actual coverage start/stop retains author source and the independently owned installed breakpoint and pause',
  )

  await session.close()
  session = undefined
  const cleaned = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerBreakpoints === 0 &&
      state.nativeDeveloperDebuggerProcessBreakpoints === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0,
    'Actual breakpoint removal and session release did not settle',
  )
  assert.equal(cleaned.nativeDebuggerLeases, opened.nativeDebuggerLeases - 1)
  assert.equal(await page.evaluate('actualBreakpointTarget(4)'), 29)
  transfer = await page.evaluateHandle('globalThis.actualTransfer')
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops'), 1)
  checks.push(
    'Closing with an installed ID removes it before developer release; actual factory lease and opaque DataTransfer remain usable',
  )

  const owner = new AbortController(),
    reason = new Error('actual breakpoint constructor owner canceled')
  session = await createNativePage(source, owner.signal).createDebuggerSession({ timeout: 0 })
  await session.setBreakpoint({ scriptId, lineNumber: 2 })
  const canceledWait = session.waitForPause({ timeout: 15_000 })
  const canceledObserved = tracked(canceledWait)
  const canceledAuthor = tracked(page.evaluate('actualBreakpointTarget(8)'))
  const canceledPause = await canceledWait
  value(await canceledObserved)
  assert.equal(canceledPause.callFrames[0]?.functionName, 'actualBreakpointTarget')
  owner.abort(reason)
  await assert.rejects(session.pause(), (error) => error === reason)
  assert.equal(value(await canceledAuthor), 37)
  // Before public close, only the constructor owner hook can admit cleanup.
  const afterAbort = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerBreakpoints === 0 &&
      state.nativeDeveloperDebuggerProcessBreakpoints === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0,
    'Actual constructor-abort breakpoint removal/resume did not settle',
  )
  await session.close()
  session = undefined
  assert.equal(afterAbort.nativeDebuggerLeases, cleaned.nativeDebuggerLeases)
  checks.push(
    'Constructor abort while actually paused removes the real ID and resumes author effect37 before public close joins cleanup',
  )

  await transfer.dispose()
  transfer = undefined
  const beforeClose = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(beforeClose.activeHandles, 0)
  await source.close()
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  checks.push(
    'Actual helper exits cleanly after zero breakpoint/session ownership and disposed opaque user handles',
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
        installed,
        removed,
        covered,
        cleaned,
        afterAbort,
        beforeClose,
        actualProcessExit: source.processExit?.(),
        scope:
          'actual current root document exact script breakpoints only; no conditional/URL/persistent/child/worker breakpoint claim; held set-ACK cancellation remains controlled native proof',
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
console.info('Native current-root developer breakpoints actual8 passed')
