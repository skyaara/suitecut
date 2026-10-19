// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, Socket } from 'node:net'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as timer } from 'node:timers/promises'
import { promisify } from 'node:util'

import * as z from 'zod'

import { NativeRecordingInfoSchema } from '../dist/native-recording-protocol.js'
import {
  createNativePublisher,
  launchNativeBrowser,
  NativeBrowserContexts,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_RECOVERY_DIRECTORY ?? '.suitecut/native-page-recovery-media-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Bounds observation of genuine native lifecycle completion.
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
        throw new Error('Native recovery media operation exceeded verifier deadline')
      }),
    ])
  } finally {
    owner.abort()
  }
}
/**
 * Receives actual encoded native publisher bytes until physical EOF.
 * @param {string} name
 */
async function receiver(name) {
  const chunks = z.array(z.instanceof(Buffer)).parse([])
  const active = z.object({ socket: z.instanceof(Socket).optional() }).parse({})
  let eof = Promise.resolve()
  let bytes = 0
  let connected = false
  const server = createServer((socket) => {
    active.socket = socket
    connected = true
    eof = once(socket, 'end').then(() => undefined)
    void eof.catch(() => undefined)
    socket.on('data', (value) => {
      const chunk = z.instanceof(Buffer).parse(value)
      bytes += chunk.length
      if (bytes > 64 * 1024 * 1024) {
        socket.destroy(new Error('Native recovery receiver byte bound exceeded'))
        return
      }
      chunks.push(chunk)
    })
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  assert(address && typeof address === 'object')
  return {
    url: `tcp://127.0.0.1:${address.port}`,
    async finish() {
      assert(connected)
      await bounded(eof)
      const path = join(directory, name)
      await writeFile(path, Buffer.concat(chunks))
      return { path, bytes }
    },
    async close() {
      active.socket?.destroy()
      await new Promise((accept) => server.close(accept))
    },
  }
}
/**
 * Creates a real animated renderer surface.
 * @param {import('../dist/native-page.js').NativePage} page
 */
async function scene(page) {
  await page.setContent(
    '<style>body{margin:0}</style><canvas width=160 height=96></canvas><script>globalThis.frames=0;const ctx=document.querySelector("canvas").getContext("2d");function draw(){ctx.fillStyle=(frames%2)?"#ee2233":"#2233ee";ctx.fillRect(0,0,160,96);++frames;requestAnimationFrame(draw)}draw()</script>',
  )
  await page.waitForFunction('frames>12')
}
/**
 * Requires actual renderer input frames instead of sleeping.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {number} count
 */
async function advance(page, count) {
  const before = z.number().parse(await page.evaluate('frames'))
  await page.waitForFunction(`frames>=${before + count}`)
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
  throw new Error('Actual context transport unavailable')
const contexts = new NativeBrowserContexts(
  Object.assign(source, {
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  }),
)
const oldTCP = await receiver('actual-failed-before-recovery.webm'),
  newTCP = await receiver('actual-complete-after-recovery.webm')
const behaviorErrors = z.array(z.unknown()).parse([])
try {
  const context = await contexts.newContext()
  const page = await context.newPage()
  page.source.onFrame = () => {
    throw new Error('Native recovery publisher installed host frame sink')
  }
  page.source.onAudio = () => {
    throw new Error('Native recovery publisher installed host audio sink')
  }
  await scene(page)
  const oldPublisher = await createNativePublisher({
    source: page.source,
    ffmpegPath: '/usr/bin/ffmpeg',
    publisher: {
      destination: oldTCP.url,
      container: 'webm',
      videoCodec: 'vp9',
      audioCodec: 'opus',
      audio: false,
      framesPerSecond: 29.97,
    },
  })
  await advance(page, 40)
  const oldStatus = await oldPublisher.status()
  assert(oldStatus.physicalEncoderPid > 0)
  const oldRecording = NativeRecordingInfoSchema.parse(
    await page.source.runAutomation('recording', {
      action: 'start',
      ffmpegPath: '/usr/bin/ffmpeg',
      outputPath: join(directory, 'failed-before-recovery.webm'),
      framesPerSecond: 29.97,
      width: 160,
      height: 96,
      audio: false,
      timeoutMs: 5000,
    }),
  )
  const oldFailure = oldPublisher.failure.catch((error) =>
    error instanceof Error ? error.message : 'Non-Error failure',
  )
  const crashWait = page.waitForEvent('crash', { timeout: 5000 })
  const crash = page.source
    .sendDevToolsCommand('Page.crash')
    .catch((error) => (error instanceof Error ? error.message : 'Non-Error failure'))
  await bounded(crashWait)
  await bounded(page.reload())
  assert.equal(page.source.state, 'running')
  assert.match(await bounded(oldFailure), /failed|renderer|closed/iu)
  const deadline = performance.now() + 5000
  let physical
  do {
    physical = z
      .object({ nativeStreamingPhysical: z.number(), nativeRecordingPhysical: z.number() })
      .parse(await source.runAutomation('diagnostics'))
  } while (
    (physical.nativeStreamingPhysical || physical.nativeRecordingPhysical) &&
    performance.now() < deadline
  )
  assert.equal(physical.nativeStreamingPhysical, 0)
  assert.equal(physical.nativeRecordingPhysical, 0)
  assert.throws(() => process.kill(oldStatus.physicalEncoderPid, 0), { code: 'ESRCH' })
  await oldTCP.finish()
  const oldTerminal = await oldPublisher.status()
  assert.equal(oldTerminal.phase, 'failed')
  assert.equal(oldTerminal.physicalEncoderPid, 0)
  const oldRecordingTerminal = NativeRecordingInfoSchema.parse(
    await page.source.runAutomation('recording', {
      action: 'status',
      recordingId: oldRecording.id,
    }),
  )
  assert.equal(oldRecordingTerminal.phase, 'failed')
  await scene(page)
  const newPublisher = await createNativePublisher({
    source: page.source,
    ffmpegPath: '/usr/bin/ffmpeg',
    publisher: {
      destination: newTCP.url,
      container: 'webm',
      videoCodec: 'vp9',
      audioCodec: 'opus',
      audio: false,
      framesPerSecond: 29.97,
    },
  })
  const newRecording = NativeRecordingInfoSchema.parse(
    await page.source.runAutomation('recording', {
      action: 'start',
      ffmpegPath: '/usr/bin/ffmpeg',
      outputPath: join(directory, 'complete-after-recovery.webm'),
      framesPerSecond: 29.97,
      width: 160,
      height: 96,
      audio: false,
      timeoutMs: 5000,
    }),
  )
  await advance(page, 80)
  const completeRecording = NativeRecordingInfoSchema.parse(
    await page.source.runAutomation('recording', {
      action: 'stop',
      recordingId: newRecording.id,
      timeoutMs: 5000,
    }),
  )
  assert.equal(completeRecording.phase, 'complete')
  assert(completeRecording.outputFrames > 0)
  assert(completeRecording.outputBytes > 0)
  const completePublisher = await bounded(newPublisher.stop())
  assert.equal(completePublisher.phase, 'complete')
  assert.equal(completePublisher.failure, '')
  assert.equal(completePublisher.physicalEncoderPid, 0)
  const received = await newTCP.finish()
  assert(received.bytes > 0)
  const decode = promisify(execFile)
  for (const file of [completeRecording.outputPath, received.path]) {
    const decoded = await decode(
      '/usr/bin/ffmpeg',
      ['-v', 'error', '-xerror', '-i', file, '-f', 'null', '-'],
      { timeout: 10000, maxBuffer: 1024 * 1024 },
    )
    assert.equal(decoded.stderr, '')
  }
  await assert.rejects(oldPublisher.failure, /failed|renderer|closed/iu)
  assert.equal((await oldPublisher.status()).phase, 'failed')
  await context.close()
  const final = z
    .object({ nativeStreamingPhysical: z.literal(0), nativeRecordingPhysical: z.literal(0) })
    .parse(await source.runAutomation('diagnostics'))
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        oldStatus,
        oldTerminal,
        oldRecordingTerminal,
        completeRecording,
        completePublisher,
        received,
        actualCompletedFilesDecoded: true,
        final,
        actualCrash: await bounded(crash),
      },
      null,
      2,
    ) + '\n',
  )
  console.log(
    'Actual native renderer recovery preserves old media failure and admits new CEF recording/publishing',
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
  try {
    await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
  } finally {
    await oldTCP.close()
    await newTCP.close()
  }
}
