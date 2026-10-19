// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

/** @type {typeof import('/workspace/suitecut/src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Explicit immutable SDK selected and hashed alongside the report.
const native = await import(
  pathToFileURL(process.env.SUITECUT_NATIVE_SDK_DIRECTORY + '/native.js').href
)
/** @type {{stderr:string,deadlines:import('/workspace/suitecut/src/untrusted.js').UntrustedInput[],successes:import('/workspace/suitecut/src/untrusted.js').UntrustedInput[],cancellations:import('/workspace/suitecut/src/untrusted.js').UntrustedInput[],final?:import('/workspace/suitecut/src/untrusted.js').UntrustedInput,exit?:{code:number|null,signal:NodeJS.Signals|null},error?:string,stack?:string|undefined,cleanup?:string,shutdown?:string}} */
const report = { stderr: '', deadlines: [], successes: [], cancellations: [] }
const source = await native.launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 240,
  height: 180,
  onDiagnostic: (value) => (report.stderr += value),
})
const browser = new native.NativeBrowserContexts(source)
const root = native.createNativePage(source)
const context = await browser.newContext()
/**
 * Calls the genuine context transport with its actual source receiver.
 * @type {NonNullable<import('/workspace/suitecut/src/native-browser.js').NativeBrowser['runContext']>}
 */
const runContext = (action, params, signal) => {
  if (!source.runContext) throw new Error('Native context command transport is unavailable')
  return source.runContext(action, params, signal)
}
const counts = z.object({
  contextAuthenticationUpdates: z.number(),
  contextAuthenticationPhysicalFences: z.number(),
  contextAuthenticationTargets: z.number(),
  contextAuthenticationReceiptOwner: z.number(),
  contextAuthenticationReceiptDeadline: z.number(),
  contextAuthenticationReceiptAt: z.number(),
})
const diagnostics = async () => counts.parse(await source.runAutomation('diagnostics', {}))
const settle = async () => {
  for (let sample = 0; sample < 100; ++sample) {
    const state = await diagnostics()
    if (!state.contextAuthenticationUpdates && !state.contextAuthenticationPhysicalFences)
      return state
    await root.waitForTimeout(10)
  }
  throw new Error('Actual native authentication receipts did not settle')
}
try {
  for (let index = 0; index < 6; ++index) await context.newPage()
  const id = context.id
  for (let index = 0; index < 3; ++index) {
    const started = performance.now()
    const result = runContext('setHTTPCredentials', {
      contextId: id,
      credentials: { username: 'deadline-' + index, password: 'actual' },
      timeoutMs: 1,
    }).then(
      () => ({ fulfilled: true }),
      (error) => ({
        fulfilled: false,
        name: error instanceof Error ? error.name : '',
        message: String(error),
      }),
    )
    const during = await diagnostics()
    const outcome = await result
    const settled = await settle()
    report.deadlines.push({ during, outcome, elapsedMs: performance.now() - started, settled })
    if (outcome.fulfilled) {
      assert.ok(settled.contextAuthenticationReceiptOwner > 0)
      assert.ok(settled.contextAuthenticationReceiptDeadline > 0)
      assert.ok(
        settled.contextAuthenticationReceiptAt < settled.contextAuthenticationReceiptDeadline,
      )
    }
    if (
      'message' in outcome &&
      typeof outcome.message === 'string' &&
      /timed out|deadline exceeded/u.test(outcome.message)
    )
      assert.equal(outcome.name, 'TimeoutError')
  }
  await runContext('setHTTPCredentials', {
    contextId: id,
    credentials: { username: 'actual-predeadline', password: 'accepted' },
    timeoutMs: 5000,
  })
  const accepted = await settle()
  assert.ok(accepted.contextAuthenticationReceiptOwner > 0)
  assert.ok(accepted.contextAuthenticationReceiptDeadline > 0)
  assert.ok(accepted.contextAuthenticationReceiptAt < accepted.contextAuthenticationReceiptDeadline)
  report.successes.push(accepted)
  for (let index = 0; index < 5; ++index) {
    const controller = new AbortController()
    const result = runContext(
      'setHTTPCredentials',
      {
        contextId: id,
        credentials: { username: 'canceled-' + index, password: 'actual' },
        timeoutMs: 5000,
      },
      controller.signal,
    ).then(
      () => ({ fulfilled: true }),
      (error) => ({
        fulfilled: false,
        name: error instanceof Error ? error.name : '',
        message: String(error),
      }),
    )
    const during = await diagnostics()
    controller.abort(new Error('actual auth update cancellation probe'))
    const outcome = await result
    const afterCancel = await diagnostics()
    report.cancellations.push({ during, outcome, afterCancel, settled: await settle() })
  }
  await context.close()
  report.final = await diagnostics()
  await source.close()
  report.exit = source.processExit()
  assert.equal(report.exit?.code, 0)
  assert.equal(report.stderr, '')
} catch (error) {
  report.error = String(error)
  report.stack = error instanceof Error ? error.stack : undefined
  process.exitCode = 1
} finally {
  await context.close().catch((error) => {
    report.cleanup = String(error)
    process.exitCode = 1
  })
  await source.close().catch((error) => {
    report.shutdown = String(error)
    process.exitCode = 1
  })
  await writeFile(
    z.string().parse(process.env.SUITECUT_NATIVE_AUTH_DEADLINE_REPORT),
    JSON.stringify(report, null, 2),
  )
  console.log(JSON.stringify(report, null, 2))
}
