// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, Socket } from 'node:net'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as deadlineTimer } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePublisher,
  launchNativeBrowser,
  NativeBrowserContexts,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_CRASH_THROW_DIRECTORY ?? '.suitecut/native-crash-throw-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Bounds real exception and physical settlement observation.
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
        throw new Error('Timeout: actual crash listener throw proof did not settle')
      }),
    ])
  } finally {
    cancel.abort()
  }
}
const chunks = z.array(z.instanceof(Buffer)).parse([])
const sockets = new Set(z.array(z.instanceof(Socket)).parse([]))
let eof = Promise.resolve()
let connected = false
const server = createServer((socket) => {
  connected = true
  sockets.add(socket)
  socket.on('error', () => undefined)
  eof = once(socket, 'close').then(() => undefined)
  socket.on('data', (chunk) => {
    const data = z.instanceof(Buffer).parse(chunk)
    chunks.push(data)
    assert(chunks.reduce((n, v) => n + v.length, 0) < 4 * 1024 * 1024)
  })
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address === 'object')
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
  throw new Error('Real native contexts unavailable')
const contexts = new NativeBrowserContexts(
  Object.assign(source, {
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  }),
)
const publicError = new Error('actual user public crash listener exception')
const rawError = new Error('actual user raw crash listener exception')
const observed = z.array(z.instanceof(Error)).parse([])
let resolveErrors = () => {
  return
}
const exceptions = new Promise((resolve) => {
  resolveErrors = () => resolve(undefined)
})
/**
 * This caller-owned observer makes real asynchronous exceptions inspectable;
 * the library installs no handler and preserves normal unhandled-error behavior.
 * @param {Error} error
 */
const observe = (error) => {
  observed.push(error)
  if (observed.length >= 2) resolveErrors(observed)
}
process.on('uncaughtException', observe)
const behaviorErrors = z.array(z.unknown()).parse([])
try {
  const context = await contexts.newContext()
  const page = await context.newPage()
  await page.setContent(
    '<canvas width=160 height=96></canvas><script>let x=0;function draw(){document.querySelector("canvas").getContext("2d").fillRect(x++%160,0,1,96);requestAnimationFrame(draw)}draw()</script>',
  )
  const publisher = await createNativePublisher({
    source: page.source,
    ffmpegPath: process.env.SUITECUT_FFMPEG ?? '/usr/bin/ffmpeg',
    publisher: {
      destination: `tcp://127.0.0.1:${address.port}`,
      videoCodec: 'vp9',
      audioCodec: 'opus',
      container: 'webm',
      framesPerSecond: 29.97,
      audio: false,
    },
  })
  const before = await publisher.status()
  assert(before.physicalEncoderPid > 0)
  const lost = page.evaluate('new Promise(()=>{})').catch((error) => {
    assert(error instanceof Error)
    return error.message
  })
  const calls = z.array(z.string()).parse([])
  page.on('crash', () => {
    assert.equal(page.source.signal.aborted, true)
    calls.push('throw')
    throw publicError
  })
  page.on('crash', () => {
    calls.push('after-throw')
  })
  if (!page.source.onCrashMessage) throw new Error('Actual native crash transport unavailable')
  page.source.onCrashMessage(() => {
    throw rawError
  })
  const crashEvent = page.waitForEvent('crash', { timeout: 5000 })
  const publishFailure = publisher.failure.catch((error) => {
    assert(error instanceof Error)
    return error.message
  })
  const crash = page.source.sendDevToolsCommand('Page.crash').catch((error) => {
    assert(error instanceof Error)
    return error.message
  })
  assert.equal(await bounded(crashEvent), page)
  await bounded(exceptions)
  assert.equal(observed[0], publicError)
  assert.equal(observed[1], rawError)
  assert.deepEqual(calls, ['throw', 'after-throw'])
  assert.match(z.string().parse(await bounded(lost)), /renderer|terminated|crash/iu)
  assert.match(await bounded(publishFailure), /renderer|failed|closed|terminated/iu)
  let diagnostics
  const deadline = performance.now() + 5000
  do {
    diagnostics = z
      .object({ nativeStreamingPhysical: z.number() })
      .parse(await source.runAutomation('diagnostics'))
  } while (diagnostics.nativeStreamingPhysical && performance.now() < deadline)
  assert.equal(diagnostics.nativeStreamingPhysical, 0)
  assert.throws(() => process.kill(before.physicalEncoderPid, 0), { code: 'ESRCH' })
  assert(connected)
  await bounded(eof)
  assert(chunks.length)
  const terminal = await publisher.status()
  assert.equal(terminal.physicalEncoderPid, 0)
  assert.equal(terminal.phase, 'failed')
  const healthy = z
    .object({ result: z.object({ value: z.number() }) })
    .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
  assert.equal(healthy.result.value, 42)
  assert.equal(source.state, 'running')
  await context.close()
  await writeFile(join(directory, 'actual-crashed-publisher.webm'), Buffer.concat(chunks))
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        observedExceptions: observed.map((error) => ({
          message: error.message,
          stack: error.stack,
        })),
        sameAuthoredErrorObjectsObserved: true,
        cleanupCompletedBeforeUserListener: true,
        laterListenerInvoked: true,
        actualNativeCrashReply: await bounded(crash),
        before,
        terminal,
        diagnostics,
        actualTcpEOF: true,
        actualEncoderReaped: true,
        healthyRootValue: healthy.result.value,
        callerInstalledUncaughtExceptionObserver: true,
      },
      null,
      2,
    ) + '\n',
  )
  console.log(
    'Actual crash listener errors remained visible after cleanup, genuine encoder reap/EOF and healthy root proof passed',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        observedExceptions: observed.map((error) => error.message),
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
    for (const socket of sockets) socket.destroy()
    await new Promise((resolve) => server.close(resolve))
    process.off('uncaughtException', observe)
  }
}
