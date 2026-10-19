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

import { createNativePage, launchNativeBrowser, NativeFrame } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-frame-debugger',
)
await mkdir(directory, { recursive: true })
const diagnostic = await captureNativeStderr(directory)
const authorSource = `globalThis.actualFrameEffect=0;
function actualFrameDebug(seed){
  const actualLocal=seed;
  debugger;
  globalThis.actualFrameEffect=actualLocal+1;
  return globalThis.actualFrameEffect;
}
`
const server = createServer((request, response) => {
  const address = server.address()
  assert(address && typeof address !== 'string')
  if (request.url?.startsWith('/author.js?')) {
    response.setHeader('Content-Type', 'text/javascript; charset=utf-8')
    response.end(authorSource)
  } else {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    const children =
      request.url === '/root'
        ? `<iframe name="same" src="/same"></iframe><iframe name="remote" src="http://localhost:${address.port}/remote"></iframe>`
        : ''
    const role =
      request.url === '/root' ? 'root' : request.url?.startsWith('/remote') ? 'remote' : 'same'
    response.end(
      `<!doctype html><body>Actual frame debugger<script src="/author.js?scope=${role}"></script>${children}</body>`,
    )
  }
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`,
  remoteOrigin = `http://localhost:${address.port}`
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
  nativeDebuggerPauses: z.number().int().nonnegative(),
  nativeDebuggerLeases: z.number().int().nonnegative(),
  nativeFrameDeveloperDebuggerLive: z.number().int().nonnegative(),
  nativeFrameDeveloperDebuggerRecords: z.number().int().nonnegative(),
  activeHandles: z.number().int().nonnegative(),
})
/**
 * Observe actual native ownership without manufacturing a receipt.
 * @param {(state:z.output<typeof stateSchema>)=>boolean} predicate
 */
async function observed(predicate) {
  const end = performance.now() + 15_000
  while (true) {
    const state = stateSchema.parse(await source.runAutomation('diagnostics'))
    if (predicate(state)) return state
    assert(performance.now() < end, 'Actual native frame debugger ownership did not settle')
  }
}
/** @type {Promise<unknown>[]} */ const drains = []
/**
 * Observe each authored promise immediately and retain its original outcome.
 * @param {Promise<unknown>} promise
 */
function tracked(promise) {
  const observed = promise.then(
    (value) => ({ value }),
    (/** @type {unknown} */ error) => ({ error }),
  )
  drains.push(observed)
  return observed
}
/**
 * Return the real author result or propagate its original observed error.
 * @param {Awaited<ReturnType<typeof tracked>>} result
 */
function value(result) {
  if ('error' in result) throw result.error
  return result.value
}
/** @type {Set<import('../dist/native.js').NativeDebuggerSession>} */ const sessions = new Set()
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
try {
  await page.goto(origin + '/root')
  const same = page.frame('same'),
    remote = page.frame('remote')
  assert(same && remote)
  assert.equal(same.url(), origin + '/same')
  assert.equal(remote.url(), remoteOrigin + '/remote')
  const targetReceipt = z
    .object({
      targetInfos: z.array(
        z.object({ type: z.string(), url: z.string(), targetId: z.string().min(1) }),
      ),
    })
    .parse(await page.sendDevToolsCommand('Target.getTargets'))
  const remoteTarget = targetReceipt.targetInfos.find(
    (target) => target.type === 'iframe' && target.url === remoteOrigin + '/remote',
  )
  assert(remoteTarget, 'Actual OOPIF target receipt is required')
  assert(
    !targetReceipt.targetInfos.some(
      (target) => target.type === 'iframe' && target.url === origin + '/same',
    ),
  )
  const root = await page.createDebuggerSession()
  sessions.add(root)
  let rootScripts = await root.scripts()
  let rootScript = rootScripts.scripts.find(
    (script) => script.url === origin + '/author.js?scope=root',
  )
  while (!rootScript && rootScripts.next) {
    rootScripts = await root.scripts(rootScripts.next)
    rootScript = rootScripts.scripts.find(
      (script) => script.url === origin + '/author.js?scope=root',
    )
  }
  assert(rootScript)
  assert.equal(await root.scriptSource(rootScript.scriptId), authorSource)
  await assert.rejects(
    same.createDebuggerSession(),
    /channel.*owned|channel.*cleaning|physically|already/iu,
  )
  const rootOpened = stateSchema.parse(await source.runAutomation('diagnostics'))
  await root.close()
  sessions.delete(root)
  checks.push(
    'Fresh root inventory without coverage, actual same-process and OOPIF target receipts, same-process child refuses the live root channel',
  )

  let session = await same.createDebuggerSession()
  sessions.add(session)
  const firstWait = session.waitForPause(),
    firstWaitObserved = tracked(firstWait),
    firstAuthor = tracked(same.evaluate('actualFrameDebug(5)'))
  const first = await firstWait
  value(await firstWaitObserved)
  const frame = first.callFrames[0]
  assert(frame)
  assert.equal(frame.functionName, 'actualFrameDebug')
  assert.equal(await session.scriptSource(frame.location.scriptId), authorSource)
  assert.deepEqual(await session.evaluateOnCallFrame(first, frame, 'actualLocal'), {
    kind: 'value',
    value: 5,
  })
  await assert.rejects(session.pause(), /Agent-wide pause and step/iu)
  for (const action of ['stepOver', 'stepInto', 'stepOut']) {
    assert(action === 'stepOver' || action === 'stepInto' || action === 'stepOut')
    await assert.rejects(session[action](first), /Agent-wide pause and step/iu)
  }
  await assert.rejects(session.scriptSource(rootScript.scriptId), /current.*script|identity/iu)
  await assert.rejects(
    session.setBreakpoint({ scriptId: rootScript.scriptId, lineNumber: 3 }),
    /script|identity|document/iu,
  )
  await session.resume(first)
  assert.equal(value(await firstAuthor), 6)
  checks.push(
    'Child owns its real author callframe/source/local; foreign root script IDs reject, unsupported pause/step preserve the actual usable pause',
  )

  const foreignWait = session.waitForPause({ timeout: 0 }),
    foreignObserved = tracked(foreignWait)
  let foreignDelivered = false
  void foreignObserved.then(() => {
    foreignDelivered = true
  })
  assert.equal(value(await tracked(page.evaluate('actualFrameDebug(2)'))), 3)
  assert.equal(foreignDelivered, false)
  const nextAuthor = tracked(same.evaluate('actualFrameDebug(7)')),
    next = await foreignWait
  value(await foreignObserved)
  assert.equal(next.callFrames[0]?.location.scriptId, frame.location.scriptId)
  await session.resume(next)
  assert.equal(value(await nextAuthor), 8)
  checks.push(
    'Foreign root author pause auto-resumes without lending its IDs; the original child waiter later receives only the genuine child pause',
  )

  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  const coveredWait = session.waitForPause(),
    coveredObserved = tracked(coveredWait),
    coveredAuthor = tracked(same.evaluate('actualFrameDebug(10)'))
  const covered = await coveredWait
  value(await coveredObserved)
  const coverage = await page.coverage.stopJSCoverage()
  assert(
    coverage.some((entry) =>
      entry.functions.some(
        (fn) =>
          fn.functionName === 'actualFrameDebug' && fn.ranges.some((range) => range.count > 0),
      ),
    ),
  )
  const duringCoverage = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(duringCoverage.nativeDebuggerPauses, 1)
  await session.resume(covered)
  assert.equal(value(await coveredAuthor), 11)
  checks.push(
    'Real child execution counts and coverage stop preserve the sustained developer pause and native bootstrap Debugger ownership',
  )

  await same.goto(origin + '/same?replacement=1')
  await assert.rejects(
    session.scriptSource(frame.location.scriptId),
    /changed|closed|closing|unavailable/iu,
  )
  await session.close()
  sessions.delete(session)
  session = await same.createDebuggerSession()
  sessions.add(session)
  await assert.rejects(session.resume(first), /stale|another session/iu)
  await session.close()
  sessions.delete(session)
  checks.push(
    'Actual child navigation retires the old captured owner; exact old close joins real retirement and the new owner cannot use its old pause',
  )

  const independentRoot = await page.createDebuggerSession()
  sessions.add(independentRoot)
  const remoteSession = await remote.createDebuggerSession()
  sessions.add(remoteSession)
  const remoteWait = remoteSession.waitForPause(),
    remoteObserved = tracked(remoteWait),
    remoteAuthor = tracked(remote.evaluate('actualFrameDebug(9)'))
  const remotePause = await remoteWait
  value(await remoteObserved)
  const remoteFrame = remotePause.callFrames[0]
  assert(remoteFrame)
  assert.equal(await page.evaluate('actualFrameEffect'), 3)
  assert.deepEqual(
    await remoteSession.evaluateOnCallFrame(remotePause, remoteFrame, 'actualLocal'),
    { kind: 'value', value: 9 },
  )
  await assert.rejects(
    remote.sendDevToolsCommand('Debugger.disable'),
    /Debugger.*(?:lease|owner|conflict)|(?:lease|owner|conflict).*Debugger/iu,
  )
  await remoteSession.resume(remotePause)
  assert.equal(value(await remoteAuthor), 10)
  await remoteSession.close()
  sessions.delete(remoteSession)
  await independentRoot.close()
  sessions.delete(independentRoot)
  checks.push(
    'Actual OOPIF session coexists with root control; its own pause/evaluation and raw conflict leave the independent root agent responsive',
  )

  const info = source.frameInfos?.().find((info) => info.id === same.identity())
  assert(info)
  const constructorOwner = new AbortController(),
    ownedFrame = new NativeFrame(page, info, constructorOwner.signal)
  const owned = await ownedFrame.createDebuggerSession()
  sessions.add(owned)
  const ownedWait = owned.waitForPause(),
    ownedObserved = tracked(ownedWait),
    ownedAuthor = tracked(same.evaluate('actualFrameDebug(20)'))
  await ownedWait
  value(await ownedObserved)
  const reason = new Error('actual frame constructor lifetime canceled')
  constructorOwner.abort(reason)
  await assert.rejects(owned.scripts(), (error) => error === reason)
  await owned.close()
  sessions.delete(owned)
  assert.equal(value(await ownedAuthor), 21)
  checks.push(
    'Post-open frame constructor abort retains canonical old-frame cleanup through real resume/retirement; the actual author effect completes21',
  )

  const finalState = await observed(
    (state) =>
      state.nativeDeveloperDebuggerSessions === 0 &&
      state.nativeDeveloperDebuggerWaiters === 0 &&
      state.nativeDeveloperDebuggerPhysical === 0 &&
      state.nativeDebuggerDeveloperReceipts === 0 &&
      state.nativeDebuggerPauses === 0 &&
      state.nativeFrameDeveloperDebuggerLive === 0,
  )
  assert(finalState.nativeFrameDeveloperDebuggerRecords <= 32)
  assert.equal(finalState.activeHandles, 0)
  assert.equal(finalState.nativeDebuggerLeases, rootOpened.nativeDebuggerLeases - 1)
  await source.close()
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  checks.push(
    'Actual native ownership settles, bounded completed records remain metadata only, and the real helper exits0 without native diagnostics',
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
        targetReceipt,
        remoteTarget,
        rootOpened,
        finalState,
        actualProcessExit: source.processExit?.(),
        scope:
          'Actual current same-process child and OOPIF document control; no child agent-wide pause/step, mixed-ancestor, worker/broker or renderer-recovery claim',
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
    for (const session of sessions)
      if (source.state !== 'closed')
        await session.close({ timeout: 15_000 }).catch((error) => errors.push(error))
    await finishNativeProof(source, diagnostic, directory, errors)
    await Promise.all(drains)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native actual captured frame debugger8 passed')
