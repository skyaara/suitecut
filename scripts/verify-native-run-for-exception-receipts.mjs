// SPDX-License-Identifier: MIT
// Actual native budget receipts. No replacement clock, browser driver or sleeps.
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
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html')
  response.end('<!doctype html><title>Native runFor exception receipts</title>')
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
  {
    const before = z.number().parse(await page.evaluate(() => Date.now()))
    const observed = page.waitForEvent('pageerror', {
      timeout: 3000,
      predicate: (error) => error.message === 'Actual runFor timer100 failure',
    })
    await page.evaluate(`globalThis.actualRunForCalls = [];
      setTimeout(() => { throw new Error('Actual runFor timer100 failure') }, 100);
      setTimeout(() => { globalThis.actualRunForCalls.push(Date.now()) }, 120);`)
    const failed = clock.runFor(120).then(
      () => assert.fail('The observed synchronous exception must reject runFor'),
      async (error) => {
        assert(error instanceof Error)
        assert.match(error.message, /Actual runFor timer100 failure/u)
        // Inspect native state as soon as rejection is delivered, before waiting
        // for the separate SDK pageerror predicate or any later clock command.
        const status = await clock.status()
        assert.equal(status.lastAcknowledgedPaused, true)
        assert.equal(status.pendingPhysicalCommand, false)
        assert.equal(status.activeOwner, false)
        assert.equal(status.capabilities.nativeRunForExceptionReceipts, true)
        assert.deepEqual(await page.evaluate('actualRunForCalls'), [before + 120])
        assert.equal(await page.evaluate(() => Date.now()), before + 120)
      },
    )
    await Promise.all([failed, observed])
    checks.push({
      originalThrowingTimerAndLaterCallbackBehavior: true,
      actualPauseAcknowledgedAtRejectionObservation: true,
      nativeReceiptPolicyNotTimerExclusiveAttribution: true,
    })
  }
  {
    const first = page.waitForEvent('pageerror', {
      timeout: 3000,
      predicate: (error) => error.message === 'Actual first runFor error',
    })
    const second = page.waitForEvent('pageerror', {
      timeout: 3000,
      predicate: (error) => error.message === 'Actual second runFor error',
    })
    await page.evaluate(`globalThis.actualRunForLater = false;
      setTimeout(() => { throw new Error('Actual first runFor error') }, 10);
      setTimeout(() => { throw new Error('Actual second runFor error') }, 20);
      setTimeout(() => { globalThis.actualRunForLater = true }, 30);`)
    await Promise.all([
      assert.rejects(clock.runFor(30), /Actual first runFor error/u),
      first,
      second,
    ])
    assert.equal(await page.evaluate('actualRunForLater'), true)
    checks.push({
      firstActualReceiptWinsAndLaterCallbacksContinue: true,
      separateNativePageErrorsRetained: 2,
    })
  }
  {
    const rejected = page.waitForEvent('pageerror', {
      timeout: 3000,
      predicate: (error) => error.message === 'Actual excluded promise rejection',
    })
    await page.evaluate(
      `setTimeout(() => { void Promise.reject(new Error('Actual excluded promise rejection')) }, 10)`,
    )
    await clock.runFor(20)
    await rejected
    checks.push({ nativePromiseRejectionRemainsSeparatePageError: true })
  }
  {
    // The captured sequence from the previous error is outside this next budget.
    await clock.runFor(10)
    const status = await clock.status()
    assert.equal(status.lastAcknowledgedPaused, true)
    assert.equal(status.activeOwner, false)
    assert.equal(status.pendingPhysicalCommand, false)
    checks.push({ nextBudgetDoesNotReplayOldExceptionReceipt: true })
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
        'Native runFor observed synchronous main-document exception receipts; not timer-exclusive attribution; promise rejection separate; original timer-error/later-timer behavior oracle; no zero/fractional raw budgets',
    },
    null,
    2,
  ),
)
console.log(`Actual native runFor: ${checks.length} groups passed`)
