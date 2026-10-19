// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  NativeWorkerControlSchema,
  NativeWorkerEventSchema,
} from '../dist/native-worker-protocol.js'
import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const cachePolicy = z
  .enum(['native-default', 'disabled'])
  .default('native-default')
  .parse(process.env.SUITECUT_NATIVE_WORKER_LIFETIME_CACHE_POLICY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_LIFETIME_DIRECTORY ??
    '.suitecut/native-worker-target-lifetime',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const cleanupFailures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const events = z.array(NativeWorkerEventSchema).parse([])
const receipts = z.array(z.object({ phase: z.string(), value: z.unknown() })).parse([])
const counters = z.object({
  nativeWorkers: z.number().int().nonnegative(),
  nativeWorkerOperations: z.number().int().nonnegative(),
  nativeWorkerHandles: z.number().int().nonnegative(),
  nativeWorkerPhysical: z.number().int().nonnegative(),
})
const targetList = z.object({
  targetInfos: z.array(z.object({ targetId: z.string(), type: z.string(), attached: z.boolean() })),
})
/** @type {Promise<void>[]} */
const settlements = []
/**
 * Observe the real operation's rejection immediately while retaining its result.
 * @template T
 * @param {Promise<T>} operation Actual native operation.
 * @returns {Promise<T>} The original operation.
 */
function observe(operation) {
  settlements.push(
    operation.then(
      () => undefined,
      (error) => {
        cleanupFailures.push(String(error))
      },
    ),
  )
  return operation
}
const server = createServer((request, response) => {
  const script = request.url === '/lifetime-worker.js'
  response.setHeader('content-type', script ? 'application/javascript' : 'text/html')
  response.end(
    script
      ? 'globalThis.actualLifetimeValue=23;postMessage(23);'
      : '<body>Actual worker target lifetime</body>',
  )
})
await new Promise((done) => server.listen(0, () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const crossOrigin = `http://localhost:${address.port}`
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
/** @type {(() => void) | undefined} */
let unsubscribe
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    ...(cachePolicy === 'disabled' ? { backForwardCache: false } : {}),
    onDiagnostic: (line) => diagnostics.push(line),
  })
  const actual = source
  const page = createNativePage(actual)
  unsubscribe = actual.onWorkerMessage?.((event) => {
    assert.ok(events.length < 128, 'Actual lifecycle receipt bound exceeded')
    events.push(NativeWorkerEventSchema.parse(event))
  })
  assert.ok(unsubscribe, 'Genuine native worker lifecycle subscription unavailable')
  const diagnosticsNow = async () => counters.parse(await actual.runAutomation('diagnostics'))
  const targetsNow = async () =>
    targetList.parse(await actual.sendDevToolsCommand('Target.getTargets')).targetInfos
  const createWorker = async () => {
    const created = observe(page.waitForEvent('worker', { timeout: 5000 }))
    assert.equal(
      await page.evaluate(
        'new Promise(resolve=>{globalThis.actualLifetimeWorker=new Worker("/lifetime-worker.js");actualLifetimeWorker.onmessage=event=>resolve(event.data)})',
      ),
      23,
    )
    const worker = await created
    assert.equal(await worker.evaluate('actualLifetimeValue'), 23)
    assert.equal(page.workers().includes(worker), true)
    return worker
  }
  await page.goto(origin + '/empty')
  const worker = await createWorker()
  const original = NativeWorkerControlSchema.parse(worker.info.control)
  assert.equal((await diagnosticsNow()).nativeWorkerOperations, 0)
  const waiting = observe(worker.waitForEvent('close', { timeout: 10000 }))
  const admitted = await diagnosticsNow()
  assert.equal(admitted.nativeWorkerOperations, 1, 'The real close wait was not admitted')
  receipts.push({ phase: 'accepted native waitClose before detach', value: admitted })
  await actual.sendDevToolsCommand('Target.setAutoAttach', {
    autoAttach: false,
    waitForDebuggerOnStart: false,
    flatten: true,
  })
  const detached = (await targetsNow()).find((target) => target.targetId === worker.info.id)
  assert.ok(detached)
  assert.equal(detached.type, 'worker')
  assert.equal(detached.attached, false)
  assert.equal(worker.isClosed(), false)
  assert.equal(page.workers().includes(worker), false)
  const channel = events.findLast(
    (event) => event.worker.id === worker.info.id && event.event === 'channelclosed',
  )
  assert.ok(channel)
  assert.deepEqual(channel.worker.control, original)
  assert.equal((await diagnosticsNow()).nativeWorkerOperations, 1)
  assert.equal(
    events.some((event) => event.worker.id === worker.info.id && event.event === 'close'),
    false,
  )
  receipts.push({
    phase: 'real detached target remains alive',
    value: { target: detached, channel },
  })
  checks.push('Actual Inspector detach preserves author target and the accepted native close wait')

  const attached = z.object({ sessionId: z.string().min(1) }).parse(
    await actual.sendDevToolsCommand('Target.attachToTarget', {
      targetId: worker.info.id,
      flatten: true,
    }),
  )
  const author = z.object({ result: z.object({ value: z.literal(23) }) }).parse(
    await actual.runAutomation('worker', {
      worker: worker.info.id,
      action: 'evaluate',
      expression: 'actualLifetimeValue',
      timeoutMs: 5000,
    }),
  )
  assert.equal(author.result.value, 23)
  const current = NativeWorkerControlSchema.parse(worker.info.control)
  assert.equal(current.owner, original.owner)
  assert.notEqual(current.generation, original.generation)
  assert.equal(
    page.workers().find((value) => value.info.id === worker.info.id),
    worker,
  )
  assert.equal(await worker.evaluate('actualLifetimeValue'), 23)
  receipts.push({ phase: 'real same-target reattachment', value: { attached, original, current } })
  checks.push(
    'A genuine new attachment recovers the same canonical worker and preserved author state',
  )

  await page.evaluate('actualLifetimeWorker.terminate()')
  assert.equal(await waiting, worker)
  assert.equal(worker.isClosed(), true)
  assert.equal(
    (await targetsNow()).some((target) => target.targetId === worker.info.id),
    false,
  )
  const destroyed = events.findLast(
    (event) => event.worker.id === worker.info.id && event.event === 'close',
  )
  assert.ok(destroyed)
  assert.deepEqual(destroyed.worker.control, current)
  assert.ok(destroyed.sequence > channel.sequence)
  assert.equal((await diagnosticsNow()).nativeWorkerOperations, 0)
  receipts.push({ phase: 'actual author terminate and native close receipt', value: destroyed })
  checks.push('Actual author termination settles the original close wait after reattachment')

  await actual.sendDevToolsCommand('Target.setAutoAttach', {
    autoAttach: true,
    waitForDebuggerOnStart: true,
    flatten: true,
  })
  const ordinary = await createWorker()
  const ordinaryClosed = observe(ordinary.waitForEvent('close', { timeout: 5000 }))
  await page.evaluate('actualLifetimeWorker.terminate()')
  assert.equal(await ordinaryClosed, ordinary)
  assert.equal(ordinary.isClosed(), true)
  assert.equal(
    (await targetsNow()).some((target) => target.targetId === ordinary.info.id),
    false,
  )
  checks.push(
    'Ordinary real worker termination still publishes close and removes its actual target',
  )

  for (const [name, url] of [
    ['same-origin', origin + '/after-navigation'],
    ['cross-origin', crossOrigin + '/after-navigation'],
  ]) {
    assert.ok(url)
    const navigated = await createWorker()
    await page.goto(url)
    assert.equal(navigated.isClosed(), true, `${name}: close must precede navigation completion`)
    assert.equal(page.workers().length, 0)
    assert.equal(
      (await targetsNow()).some((target) => target.targetId === navigated.info.id),
      false,
    )
    checks.push(
      `Actual ${name} navigation preserves the original immediate worker-retirement contract`,
    )
  }
  assert.equal((await diagnosticsNow()).nativeWorkerOperations, 0)
  await actual.close()
  await actual.processClosed
  assert.deepEqual(actual.processExit?.(), { code: 0, signal: null })
  receipts.push({ phase: 'actual helper exit and stdio closure', value: actual.processExit?.() })
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  if (source) {
    await source.close().catch((error) => cleanupFailures.push(String(error)))
    await source.processClosed?.catch((error) => cleanupFailures.push(String(error)))
    const exit = source.processExit?.()
    receipts.push({ phase: 'cleanup actual process exit and stdio closure', value: exit })
    if (exit?.code !== 0 || exit.signal !== null)
      cleanupFailures.push(`Actual helper exit was ${JSON.stringify(exit)}`)
  }
  unsubscribe?.()
  await Promise.all(settlements)
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      { binarySHA256, cachePolicy, checks, failures, cleanupFailures, receipts, events },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.deepEqual(cleanupFailures, [])
assert.equal(checks.length, 6)
