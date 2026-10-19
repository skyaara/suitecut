// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser } from '../dist/native-browser.js'
import { NativeConsoleMessage } from '../dist/native-page.js'
import { NativeConsoleInfoSchema } from '../dist/native-protocol.js'
import {
  NativeWorkerControlSchema,
  NativeWorkerInfoSchema,
} from '../dist/native-worker-protocol.js'
import { NativeWorkerEvents } from '../dist/native-worker.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE')
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_CONSOLE_GENERATION_DIRECTORY ??
    '.suitecut/native-worker-console-generation',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const receipts = z.array(z.object({ method: z.string(), result: z.unknown() })).parse([])
const server = createServer((request, response) => {
  if (request.url === '/worker.js') {
    response.setHeader('content-type', 'application/javascript')
    response.end(
      "globalThis.actualGenerationValue={value:23};self.postMessage('actual-author-ready');",
    )
  } else {
    response.setHeader('content-type', 'text/html')
    response.end('<body>Actual console generation history</body>')
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
  let commands = 0
  const consoleOwner = {
    source,
    workerFromConsole: (info) => bridge.fromConsole(NativeWorkerInfoSchema.parse(info)),
  }
  const bridge = NativeWorkerEvents.forOwner({
    source,
    defaultTimeout: () => 5000,
    runWorker: (params, signal) => {
      commands++
      return source.runAutomation('worker', params, signal)
    },
    isPageHandle: () => false,
    createConsole: (info) => new NativeConsoleMessage(consoleOwner, info),
  })
  await source.navigate(origin)
  const author = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: `globalThis.__suitecutActualDedicated=new Worker(${JSON.stringify(origin + '/worker.js')});new Promise(resolve=>{__suitecutActualDedicated.onmessage=event=>resolve(event.data)})`,
    awaitPromise: true,
    returnByValue: true,
  })
  assert.equal(
    z.object({ result: z.object({ value: z.literal('actual-author-ready') }) }).parse(author).result
      .value,
    'actual-author-ready',
  )
  const targets = z
    .object({
      targetInfos: z.array(z.object({ targetId: z.string(), type: z.string(), url: z.string() })),
    })
    .parse(await source.sendDevToolsCommand('Target.getTargets'))
  const target = targets.targetInfos.find(
    (value) => value.type === 'worker' && value.url === origin + '/worker.js',
  )
  assert.ok(target, 'Actual dedicated Inspector target unavailable')
  await source.runAutomation('worker', {
    worker: target.targetId,
    action: 'evaluate',
    expression: '1',
    timeoutMs: 5000,
  })
  const original = source.workerInfos?.().find((info) => info.id === target.targetId)
  assert.ok(original, 'Actual worker ready inventory unavailable')
  const originalControl = NativeWorkerControlSchema.parse(original.control)
  const worker = bridge.fromObservedInfo(original)
  const rawOld = new Promise((done) => {
    const detach = source.onConsoleMessage?.((value) => {
      if (value.worker !== target.targetId || !value.text.includes('suitecut-old-generation'))
        return
      detach?.()
      done(NativeConsoleInfoSchema.parse(value))
    })
    assert.ok(detach, 'Actual console subscription unavailable')
  })
  const [actualOld] = await Promise.all([
    rawOld.then((value) => NativeConsoleInfoSchema.parse(value)),
    worker.evaluate("console.log('suitecut-old-generation',globalThis.actualGenerationValue)"),
  ])
  assert.deepEqual(actualOld.workerInfo?.control, originalControl)
  const oldMessage = new NativeConsoleMessage(consoleOwner, actualOld)
  const oldArgument = oldMessage.args()[1]
  assert.ok(oldArgument)
  assert.deepEqual(await oldArgument.jsonValue(), { value: 23 })
  receipts.push({ method: 'actual old console DTO', result: actualOld })
  checks.push(
    'Actual dedicated worker console argument uses exact native owner and attachment generation',
  )

  receipts.push({
    method: 'Target.setAutoAttach(false)',
    result: await source.sendDevToolsCommand('Target.setAutoAttach', {
      autoAttach: false,
      waitForDebuggerOnStart: false,
      flatten: true,
    }),
  })
  assert.throws(() => worker.assertUsable(), /channel detached/u)
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
  const current = source.workerInfos?.().find((info) => info.id === target.targetId)
  assert.ok(current, 'Actual reattached worker ready inventory unavailable')
  const currentControl = NativeWorkerControlSchema.parse(current.control)
  assert.equal(currentControl.owner, originalControl.owner)
  assert.notEqual(currentControl.generation, originalControl.generation)
  assert.equal(bridge.fromObservedInfo(current), worker)
  const before = commands
  const delayed = new NativeConsoleMessage(consoleOwner, actualOld)
  assert.equal(delayed.worker(), worker)
  assert.throws(() => delayed.args(), /another Inspector owner or attachment generation/u)
  assert.throws(() => oldMessage.args(), /earlier Inspector control channel/u)
  await assert.rejects(oldArgument.jsonValue(), /earlier Inspector control channel/u)
  await oldArgument.dispose()
  assert.equal(commands, before, 'Historical console argument crossed into the new native channel')
  checks.push(
    'First-materialized historical console and old opaque argument reject before any protocol command',
  )

  const rawNew = new Promise((done) => {
    const detach = source.onConsoleMessage?.((value) => {
      if (value.worker !== target.targetId || !value.text.includes('suitecut-new-generation'))
        return
      detach?.()
      done(NativeConsoleInfoSchema.parse(value))
    })
    assert.ok(detach)
  })
  const [actualNew] = await Promise.all([
    rawNew.then((value) => NativeConsoleInfoSchema.parse(value)),
    worker.evaluate("console.log('suitecut-new-generation',globalThis.actualGenerationValue)"),
  ])
  assert.deepEqual(actualNew.workerInfo?.control, currentControl)
  const currentMessage = new NativeConsoleMessage(consoleOwner, actualNew)
  assert.equal(currentMessage.worker(), worker)
  const currentArgument = currentMessage.args()[1]
  assert.ok(currentArgument)
  assert.deepEqual(await currentArgument.jsonValue(), { value: 23 })
  await currentArgument.dispose()
  receipts.push({ method: 'actual new console DTO', result: actualNew })
  checks.push('Genuine new console argument remains usable on the recovered canonical worker')
  await source.close()
  await source.processClosed
  const exit = source.processExit?.()
  assert.deepEqual(exit, { code: 0, signal: null })
  assert.equal(worker.isClosed(), true)
  receipts.push({ method: 'actual helper close', result: exit })
  checks.push('Actual helper exit and stdio closure close the canonical worker')
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
        checks,
        failures,
        receipts,
        diagnostics,
        scope:
          'Actual same Source owner dedicated-worker console; no cross-owner broker lifecycle ordering claim',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
