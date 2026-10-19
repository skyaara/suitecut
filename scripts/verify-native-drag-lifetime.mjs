// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-drag-lifetime',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
/** @type {unknown[]} */
const errors = []
/** @type {unknown[]} */
const observations = []
/** @type {string[]} */
const checks = []
/** @type {Awaited<ReturnType<typeof launchNativeBrowser>> | undefined} */
let source
/** @type {(() => void) | undefined} */
let enteredResolve
const entered = new Promise((resolve) => {
  enteredResolve = () => resolve(undefined)
})
const fixture = `<!DOCTYPE html><div id=source draggable=true style="position:fixed;left:10px;top:20px;width:100px;height:80px;background:blue">Source</div><div id=target style="position:fixed;left:240px;top:180px;width:100px;height:80px;background:gray">Target</div><button id=healthy style="position:fixed;left:480px;top:30px">Healthy</button><script>
globalThis.events=[];globalThis.holdNextOver=false;globalThis.realDrop=null;globalThis.healthy=[];
for(const type of ['dragstart','dragenter','dragover','drop','dragend'])document.addEventListener(type,event=>events.push({type:event.type,trusted:event.isTrusted}),true);
document.querySelector('#source').addEventListener('dragstart',event=>{event.dataTransfer.effectAllowed='copy';event.dataTransfer.setData('text/plain','Actual held Drop bytes')});
document.querySelector('#target').addEventListener('dragover',event=>{event.dataTransfer.dropEffect='copy';event.preventDefault();if(holdNextOver){holdNextOver=false;navigator.sendBeacon('/entered','actual trusted dragover');const started=performance.now();while(performance.now()-started<1200){}}});
document.querySelector('#target').addEventListener('drop',event=>{event.preventDefault();realDrop=event.dataTransfer.getData('text/plain')});
document.querySelector('#healthy').addEventListener('click',event=>healthy.push(event.isTrusted));
</script>`
const server = createServer((request, response) => {
  request.resume()
  if (request.url === '/entered' && request.method === 'POST') {
    enteredResolve?.()
    response.writeHead(204).end()
    return
  }
  if (request.url === '/') response.writeHead(200, { 'content-type': 'text/html' }).end(fixture)
  else response.writeHead(404).end()
})
const ownership = z.object({
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  inputWork: z.number(),
  formClicks: z.number(),
  mouseButtons: z.number(),
  pressedKeys: z.number(),
  nativeDragOperations: z.number(),
  nativeManualDragActive: z.boolean(),
  pendingDevTools: z.number(),
  nativeDragCompletionRecords: z.number(),
  nativeDragCompletionCallbacks: z.number(),
  nativeDragCompletionClosing: z.number(),
  nativeDragCompletionAdmissionFailed: z.boolean(),
  nativeDragGlobalRetainedRecords: z.number(),
  nativeDragGlobalRetainedBytes: z.number(),
})
/**
 * This is a proof failure bound, never a native receipt/retirement mechanism.
 * @template T
 * @param {Promise<T>} promise
 * @param {number} milliseconds
 * @param {string} label
 */
async function bounded(promise, milliseconds, label) {
  /** @type {ReturnType<typeof setTimeout> | undefined} */
  let timer
  try {
    /** @type {Promise<never>} */
    const deadline = new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(label)), milliseconds)
    })
    return await Promise.race([promise, deadline])
  } finally {
    if (timer) clearTimeout(timer)
  }
}
try {
  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(0, '127.0.0.1', () => resolve(undefined))
  })
  const address = z.object({ port: z.number() }).parse(server.address())
  source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    width: 640,
    height: 420,
    onDiagnostic: diagnostics.observe,
  })
  const page = createNativePage(source)
  page.setDefaultTimeout(5000)
  await page.goto(`http://127.0.0.1:${address.port}/`)
  await page.hover('#source')
  await page.mouse.down()
  await page.hover('#target')
  await page.evaluate('globalThis.holdNextOver=true;void 0')
  const controller = new AbortController()
  const reason = new Error('Original caller cancellation while real Drop is pending')
  const completion = page
    .runAutomation(
      'input',
      { kind: 'mouse', action: 'up', button: 'left', clickCount: 1, timeoutMs: 5000 },
      controller.signal,
    )
    .then(
      () => ({ succeeded: true, error: undefined }),
      (error) => ({ succeeded: false, error: /** @type {unknown} */ (error) }),
    )
  await bounded(entered, 3000, 'Actual trusted dragover beacon was not observed')
  const pending = ownership.parse(await page.runAutomation('diagnostics'))
  observations.push({ phase: 'real trusted handler entered; before caller abort', state: pending })
  assert.equal(pending.nativeDragCompletionRecords, 1)
  assert.equal(pending.nativeDragCompletionCallbacks, 1)
  assert.equal(pending.nativeDragGlobalRetainedRecords, 1)
  assert(pending.nativeDragGlobalRetainedBytes > 0)
  checks.push(
    'Actual trusted dragover wire beacon and native registry show one genuine pending Drop receipt before cancellation',
  )
  controller.abort(reason)
  const outcome = await bounded(completion, 1000, 'Public abort awaited the held native Drop')
  assert.equal(outcome.succeeded, false)
  assert.equal(outcome.error, reason)
  const cancellationDeadline = performance.now() + 1000
  let canceled
  for (;;) {
    canceled = ownership.parse(await page.runAutomation('diagnostics'))
    assert.equal(
      canceled.nativeDragCompletionRecords,
      1,
      'No actual pending/canceled overlap was observed',
    )
    assert.equal(
      canceled.nativeDragCompletionCallbacks,
      1,
      'Drop callback disappeared during logical cancellation',
    )
    if (canceled.inputWork === 0) break
    assert(
      performance.now() < cancellationDeadline,
      'Native logical cancellation was not observed before actual receipt',
    )
    await setImmediate()
  }
  observations.push({
    phase: 'original public reason rejected; native cancellation observed',
    state: canceled,
  })
  checks.push(
    'Original caller error identity rejects while the actual Drop callback, payload and native budget remain retained',
  )
  const retirementDeadline = performance.now() + 5000
  let state
  for (;;) {
    state = ownership.parse(await page.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0 || value === false)) break
    assert(
      performance.now() < retirementDeadline,
      'Actual late Drop/cleanup receipts did not settle',
    )
    await setImmediate()
  }
  observations.push({ phase: 'actual late Drop and disable ACKs retired', state })
  const events = z
    .array(z.object({ type: z.string(), trusted: z.boolean() }))
    .parse(await page.evaluate('events'))
  assert(events.every((event) => event.trusted))
  assert.equal(events.filter((event) => event.type === 'drop').length, 1)
  assert.equal(events.filter((event) => event.type === 'dragend').length, 1)
  assert.equal(await page.evaluate('realDrop'), 'Actual held Drop bytes')
  await page.click('#healthy')
  assert.deepEqual(await page.evaluate('healthy'), [true])
  assert.equal(
    z.array(z.unknown()).parse(await page.evaluate('events.filter(event=>event.type==="dragend")'))
      .length,
    1,
  )
  checks.push(
    'Real late ACK cleanup clears only the old intercepted data, emits one source-end and permits a later genuine trusted click',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        checks,
        observations,
        events,
        state,
        publicReasonIdentityPreserved: true,
        limits:
          'One bounded 1200ms real authored handler; no synthetic events/ACKs or physical retirement timer; ordinary protocol ACKs do not prove original-widget delivery',
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, observations, error: String(error) }, null, 2) +
      '\n',
  )
} finally {
  server.closeAllConnections()
  try {
    await new Promise((resolve, reject) =>
      server.close((error) => (error ? reject(error) : resolve(undefined))),
    )
  } catch (error) {
    errors.push(error)
  }
  try {
    if (source) await finishNativeProof(source, diagnostics, directory, errors)
    else await diagnostics.finish()
  } catch (error) {
    errors.push(error)
  }
  if (source) {
    try {
      const actualExit = source.processExit?.()
      await writeFile(
        resolve(directory, 'native-process-exit.json'),
        JSON.stringify({ observed: actualExit !== undefined, actualExit }, null, 2) + '\n',
      )
      assert(actualExit, 'No actual native process/stdio closure receipt was observed')
      assert.equal(actualExit.code, 0)
      assert.equal(actualExit.signal, null)
    } catch (error) {
      errors.push(error)
    }
  }
}
if (errors.length) {
  for (const name of ['report.json', 'failure.json']) {
    const file = resolve(directory, name)
    try {
      const report = z
        .record(z.string(), z.unknown())
        .parse(JSON.parse(await readFile(file, 'utf8')))
      report.completed = false
      report.failures = errors.map(String)
      await writeFile(file, JSON.stringify(report, null, 2) + '\n')
    } catch (error) {
      if (!(error instanceof Error && 'code' in error && error.code === 'ENOENT'))
        errors.push(error)
    }
  }
  throw new AggregateError(
    errors,
    'Actual held Drop proof failed; retained evidence is authoritative',
  )
}
console.info('Native held Drop lifetime3 passed')
