// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as deadlineTimer } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PUBLIC_CRASH_DIRECTORY ?? '.suitecut/native-public-crash-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Observes actual failure/closure under a bounded verifier owner deadline.
 * @template T
 * @param {Promise<T>} operation
 * @returns {Promise<T>}
 */
async function bounded(operation) {
  const cancel = new AbortController()
  try {
    return await Promise.race([
      operation,
      deadlineTimer(10000, undefined, { signal: cancel.signal }).then(() => {
        throw new Error('Timeout: actual native crash proof operation did not settle')
      }),
    ])
  } finally {
    cancel.abort()
  }
}
const nativeStderr = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  onDiagnostic: nativeStderr.observe,
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat: 'i420',
  headless: true,
})
if (!source.runContext || !source.pageSource || !source.onContextEvent)
  throw new Error('Real native context transport unavailable')
const contexts = new NativeBrowserContexts(
  Object.assign(source, {
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  }),
)
const behaviorErrors = z.array(z.unknown()).parse([])
try {
  const context = await contexts.newContext()
  const page = await context.newPage()
  await page.setContent('<title>Actual crashed child</title><body>native crash target</body>')
  const calls = z.array(z.string()).parse([])

  /**
   * Verifies actual page identity for every duplicate crash listener.
   * @param {import('../dist/native-page.js').NativePage} actual
   */
  const duplicate = (actual) => {
    assert.equal(actual, page)
    calls.push('duplicate')
  }
  page
    .on('crash', duplicate)
    .on('crash', duplicate)
    .once('crash', (actual) => {
      assert.equal(actual, page)
      calls.push('once')
    })
  page.prependListener('crash', (actual) => {
    assert.equal(actual, page)
    calls.push('first')
  })
  page.off('crash', duplicate)
  const plain = page.waitForEvent('crash', { timeout: 5000 })
  const predicate = page.waitForEvent('crash', {
    timeout: 5000,
    predicate: async (actual) => await Promise.resolve(actual === page),
  })
  const started = performance.now()
  const refused = page
    .waitForEvent('crash', { timeout: 300, predicate: () => false })
    .catch((error) => {
      assert(error instanceof Error)
      return error
    })
  await page.source.runAutomation('diagnostics')
  const crash = page.source.sendDevToolsCommand('Page.crash').catch((error) => {
    assert(error instanceof Error)
    return error.message
  })
  assert.equal(await bounded(plain), page)
  assert.equal(await bounded(predicate), page)
  const refusedError = await bounded(refused)
  assert(refusedError instanceof Error)
  assert.match(refusedError.message, /timed out|Timeout/iu)
  const refusalDurationMs = performance.now() - started
  assert(refusalDurationMs < 1500, 'Rejected host predicate extended its original native deadline')
  assert.deepEqual(calls, ['first', 'duplicate', 'once'])
  assert.equal(page.isClosed(), false)
  assert.equal(page.source.state, 'failed')
  await assert.rejects(page.evaluate('40+2'), /renderer|terminated|crash/iu)
  const eventsAfterCrash = calls.length
  const later = () => {
    calls.push('fabricated')
  }
  page.on('crash', later)
  await assert.rejects(page.waitForEvent('crash', { timeout: 60 }), /timed out|Timeout/iu)
  assert.equal(calls.length, eventsAfterCrash, 'Late registration replayed an old crash event')
  page.off('crash', later)
  const healthy = z
    .object({ result: z.object({ value: z.number() }) })
    .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
  assert.equal(healthy.result.value, 42)
  await bounded(context.close())
  assert.equal(page.isClosed(), true)
  assert.equal(source.state, 'running')
  const report = {
    completed: true,
    binarySHA256: createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    actualChildPageId: page.source.pageId,
    nativeCrashReply: await bounded(crash),
    actualListeners: calls,
    plainWaitAndAsyncPredicateResolved: true,
    rejectedPredicateError: refusedError.message,
    refusalDurationMs,
    lateWaitTimedOutWithoutReplay: true,
    pageRemainedOpenUntilActualContextClose: true,
    healthyRootActualValue: healthy.result.value,
    reloadRecoveryImplemented: false,
  }
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(
    'Actual native public crash listeners, cursor/deadline waits and physical closure proof passed',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, error: error instanceof Error ? error.stack : 'Non-Error failure' },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
}
