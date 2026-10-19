// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { Socket } from 'node:net'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as timer } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_RECOVERY_DIRECTORY ?? '.suitecut/native-page-recovery-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Bounds actual native observation under the verifier deadline.
 * @template T
 * @param {Promise<T>} value
 * @returns {Promise<T>}
 */
async function bounded(value) {
  const owner = new AbortController()
  try {
    return await Promise.race([
      value,
      timer(10000, undefined, { signal: owner.signal }).then(() => {
        throw new Error('Native recovery proof operation exceeded verifier deadline')
      }),
    ])
  } finally {
    owner.abort()
  }
}
let actualDocuments = 0
const payload = Buffer.from('actual-native-download-after-recovery\n')
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  if (request.url === '/download') {
    response.setHeader('Content-Disposition', 'attachment; filename=real-recovery.txt')
    response.end(payload)
    return
  }
  if (request.url === '/recover') {
    ++actualDocuments
    response.setHeader('Content-Type', 'text/html')
    response.end(
      `<title>actual-document-${actualDocuments}</title><body><input type=checkbox id=check><input type=file id=upload><button id=dialog onclick="alert('after-recovery-dialog')">dialog</button><a id=download href=/download>download</a><script>console.log('actual-document-${actualDocuments}')</script>`,
    )
  } else {
    response.end('actual-server-response')
  }
})
const peers = z.array(z.instanceof(Socket)).parse([])
server.on('upgrade', (request, socket, head) => {
  const key = z.string().min(1).parse(request.headers['sec-websocket-key'])
  peers.push(socket)
  const accept = createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64')
  socket.write(
    `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
  )
  let pending = Buffer.from(head)
  socket.on('data', (chunk) => {
    pending = Buffer.concat([pending, z.instanceof(Buffer).parse(chunk)])
    assert(pending.length <= 4096, 'Recovery echo fixture frame byte bound exceeded')
    while (pending.length >= 6) {
      const opcode = z.number().parse(pending[0]) & 15
      const length = z.number().parse(pending[1]) & 127
      assert(length < 126, 'Authored echo fixture only accepts bounded short messages')
      assert(z.number().parse(pending[1]) & 128, 'Browser client frame must be masked')
      if (pending.length < length + 6) return
      const body = Buffer.alloc(length)
      for (let i = 0; i < length; ++i)
        body[i] = z.number().parse(pending[i + 6]) ^ z.number().parse(pending[2 + (i % 4)])
      pending = pending.subarray(length + 6)
      if (opcode === 8) {
        socket.end(Buffer.concat([Buffer.from([136, length]), body]))
        return
      }
      if (opcode === 1 || opcode === 2 || opcode === 9)
        socket.write(
          Buffer.concat([Buffer.from([128 | (opcode === 9 ? 10 : opcode), length]), body]),
        )
    }
  })
})
await new Promise((accept) => server.listen(0, '127.0.0.1', accept))
const address = server.address()
if (!address || typeof address === 'string') throw new Error('Actual server port unavailable')
const url = `http://127.0.0.1:${address.port}/recover`
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
  throw new Error('Actual native context transport missing')
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
  const identity = page.source
  const delivery = { dialogs: 0, choosers: 0, downloads: 0, workers: 0, sockets: 0 }
  const answers = z.array(z.promise(z.void())).parse([])
  page.on('dialog', (dialog) => {
    ++delivery.dialogs
    answers.push(dialog.accept())
  })
  page.on('filechooser', () => {
    ++delivery.choosers
  })
  page.on('download', () => {
    ++delivery.downloads
  })
  page.on('worker', () => {
    ++delivery.workers
  })
  page.on('websocket', () => {
    ++delivery.sockets
  })
  const messages = z.array(z.string()).parse([])
  const errors = z.array(z.string()).parse([])
  const commits = z.array(z.string()).parse([])
  let crashes = 0
  let calls = 0
  page.on('console', (message) => messages.push(message.text()))
  page.on('pageerror', (error) => errors.push(error.message))
  page.on('framenavigated', (frame) => commits.push(frame.url()))
  page.on('crash', (actual) => {
    assert.equal(actual, page)
    ++crashes
  })
  await page.exposeFunction('nativeRetainedBinding', (value) => {
    ++calls
    return value
  })
  await page.route('**/intercept', (route) =>
    route.fulfill({ body: 'actual-native-retained-route' }),
  )
  await page.goto(url)
  const epochs = z
    .array(
      z.object({
        cycle: z.number(),
        actualTitle: z.string(),
        oldOperation: z.string(),
        oldPendingHandle: z.string(),
        admittedCounters: z.record(z.string(), z.unknown()),
        oldFailure: z.string(),
        actualCrash: z.string(),
        oldSignalRemainsAborted: z.boolean(),
        failedCounters: z.record(z.string(), z.unknown()),
        recoveredCounters: z.record(z.string(), z.unknown()),
        afterCurrentConsoleClear: z.record(z.string(), z.unknown()),
      }),
    )
    .parse([])
  for (let cycle = 1; cycle <= 2; ++cycle) {
    const oldSignal = page.source.signal
    const oldFailure = page.source.failure.catch((error) => {
      assert(error instanceof Error)
      return error.message
    })
    const oldHandle = await page.evaluateHandle('document.body')
    const oldOperation = page
      .evaluate(
        (value) =>
          new Promise((resolve) => {
            if (value === null) resolve(null)
          }),
        oldHandle,
      )
      .catch((error) => {
        assert(error instanceof Error)
        return error.message
      })
    const oldPendingHandle = page.evaluateHandle('new Promise(()=>{})').catch((error) => {
      assert(error instanceof Error)
      return error.message
    })
    const admittedCounters = z
      .record(z.string(), z.unknown())
      .parse(await page.source.runAutomation('diagnostics'))
    assert(z.number().parse(admittedCounters.activeHandles) > 0)
    assert(z.number().parse(admittedCounters.pendingHandles) > 0)
    assert(z.number().parse(admittedCounters.evaluationGroups) > 0)
    const crashWait = page.waitForEvent('crash', { timeout: 5000 })
    const actualCrash = page.source.sendDevToolsCommand('Page.crash').catch((error) => {
      assert(error instanceof Error)
      return error.message
    })
    assert.equal(await bounded(crashWait), page)
    assert.equal(page.source.state, 'failed')
    assert.equal(oldSignal.aborted, true)
    assert.equal(page.source.pageSignal?.aborted, false)
    await assert.rejects(page.title(), /renderer|terminated|crash/iu)
    const failedCounters = z
      .record(z.string(), z.unknown())
      .parse(await page.source.runAutomation('diagnostics'))
    assert.equal(failedCounters.rendererFailed, true)
    const response = await bounded(page.reload())
    assert.equal(response?.status(), 200)
    assert.equal(page.source, identity)
    assert.equal(page.source.state, 'running')
    assert.notEqual(page.source.signal, oldSignal)
    assert.equal(page.source.signal.aborted, false)
    assert.equal(oldSignal.aborted, true)
    assert.equal(await page.title(), `actual-document-${cycle + 1}`)
    assert.equal(await page.evaluate('6*7'), 42)
    assert.equal(
      await page.evaluate('nativeRetainedBinding("real-binding-result")'),
      'real-binding-result',
    )
    assert.equal(
      await page.evaluate('fetch("/intercept").then(response=>response.text())'),
      'actual-native-retained-route',
    )
    await assert.rejects(oldHandle.jsonValue(), /handle|context|destroy|dispos/iu)
    await page.check('#check')
    assert.equal(await page.isChecked('#check'), true)
    await page.click('#dialog')
    await Promise.all(answers.splice(0))
    const chooserWait = page.waitForEvent('filechooser', { timeout: 2000 })
    await page.click('#upload')
    const chooser = await bounded(chooserWait)
    await chooser.setFiles({
      name: 'real-recovery-upload.txt',
      mimeType: 'text/plain',
      buffer: Buffer.from('actual-recovery-upload'),
    })
    assert.equal(
      await page.evaluate('document.querySelector("#upload").files[0].text()'),
      'actual-recovery-upload',
    )
    await chooser.element().dispose()
    const downloadWait = page.waitForEvent('download', { timeout: 2000 })
    await page.click('#download')
    const download = await bounded(downloadWait)
    assert.equal(download.suggestedFilename(), 'real-recovery.txt')
    const actualPath = await download.path()
    assert(actualPath)
    assert.deepEqual(await readFile(actualPath), payload)
    const workerWait = page.waitForEvent('worker', { timeout: 2000 })
    await page.evaluate(
      'globalThis.recoveryWorker=new Worker(URL.createObjectURL(new Blob(["onmessage=()=>postMessage(42)"],{type:"text/javascript"})))',
    )
    const worker = await bounded(workerWait)
    assert.equal(await worker.evaluate('6*7'), 42)
    const socketWait = page.waitForEvent('websocket', { timeout: 2000 })
    await page.evaluate(
      `globalThis.recoverySocket=new WebSocket(${JSON.stringify('ws://127.0.0.1:' + address.port + '/echo')});new Promise(resolve=>recoverySocket.onopen=()=>{recoverySocket.onmessage=event=>{globalThis.actualEcho=event.data;resolve()};recoverySocket.send("real-echo")})`,
    )
    await bounded(socketWait)
    assert.equal(await page.evaluate('actualEcho'), 'real-echo')
    await page.evaluate(
      'console.log("after-real-recovery");setTimeout(()=>{throw new Error("after-real-recovery-error")},0)',
    )
    const readiness = await page.waitForFunction('document.title.startsWith("actual-document-")')
    await readiness.dispose()
    const recoveredCounters = z
      .record(z.string(), z.unknown())
      .parse(await page.source.runAutomation('diagnostics'))
    await writeFile(
      join(directory, `cycle-${cycle}-native-counters.json`),
      JSON.stringify({ admittedCounters, failedCounters, recoveredCounters }, null, 2) + '\n',
    )
    assert.equal(recoveredCounters.rendererFailed, false)
    assert.equal(recoveredCounters.pendingHandles, 0)
    assert.equal(recoveredCounters.evaluationGroups, 0)
    assert.equal(
      recoveredCounters.failedEpochRetiredHandleMetadata,
      recoveredCounters.failedEpochHandleMetadata,
    )
    assert.equal(
      recoveredCounters.failedEpochRetiredPendingHandleMetadata,
      recoveredCounters.failedEpochPendingHandleMetadata,
    )
    assert.equal(
      recoveredCounters.failedEpochRetiredEvaluationGroups,
      recoveredCounters.failedEpochEvaluationGroups,
    )
    // The old epoch must already be retired before disposing this new document's legitimate console journal.
    await page.clearConsoleMessages()
    const afterCurrentConsoleClear = z
      .record(z.string(), z.unknown())
      .parse(await page.source.runAutomation('diagnostics'))
    assert.equal(afterCurrentConsoleClear.activeHandles, 0)
    assert.equal(afterCurrentConsoleClear.pendingHandles, 0)
    assert.equal(afterCurrentConsoleClear.evaluationGroups, 0)
    epochs.push({
      cycle,
      admittedCounters,
      failedCounters,
      recoveredCounters,
      afterCurrentConsoleClear,
      oldPendingHandle: z.string().parse(await bounded(oldPendingHandle)),
      actualTitle: await page.title(),
      oldOperation: z.string().parse(await bounded(oldOperation)),
      oldFailure: await bounded(oldFailure),
      actualCrash: await bounded(actualCrash),
      oldSignalRemainsAborted: oldSignal.aborted,
    })
  }
  assert.equal(actualDocuments, 3)
  assert.equal(crashes, 2)
  assert.equal(calls, 2)
  assert.deepEqual(delivery, { dialogs: 2, choosers: 2, downloads: 2, workers: 2, sockets: 2 })
  assert(messages.includes('after-real-recovery'))
  assert(commits.filter((value) => value === url).length >= 3)
  const finalError = page.waitForEvent('pageerror', {
    predicate: (error) => error.message === 'final-recovered-error',
    timeout: 2000,
  })
  await page.evaluate('setTimeout(()=>{throw new Error("final-recovered-error")},0)')
  const actualError = await bounded(finalError)
  await context.close()
  assert.equal(page.isClosed(), true)
  assert.equal(identity.pageSignal?.aborted, true)
  const root = z
    .object({ result: z.object({ value: z.number() }) })
    .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
  assert.equal(root.result.value, 42)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        actualDocuments,
        crashes,
        calls,
        delivery,
        messages,
        errors,
        commits,
        epochs,
        actualError: actualError.message,
        healthyRootValue: root.result.value,
      },
      null,
      2,
    ) + '\n',
  )
  console.log('Actual native repeated crash/reload recovery proof passed')
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        actualDocuments,
        error: error instanceof Error ? error.stack : 'Non-Error failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  try {
    await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
  } finally {
    for (const peer of peers) peer.destroy()
    await new Promise((accept, reject) =>
      server.close((error) => (error ? reject(error) : accept(undefined))),
    )
  }
}
