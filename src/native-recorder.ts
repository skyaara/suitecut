import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { rm } from 'node:fs/promises'
import { finished } from 'node:stream/promises'

import { raceWithAbort } from './abort.js'
import { createCaptureScaleFilter } from './capture.js'
import { FrameRateSchema, videoFramePosition } from './frame-rate.js'
import { type NativeBrowser } from './native-browser.js'
import { runProcess, waitForProcessExit } from './process.js'
import { type SuiteCutCaptureSize } from './schemas.js'

/** Bounded latest-frame encoder; repeats held frames to preserve wall-clock timing. */
export function createNativeVideoRecorder(options: {
  source: Pick<NativeBrowser, 'width' | 'height' | 'pixelFormat' | 'framesPerSecond'>
  framesPerSecond?: number | undefined
  ffmpegPath: string
  outputPath: string
  size: SuiteCutCaptureSize
  audio?: boolean
}) {
  const { source } = options
  const framesPerSecond = FrameRateSchema.parse(options.framesPerSecond ?? source.framesPerSecond)
  const videoOutputPath = options.audio ? `${options.outputPath}.video.webm` : options.outputPath
  const audioOutputPath = `${options.outputPath}.audio.pcm`
  const child = spawn(
    options.ffmpegPath,
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-f',
      'rawvideo',
      '-pixel_format',
      source.pixelFormat === 'bgra' ? 'bgra' : 'yuv420p',
      '-video_size',
      `${source.width}x${source.height}`,
      '-framerate',
      String(framesPerSecond),
      '-i',
      'pipe:0',
      '-an',
      '-vf',
      createCaptureScaleFilter(source, options.size),
      '-c:v',
      'libvpx-vp9',
      '-crf',
      '18',
      '-b:v',
      '0',
      '-deadline',
      'realtime',
      '-cpu-used',
      '5',
      '-row-mt',
      '1',
      '-threads',
      '4',
      '-pix_fmt',
      'yuv420p',
      ...(source.pixelFormat === 'i420'
        ? [
            '-colorspace',
            'bt709',
            '-color_primaries',
            'bt709',
            '-color_trc',
            'iec61966-2-1',
            '-color_range',
            'tv',
          ]
        : []),
      '-y',
      videoOutputPath,
    ],
    { shell: false, stdio: ['pipe', 'ignore', 'pipe'] },
  )
  let diagnostics = ''
  child.stderr.on('data', (chunk: Buffer) => {
    diagnostics = (diagnostics + chunk.toString('utf8')).slice(-8192)
  })
  let rejectFailure: (error: Error) => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  const failureController = new AbortController()
  let failed: Error | undefined
  const fail = (error: Error) => {
    failed ??= error
    failureController.abort(error)
    rejectFailure(error)
  }
  let ending = false
  const completion = new Promise<void>((resolve, reject) => {
    child.once('error', (error) => {
      fail(error)
      reject(error)
    })
    child.once('close', (code) => {
      if (code === 0 && ending) resolve()
      else {
        const error = new Error(`Native recording encoder exited (${String(code)}): ${diagnostics}`)
        fail(error)
        reject(error)
      }
    })
  })
  void completion.catch(() => undefined)
  child.stdin.on('error', fail)
  let pending: { data: Buffer; atMs: number } | undefined
  let last: Buffer | undefined
  let firstAtMs: number | undefined
  let latestAtMs = -Infinity
  let lastNumber = -1
  let work: Promise<void> | undefined
  const audioOutput = options.audio ? createWriteStream(audioOutputPath, { flags: 'w' }) : undefined
  const audioPackets: { data: Buffer; atMs: number }[] = []
  let audioPacketBytes = 0
  let audioFrame = 0
  let audioWork: Promise<void> | undefined
  let stopping: Promise<void> | undefined
  const write = async (data: Buffer) => {
    if (failed) throw failed
    if (!child.stdin.write(data))
      await once(child.stdin, 'drain', { signal: failureController.signal })
  }
  const drain = async () => {
    while (pending) {
      const frame = pending
      pending = undefined
      if (firstAtMs === undefined) {
        firstAtMs = frame.atMs
        startAudioWork()
      }
      // A tick sees the newest captured frame at or before its timestamp.
      // Frames between ticks update the held image, never an earlier output tick.
      const position = videoFramePosition(frame.atMs - firstAtMs, framesPerSecond, firstAtMs)
      const number = Math.ceil(position)
      while (last && lastNumber + 1 < number) {
        await write(last)
        lastNumber++
      }
      // Keep boundary frames until the next capture or stop establishes their duration.
      if (lastNumber < 0) {
        await write(frame.data)
        lastNumber = number
      }
      last = frame.data
    }
  }
  const startWork = () => {
    work ??= drain()
      .catch((error: Error) => {
        fail(error)
      })
      .finally(() => {
        work = undefined
        if (pending && !failed) startWork()
      })
  }
  const push = (data: Buffer, atMs: number): Promise<void> | void => {
    if (ending || failed) return
    if (!Number.isFinite(atMs) || atMs < 0) {
      fail(new Error('Native recording received an invalid capture timestamp'))
      return
    }
    if (atMs < latestAtMs) return // A delayed capture cannot replace a newer held frame.
    latestAtMs = atMs
    pending = { data, atMs }
    startWork()
    return work
  }
  const writeAudio = async (data: Buffer): Promise<void> => {
    if (!audioOutput) return
    if (failed) throw failed
    if (!audioOutput.write(data))
      await once(audioOutput, 'drain', { signal: failureController.signal })
  }
  const writeSilence = async (frames: number): Promise<void> => {
    const silence = Buffer.alloc(3_840)
    let remaining = frames
    while (remaining > 0) {
      const chunkFrames = Math.min(960, remaining)
      await writeAudio(chunkFrames === 960 ? silence : silence.subarray(0, chunkFrames * 4))
      remaining -= chunkFrames
    }
  }
  const drainAudio = async (): Promise<void> => {
    if (firstAtMs === undefined) return
    while (audioPackets.length > 0) {
      const packet = audioPackets.shift()
      if (!packet) break
      audioPacketBytes -= packet.data.length
      const packetFrames = packet.data.length / 4
      const packetStartFrame = Math.round((packet.atMs - firstAtMs) * 48)
      const overlapFrames = Math.max(0, audioFrame - packetStartFrame)
      if (overlapFrames >= packetFrames) continue
      if (packetStartFrame > audioFrame) {
        await writeSilence(packetStartFrame - audioFrame)
        audioFrame = packetStartFrame
      }
      const data = packet.data.subarray(overlapFrames * 4)
      await writeAudio(data)
      audioFrame += data.length / 4
    }
  }
  const startAudioWork = (): void => {
    if (!audioOutput || firstAtMs === undefined) return
    audioWork ??= drainAudio()
      .catch((error: Error) => fail(error))
      .finally(() => {
        audioWork = undefined
        if (audioPackets.length > 0 && !failed) startAudioWork()
      })
  }
  audioOutput?.once('error', fail)
  const pushAudio = (data: Buffer, atMs: number): void => {
    if (!audioOutput || ending || failed) return
    if (data.length === 0 || data.length % 4 !== 0 || !Number.isFinite(atMs)) {
      fail(new Error('Native recording received malformed stereo PCM'))
      return
    }
    audioPackets.push({ data, atMs })
    audioPacketBytes += data.length
    if (audioPacketBytes > 48_000 * 4 * 30) {
      fail(new Error('Native recording audio queue exceeded 30 seconds'))
      return
    }
    startAudioWork()
  }
  return {
    failure,
    push,
    pushAudio,
    get firstAtMs() {
      return firstAtMs
    },
    stop(atMs: number): Promise<void> {
      stopping ??= (async () => {
        ending = true
        startWork()
        const deadline = AbortSignal.timeout(60_000)
        try {
          while (work || pending) {
            if (!work) startWork()
            await raceWithAbort(Promise.race([work, failure]), deadline)
          }
          // Emit slots in [firstAtMs, atMs); do not append a frame at the end boundary.
          if (firstAtMs !== undefined && last) {
            const count = Math.max(
              1,
              Math.ceil(
                videoFramePosition(Math.max(0, atMs - firstAtMs), framesPerSecond, firstAtMs),
              ),
            )
            while (lastNumber + 1 < count) {
              await raceWithAbort(write(last), deadline)
              lastNumber++
            }
          }
          startAudioWork()
          while (audioWork || audioPackets.length > 0) {
            if (!audioWork) startAudioWork()
            await raceWithAbort(Promise.race([audioWork, failure]), deadline)
          }
          if (audioOutput && firstAtMs !== undefined) {
            const targetAudioFrame = Math.max(
              audioFrame,
              Math.ceil(Math.max(0, atMs - firstAtMs) * 48),
            )
            await writeSilence(targetAudioFrame - audioFrame)
            audioFrame = targetAudioFrame
            audioOutput.end()
            await raceWithAbort(finished(audioOutput), deadline)
          }
          child.stdin.end()
          await raceWithAbort(completion, deadline)
          if (firstAtMs === undefined) throw new Error('Native recording received no video frames')
          if (audioOutput) {
            const mux = await runProcess(
              options.ffmpegPath,
              [
                '-hide_banner',
                '-loglevel',
                'error',
                '-i',
                videoOutputPath,
                '-f',
                's16le',
                '-ar',
                '48000',
                '-ac',
                '2',
                '-i',
                audioOutputPath,
                '-map',
                '0:v:0',
                '-map',
                '1:a:0',
                '-c:v',
                'copy',
                '-c:a',
                'libopus',
                '-b:a',
                '160k',
                '-t',
                ((atMs - firstAtMs) / 1_000).toFixed(6),
                '-y',
                options.outputPath,
              ],
              { signal: deadline },
            )
            if (mux.exitCode !== 0)
              throw new Error(`Native recording audio mux failed: ${mux.stderr.slice(-2_000)}`)
            await Promise.all([
              rm(videoOutputPath, { force: true }),
              rm(audioOutputPath, { force: true }),
            ])
          }
        } catch (error) {
          child.stdin.destroy()
          audioOutput?.destroy()
          if (child.exitCode === null && child.signalCode === null) child.kill('SIGTERM')
          await waitForProcessExit(child, completion).catch(() => undefined)
          if (options.audio)
            await Promise.all([
              rm(videoOutputPath, { force: true }),
              rm(audioOutputPath, { force: true }),
            ])
          throw error
        }
      })()
      return stopping
    },
  }
}
