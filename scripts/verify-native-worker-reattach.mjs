// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, realpath, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser } from '../dist/native-browser.js'
import { NativeConsoleMessage } from '../dist/native-page.js'
import {
  NativeWorkerControlSchema,
  NativeWorkerInfoSchema,
} from '../dist/native-worker-protocol.js'
import { NativeWorkerEvents } from '../dist/native-worker.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_REATTACH_DIRECTORY ?? '.suitecut/native-worker-reattach',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([])
const events = z
  .array(z.object({ event: z.string(), id: z.string(), sequence: z.number() }))
  .parse([])
const receipts = z.array(z.object({ method: z.string(), result: z.unknown() })).parse([])
const server = createServer((request, response) => {
  if (request.url === '/service.js') {
    response.setHeader('content-type', 'application/javascript')
    response.end(
      "self.addEventListener('install',e=>e.waitUntil(skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(clients.claim()));",
    )
  } else {
    response.setHeader('content-type', 'text/html')
    response.end('<body>actual worker reattach</body>')
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let closeSource = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (line) => diagnostics.push(line),
  })
  closeSource = () => source.close()
  source.onWorkerMessage?.((event) =>
    events.push({ event: event.event, id: event.worker.id, sequence: event.sequence }),
  )
  let commands = 0
  const bridge = NativeWorkerEvents.forOwner({
    source,
    defaultTimeout: () => 5000,
    runWorker: (params, signal) => {
      commands++
      return source.runAutomation('worker', params, signal)
    },
    isPageHandle: () => false,
    createConsole: (info) =>
      new NativeConsoleMessage(
        {
          source,
          workerFromConsole: (value) => bridge.fromConsole(NativeWorkerInfoSchema.parse(value)),
        },
        info,
      ),
  })
  await source.navigate(origin)
  const registered = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: `navigator.serviceWorker.register(${JSON.stringify(origin + '/service.js')}).then(()=>navigator.serviceWorker.ready).then(()=>true)`,
    awaitPromise: true,
    returnByValue: true,
  })
  assert.equal(
    z.object({ result: z.object({ value: z.literal(true) }) }).parse(registered).result.value,
    true,
  )
  const inventory = z
    .object({
      targetInfos: z.array(z.object({ targetId: z.string(), type: z.string(), url: z.string() })),
    })
    .parse(await source.sendDevToolsCommand('Target.getTargets'))
  const target = inventory.targetInfos.find(
    (info) => info.type === 'service_worker' && info.url === origin + '/service.js',
  )
  assert.ok(target, 'Actual Inspector service-worker target is unavailable')
  // The real native operation waits its original 5s budget for genuine initial
  // Runtime/Network/script/resume receipts. There is no host readiness timer.
  await source.runAutomation('worker', {
    worker: target.targetId,
    action: 'evaluate',
    expression: '1',
    timeoutMs: 5000,
  })
  const info = source.workerInfos?.().find((value) => value.id === target.targetId)
  assert.ok(info, 'Actual Source ready inventory is unavailable')
  const originalControl = NativeWorkerControlSchema.parse(info.control)
  receipts.push({ method: 'actual initial Source control', result: originalControl })
  const worker = bridge.fromObservedInfo(info)
  assert.ok(events.some((event) => event.id === target.targetId && event.event === 'worker'))
  await worker.evaluate('globalThis.__suitecutActualRecovery={value:23};23')
  const handle = await worker.evaluateHandle('globalThis.__suitecutActualRecovery')
  assert.deepEqual(await handle.jsonValue(), { value: 23 })
  checks.push(
    'Actual Source worker ready, persistent author state and owned opaque handle value are observed',
  )

  const detached = await source.sendDevToolsCommand('Target.setAutoAttach', {
    autoAttach: false,
    waitForDebuggerOnStart: false,
    flatten: true,
  })
  receipts.push({ method: 'Target.setAutoAttach(false)', result: detached })
  assert.ok(
    events.some((event) => event.id === target.targetId && event.event === 'channelclosed'),
    'Actual channel-close receipt is unavailable',
  )
  assert.equal(worker.isClosed(), false)
  assert.throws(() => worker.assertUsable(), /channel detached/u)
  checks.push(
    'Actual Inspector auto-attach disable ACK and channelclosed preserve author-target identity',
  )

  const attached = z.object({ sessionId: z.string().min(1) }).parse(
    await source.sendDevToolsCommand('Target.attachToTarget', {
      targetId: target.targetId,
      flatten: true,
    }),
  )
  receipts.push({ method: 'Target.attachToTarget', result: attached })
  await source.runAutomation('worker', {
    worker: target.targetId,
    action: 'evaluate',
    expression: '1',
    timeoutMs: 5000,
  })
  const current = source.workerInfos?.().find((value) => value.id === target.targetId)
  assert.ok(current, 'Actual reattached ready inventory is unavailable')
  const currentControl = NativeWorkerControlSchema.parse(current.control)
  receipts.push({ method: 'actual reattached Source control', result: currentControl })
  assert.equal(currentControl.owner, originalControl.owner)
  assert.notEqual(currentControl.generation, originalControl.generation)
  assert.equal(bridge.fromObservedInfo(current), worker)
  assert.equal(
    events.filter((event) => event.id === target.targetId && event.event === 'worker').length,
    2,
  )
  assert.equal(await worker.evaluate('globalThis.__suitecutActualRecovery.value'), 23)
  const before = commands
  await assert.rejects(handle.jsonValue(), /earlier Inspector control channel/u)
  await handle.dispose()
  assert.equal(commands, before, 'Old handle operations reached the protocol')
  const fresh = await worker.evaluateHandle('globalThis.__suitecutActualRecovery')
  assert.deepEqual(await fresh.jsonValue(), { value: 23 })
  await fresh.dispose()
  checks.push(
    'Genuine same-target new-session ACK and ready recover canonical control; old handles reject before RPC',
  )
  await source.close()
  assert.ok(source.processClosed)
  await source.processClosed
  const exit = source.processExit?.()
  assert.ok(exit)
  receipts.push({ method: 'actual helper process and stdio closure', result: exit })
  assert.deepEqual(exit, { code: 0, signal: null })
  assert.equal(worker.isClosed(), true)
  checks.push('Actual helper process and stdio closure retire the recovered canonical worker')
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await closeSource().catch((error) => failures.push(String(error)))
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
      {
        binarySHA256,
        sdkDirectory: await realpath(new URL('../dist', import.meta.url)),
        checks,
        failures,
        events,
        receipts,
        diagnostics,
        scope:
          'Actual same Source owner only; broker replacement sequence ownership is outside this checker',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
