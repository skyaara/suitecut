// SPDX-License-Identifier: MIT
/* global navigator */
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { NativeWorkerControlSchema } from '../dist/native-worker-protocol.js'
import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_REPLACEMENT_DIRECTORY ??
    '.suitecut/native-context-worker-replacement',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const receipts = z.array(z.object({ phase: z.string(), value: z.unknown() })).parse([])
const decimal = z.string().regex(/^(0|[1-9][0-9]*)$/u)
const brokerSchema = z.object({
  contextId: z.number().int(),
  controlOwner: z.string().min(1),
  rootTargetId: z.string().min(1),
  eventCursor: decimal,
  initialEventCursor: decimal,
})
const diagnosticsSchema = z.object({
  nativeWorkerBrokers: z.number().int(),
  nativeWorkerCursorContexts: z.number().int(),
  nativeWorkerOwnerCloseWaits: z.number().int(),
  nativeWorkerBrokerContexts: z.array(brokerSchema),
})
const server = createServer((request, response) => {
  response.setHeader(
    'content-type',
    request.url === '/service.js' ? 'application/javascript' : 'text/html',
  )
  response.end(
    request.url === '/service.js'
      ? "self.addEventListener('install',e=>e.waitUntil(skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(clients.claim()));"
      : '<body>actual context broker replacement</body>',
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let close = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  close = () => source.close()
  const runContext = source.runContext?.bind(source)
  assert.ok(runContext, 'Actual context command transport is unavailable')
  const pageSource = source.pageSource?.bind(source)
  const onContextEvent = source.onContextEvent?.bind(source)
  assert.ok(pageSource && onContextEvent, 'Actual context adapters are unavailable')
  const contexts = new NativeBrowserContexts({
    ...source,
    runContext,
    pageSource,
    onContextEvent,
  })
  const observerBaseline = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  receipts.push({ phase: 'genuine still-live default observer baseline', value: observerBaseline })
  const context = await contexts.newContext()
  const page = await context.newPage()
  await page.goto(origin)
  const discovered = context.waitForServiceWorker({ timeout: 5000 })
  discovered.catch(() => undefined)
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
    await navigator.serviceWorker.ready
  })
  const worker = await discovered
  const original = NativeWorkerControlSchema.parse(worker.info.control)
  await worker.evaluate('globalThis.actualBrokerReplacementValue=23;23')
  const handle = await worker.evaluateHandle('({actualBrokerReplacementValue})')
  assert.deepEqual(await handle.jsonValue(), { actualBrokerReplacementValue: 23 })
  const before = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  const broker = before.nativeWorkerBrokerContexts.find((value) => value.contextId === context.id)
  assert.ok(broker)
  assert.equal(original.owner, broker.controlOwner)
  receipts.push({ phase: 'actual original broker ownership', value: broker })
  checks.push('Actual private broker owns the observed canonical worker and original opaque handle')

  const closed = runContext('worker', {
    contextId: context.id,
    action: 'waitOwnerClosed',
    controlOwner: broker.controlOwner,
    timeoutMs: 5000,
  })
  closed.catch(() => undefined)
  const admitted = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(admitted.nativeWorkerOwnerCloseWaits, 1)
  receipts.push({ phase: 'native original owner-close wait admitted', value: admitted })
  const requested = await source.sendDevToolsCommand('Target.closeTarget', {
    targetId: broker.rootTargetId,
  })
  assert.equal(z.object({ success: z.literal(true) }).parse(requested).success, true)
  receipts.push({ phase: 'actual closeTarget request ACK only', value: requested })
  const physical = z
    .object({ contextId: z.number().int(), controlOwner: z.string(), finalEventCursor: decimal })
    .parse(await closed)
  assert.equal(physical.contextId, context.id)
  assert.equal(physical.controlOwner, broker.controlOwner)
  assert.ok(BigInt(physical.finalEventCursor) >= BigInt(broker.eventCursor))
  receipts.push({ phase: 'actual final Client OnBeforeClose manager receipt', value: physical })
  const between = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(between.nativeWorkerBrokers, observerBaseline.nativeWorkerBrokers)
  assert.equal(
    between.nativeWorkerBrokerContexts.some((value) => value.contextId === context.id),
    false,
  )
  assert.equal(between.nativeWorkerOwnerCloseWaits, 0)
  assert.equal(between.nativeWorkerCursorContexts, observerBaseline.nativeWorkerCursorContexts + 1)
  checks.push(
    'Real OnBeforeClose commits the final manager cursor before replacement can be admitted',
  )

  assert.equal(
    worker.isClosed(),
    false,
    'Closing the broker channel must not invent service-worker execution death',
  )
  const target = await source.sendDevToolsCommand('Target.getTargetInfo', {
    targetId: worker.info.id,
  })
  assert.equal(
    z.object({ targetInfo: z.object({ targetId: z.string() }) }).parse(target).targetInfo.targetId,
    worker.info.id,
  )
  receipts.push({ phase: 'genuine author target after old broker closure', value: target })
  const recovered = await context.waitForServiceWorker({ timeout: 5000 })
  assert.equal(recovered, worker)
  const current = NativeWorkerControlSchema.parse(worker.info.control)
  assert.notEqual(current.owner, original.owner)
  const after = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  const replacement = after.nativeWorkerBrokerContexts.find(
    (value) => value.contextId === context.id,
  )
  assert.ok(replacement)
  assert.equal(replacement.controlOwner, current.owner)
  assert.equal(replacement.initialEventCursor, physical.finalEventCursor)
  assert.ok(BigInt(replacement.eventCursor) > BigInt(physical.finalEventCursor))
  receipts.push({
    phase: 'actual replacement owner and inherited manager cursor',
    value: replacement,
  })
  checks.push(
    'Actual replacement ready preserves canonical identity and advances only the same manager publisher cursor',
  )

  assert.equal(await worker.evaluate('globalThis.actualBrokerReplacementValue'), 23)
  await assert.rejects(handle.jsonValue(), /earlier Inspector control channel/u)
  await handle.dispose()
  checks.push(
    'Genuine author state survives broker replacement while the old opaque control remains unusable',
  )
  await context.close()
  const final = diagnosticsSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(final.nativeWorkerBrokers, observerBaseline.nativeWorkerBrokers)
  assert.equal(
    final.nativeWorkerBrokerContexts.some((value) => value.contextId === context.id),
    false,
  )
  assert.equal(final.nativeWorkerCursorContexts, observerBaseline.nativeWorkerCursorContexts)
  assert.equal(final.nativeWorkerOwnerCloseWaits, 0)
  receipts.push({
    phase: 'genuine still-live observer after actual independent context disposal',
    value: final,
  })
  checks.push('Actual context disposal removes the physically closed manager cursor history')
  await source.close()
  await source.processClosed
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  receipts.push({ phase: 'actual helper process and stdio closure', value: source.processExit?.() })
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await close().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        binarySHA256,
        checks,
        failures,
        receipts,
        scope:
          'Checker-only observer correction: default context/Page stays live for genuine diagnostics; replacement runs in an independent context. Same manager cursor and original lifecycle assertions retained; global counters return to the actual measured observer baseline. Source/broker sequences remain incomparable.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
