import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { setTimeout as delay } from 'node:timers/promises'

import { raceWithAbort } from './abort.js'
import { createCaptureScaleFilter } from './capture.js'
import { LIVE_AUDIO_DELAY_MS, type LiveAudioTransport } from './live-audio.js'
import {
  LiveCongestion,
  LIVE_RECOVERY_WINDOW_MS,
  LIVE_FRAME_BUFFER_BYTES,
  LIVE_NO_PROGRESS_MS,
} from './live-congestion.js'
import { createPersistentLiveStream } from './live-publisher.js'
import { waitForProcessExit } from './process.js'
import {
  type SuiteCutCaptureSize,
  type SuiteCutCaptureViewport,
  type SuiteCutStreamOptions,
} from './schemas.js'

export interface SuiteCutLiveStream {
  readonly failure: Promise<never>
  health(): { captureLagMs: number; outputLagMs: number; progressAgeMs: number }
  setBitrate?(bitrateKbps: number): Promise<void>
  ready(): Promise<void>
  update(frame: Buffer): void
  stop(): Promise<void>
}

export interface LiveEncodedOutput {
  health(): {
    outputLagMs: number
    progressAgeMs: number
    networkBlockedMs: number
    networkQueuedBytes: number
  }
  data(chunk: Buffer): Promise<void>
  frame(index: number, captureAt: number): void
}

/** Publishes the latest selected page frame on a continuous wall-clock timeline. */
export function createLiveStreamAttempt(
  ffmpegPath: string,
  options: SuiteCutStreamOptions,
  framesPerSecond: number,
  fallbackSize: SuiteCutCaptureSize,
  signal?: AbortSignal,
  processFrame?: (data: Buffer) => Promise<Buffer>,
  audio?: LiveAudioTransport,
  networkUrl?: string,
  networkPressure?: () => { networkBlockedMs: number; networkQueuedBytes: number },
  encodedOutput?: LiveEncodedOutput,
  inputSize?: SuiteCutCaptureViewport,
): SuiteCutLiveStream {
  const audioInput = encodedOutput ? undefined : audio?.createInput()
  const frames: { at: number; data: Buffer }[] = []
  let frameBytes = 0
  const congestion = new LiveCongestion(options.onDiagnostic)
  const size = options.size ?? fallbackSize
  const bitrate = options.bitrateKbps ?? 4500
  const child = spawn(
    ffmpegPath,
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-stats_period',
      '0.1',
      '-progress',
      'pipe:3',
      '-thread_queue_size',
      '16',
      '-f',
      'image2pipe',
      '-framerate',
      String(framesPerSecond),
      '-vcodec',
      'mjpeg',
      '-probesize',
      '32',
      '-analyzeduration',
      '0',
      '-i',
      'pipe:0',
      ...(encodedOutput
        ? []
        : [
            '-thread_queue_size',
            '16',
            ...(audioInput
              ? [
                  '-f',
                  's16le',
                  '-ar',
                  '48000',
                  '-ac',
                  '2',
                  '-probesize',
                  '32',
                  '-analyzeduration',
                  '0',
                  '-i',
                  audioInput.url,
                ]
              : ['-f', 'lavfi', '-i', 'anullsrc=r=48000:cl=stereo']),
          ]),
      '-map',
      '0:v:0',
      ...(encodedOutput ? ['-an'] : ['-map', '1:a:0']),
      '-vf',
      inputSize === undefined
        ? `scale=${size.width}:${size.height}:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=${size.width}:${size.height}:(ow-iw)/2:(oh-ih)/2,setsar=1`
        : createCaptureScaleFilter(inputSize, size),
      '-c:v',
      'libx264',
      '-preset',
      'veryfast',
      '-tune',
      'zerolatency',
      '-pix_fmt',
      'yuv420p',
      '-r',
      String(framesPerSecond),
      '-g',
      String(framesPerSecond * 2),
      '-keyint_min',
      String(framesPerSecond * 2),
      '-sc_threshold',
      '0',
      '-b:v',
      `${bitrate}k`,
      '-maxrate',
      `${bitrate}k`,
      '-bufsize',
      `${bitrate * 2}k`,
      ...(encodedOutput ? [] : ['-c:a', 'aac', '-b:a', '128k', '-shortest']),
      '-rw_timeout',
      '20000000',
      '-f',
      'flv',
      '-flvflags',
      'no_duration_filesize',
      ...(networkUrl ? ['-rtmp_tcurl', options.url.slice(0, options.url.lastIndexOf('/'))] : []),
      encodedOutput ? 'pipe:1' : (networkUrl ?? options.url),
    ],
    { shell: false, stdio: ['pipe', encodedOutput ? 'pipe' : 'ignore', 'pipe', 'pipe'] },
  )

  let latestFrame: Buffer | undefined
  let timer: ReturnType<typeof setTimeout> | undefined
  let closing = false
  let failure: Error | undefined
  let stopping: Promise<void> | undefined
  let writing = Promise.resolve()
  let firstWriteAt: number | undefined
  let writtenFrames = 0
  let hasPublished = false
  let progressedAt = performance.now()
  let outputFrames = 0
  let pendingStage: 'processing' | 'writing' | undefined
  let pendingAt = 0
  let videoPending = false
  let audioPending = false
  const health = (): { captureLagMs: number; outputLagMs: number; progressAgeMs: number } => ({
    captureLagMs:
      firstWriteAt === undefined
        ? 0
        : Math.max(0, performance.now() - firstWriteAt - (writtenFrames * 1000) / framesPerSecond),
    outputLagMs: Math.max(0, ((writtenFrames - outputFrames) * 1000) / framesPerSecond),
    progressAgeMs: performance.now() - progressedAt,
  })
  const ioController = new AbortController()
  let markReady = (): void => undefined
  let rejectFailure = (error: Error): void => {
    void error
  }
  const failed = new Promise<never>((_resolve, reject) => {
    rejectFailure = reject
  })
  void failed.catch(() => undefined)
  const published = new Promise<void>((resolve) => {
    markReady = resolve
  })
  // FFmpeg diagnostics can contain stream keys, including fragments of a URL.
  // Never forward its stderr or spawn error arguments into saved reports.
  child.stderr?.resume()
  let progress = ''
  child.stdio[3]?.on('data', (chunk: Buffer) => {
    progress += chunk.toString('utf8')
    if (progress.length > 16_384) progress = progress.slice(-16_384)
    const lines = progress.split('\n')
    progress = lines.pop() ?? ''
    for (const line of lines) {
      if (/^frame=\s*[1-9][0-9]*\s*$/u.test(line)) {
        const count = Number(line.slice(line.indexOf('=') + 1))
        if (count > outputFrames) {
          outputFrames = count
          progressedAt = performance.now()
        }
        if (!hasPublished) {
          // Connection setup is not encoder drift. Start pacing at the current
          // capture timeline once the first frames reach the output.
          hasPublished = true
          firstWriteAt = performance.now() - (writtenFrames * 1000) / framesPerSecond
        }
        markReady()
      }
    }
  })

  const fail = (message: string): void => {
    failure ??= new Error(message)
    rejectFailure(failure)
    if (timer !== undefined) clearTimeout(timer)
    ioController.abort(failure)
    child.kill('SIGTERM')
  }
  if (encodedOutput && child.stdout) {
    const output = child.stdout
    output.on('data', (chunk: Buffer) => {
      output.pause()
      void encodedOutput.data(chunk).then(
        () => output.resume(),
        () => {
          if (!closing) fail('SuiteCut live encoded output failed.')
        },
      )
    })
  }
  const watchdog = setInterval(() => {
    if (closing || failure !== undefined || !hasPublished) return
    const now = performance.now()
    if (videoPending) congestion.measure({ videoWriteMs: now - pendingAt })
    if (audioPending) congestion.measure({ audioWriteMs: now - pendingAt })
    congestion.measure({
      ...networkPressure?.(),
      outputLagMs: health().outputLagMs,
      progressAgeMs: now - progressedAt,
      ...encodedOutput?.health(),
    })
    if (now - progressedAt < LIVE_NO_PROGRESS_MS) return
    if (pendingStage === 'processing') congestion.measure({ processingMs: now - pendingAt })
    congestion.stalled(
      firstWriteAt === undefined
        ? 0
        : Math.max(0, now - firstWriteAt - (writtenFrames * 1000) / framesPerSecond),
    )
    fail('SuiteCut live encoder made no frame progress for 20 seconds.')
  }, 1000)
  watchdog.unref()
  const completion = new Promise<void>((resolve) => {
    child.once('error', () => {
      fail('SuiteCut could not launch the live encoder. Check the FFmpeg installation.')
      resolve()
    })
    child.once('close', (code) => {
      clearInterval(watchdog)
      if (!closing || code !== 0) {
        fail(
          'SuiteCut live stream disconnected or failed. Check the publish URL, connection, and FFmpeg H.264/AAC support.',
        )
      }
      resolve()
    })
  })
  child.stdin?.on('error', () => {
    if (!closing) fail('SuiteCut live encoder stopped accepting frames.')
  })
  const onAbort = (): void => {
    closing = true
    clearInterval(watchdog)
    if (timer !== undefined) clearTimeout(timer)
    ioController.abort()
    audioInput?.close()
    child.stdin?.end()
  }
  signal?.addEventListener('abort', onAbort, { once: true })
  if (signal?.aborted === true) onAbort()

  const tick = async (): Promise<void> => {
    if (closing || failure !== undefined || latestFrame === undefined) return
    const stdin = child.stdin
    if (stdin === null) throw new Error('SuiteCut live encoder has no frame input.')
    firstWriteAt ??= performance.now()
    const scheduledAt = firstWriteAt + (writtenFrames * 1000) / framesPerSecond
    const lag = Math.max(0, performance.now() - scheduledAt)
    // If a byte cap evicted required video, skip its corresponding audio too.
    const missingVideo =
      audio !== undefined && frames.length > 0 && (frames[0]?.at ?? 0) > scheduledAt + 250
    congestion.measure({
      ...networkPressure?.(),
      outputLagMs: health().outputLagMs,
      progressAgeMs: health().progressAgeMs,
      ...encodedOutput?.health(),
    })
    firstWriteAt += congestion.update(performance.now(), lag, missingVideo)
    const mediaAt =
      firstWriteAt + (writtenFrames * 1000) / framesPerSecond - (audio ? LIVE_AUDIO_DELAY_MS : 0)
    let selectedFrame = latestFrame
    if (audio) {
      while (frames.length > 1 && (frames[1]?.at ?? Infinity) <= mediaAt)
        frameBytes -= frames.shift()?.data.length ?? 0
      selectedFrame = frames[0]?.data ?? latestFrame
    }
    pendingStage = 'processing'
    pendingAt = performance.now()
    const frame = processFrame
      ? await raceWithAbort(processFrame(selectedFrame), ioController.signal)
      : selectedFrame
    const processingMs = performance.now() - pendingAt
    congestion.measure({ processingMs, videoWriteMs: 0, audioWriteMs: 0 })
    pendingStage = undefined
    if (closing || failure !== undefined) return
    // Do not spend more work encoding a frame that became obsolete while processing.
    if (processingMs >= LIVE_RECOVERY_WINDOW_MS) {
      firstWriteAt += congestion.update(
        performance.now(),
        performance.now() - firstWriteAt - (writtenFrames * 1000) / framesPerSecond,
        true,
      )
      timer = setTimeout(schedule, 0)
      return
    }
    const writeSignal = AbortSignal.any([
      ioController.signal,
      AbortSignal.timeout(LIVE_NO_PROGRESS_MS),
    ])
    encodedOutput?.frame(writtenFrames, mediaAt)
    videoPending = true
    audioPending = audioInput !== undefined
    pendingStage = 'writing'
    pendingAt = performance.now()
    await Promise.all([
      (async () => {
        try {
          if (!stdin.write(frame)) await once(stdin, 'drain', { signal: writeSignal })
        } finally {
          videoPending = false
          congestion.measure({ videoWriteMs: performance.now() - pendingAt })
        }
      })(),
      (async () => {
        try {
          await audioInput?.write(mediaAt, 48000 / framesPerSecond, writeSignal)
        } finally {
          audioPending = false
          congestion.measure({ audioWriteMs: performance.now() - pendingAt })
        }
      })(),
    ])
    pendingStage = undefined
    writtenFrames += 1
    const nextFrameAt = firstWriteAt + (writtenFrames * 1000) / framesPerSecond

    if (!closing && failure === undefined) {
      timer = setTimeout(schedule, Math.max(0, nextFrameAt - performance.now()))
    }
  }
  const schedule = (): void => {
    writing = tick().catch(() => {
      if (!closing) {
        congestion.stalled(
          firstWriteAt === undefined
            ? 0
            : Math.max(
                0,
                performance.now() - firstWriteAt - (writtenFrames * 1000) / framesPerSecond,
              ),
        )
        fail('SuiteCut live input failed or remained blocked for 20 seconds.')
      }
    })
  }

  return {
    failure: failed,
    health,
    ready: async () => {
      await raceWithAbort(
        Promise.race([
          published,
          completion.then(() => {
            throw failure ?? new Error('SuiteCut live encoder exited before publishing.')
          }),
        ]),
        AbortSignal.any([
          ioController.signal,
          AbortSignal.timeout(15_000),
          ...(signal === undefined ? [] : [signal]),
        ]),
      )
      if (failure !== undefined) throw failure
    },
    update: (frame) => {
      if (closing || failure !== undefined || frame.length > LIVE_FRAME_BUFFER_BYTES) return
      latestFrame = frame
      if (audio) {
        const now = performance.now()
        frames.push({ at: now, data: frame })
        frameBytes += frame.length
        while (
          frames.length > 1 &&
          (frames.length > 610 ||
            frameBytes > LIVE_FRAME_BUFFER_BYTES ||
            (frames[0]?.at ?? now) < now - LIVE_RECOVERY_WINDOW_MS - LIVE_AUDIO_DELAY_MS)
        )
          frameBytes -= frames.shift()?.data.length ?? 0
      }
      if (firstWriteAt === undefined) schedule()
    },
    stop: () => {
      stopping ??= (async () => {
        closing = true
        clearInterval(watchdog)
        if (timer !== undefined) clearTimeout(timer)
        signal?.removeEventListener('abort', onAbort)
        ioController.abort()
        await writing
        latestFrame = undefined
        frames.length = 0
        frameBytes = 0
        audioInput?.close()
        child.stdin?.end()
        await waitForProcessExit(child, completion, {
          gracefulTimeoutMs: 5000,
          forceKillAfterMs: 2000,
        })
        if (failure !== undefined) throw failure
      })()
      return stopping
    },
  }
}

/** Keeps the browser running while replacing disconnected publishing processes. */
export function createLiveStream(
  ffmpegPath: string,
  options: SuiteCutStreamOptions,
  framesPerSecond: number,
  fallbackSize: SuiteCutCaptureSize,
  signal?: AbortSignal,
  processFrame?: (data: Buffer) => Promise<Buffer>,
  audio?: LiveAudioTransport,
  inputSize?: SuiteCutCaptureViewport,
): SuiteCutLiveStream {
  if (options.adaptiveBitrate !== false)
    return createPersistentLiveStream(
      ffmpegPath,
      options,
      framesPerSecond,
      fallbackSize,
      signal,
      audio,
      (videoOptions, output, attemptSignal) =>
        createLiveStreamAttempt(
          ffmpegPath,
          videoOptions,
          framesPerSecond,
          fallbackSize,
          attemptSignal,
          processFrame,
          audio,
          undefined,
          undefined,
          output,
          inputSize,
        ),
    )
  if (options.reconnect === false)
    return createLiveStreamAttempt(
      ffmpegPath,
      options,
      framesPerSecond,
      fallbackSize,
      signal,
      processFrame,
      audio,
      undefined,
      undefined,
      undefined,
      inputSize,
    )
  const reconnect = options.reconnect ?? {}
  const controller = new AbortController()
  const combined = signal ? AbortSignal.any([signal, controller.signal]) : controller.signal
  let current: SuiteCutLiveStream | undefined
  let latest: Buffer | undefined
  let stopping: Promise<void> | undefined
  let resolveReady = (): void => undefined
  const firstReady = new Promise<void>((resolve) => {
    resolveReady = resolve
  })
  let terminalFailure: Error | undefined
  let consecutiveFailures = 0
  const failure = (async (): Promise<never> => {
    try {
      while (!combined.aborted) {
        const attempt = createLiveStreamAttempt(
          ffmpegPath,
          options,
          framesPerSecond,
          fallbackSize,
          combined,
          processFrame,
          audio,
          undefined,
          undefined,
          undefined,
          inputSize,
        )
        current = attempt
        if (latest) attempt.update(latest)
        const started = performance.now()
        try {
          await attempt.ready()
          resolveReady()
          await raceWithAbort(attempt.failure, combined)
        } catch (error) {
          await attempt.stop().catch(() => undefined)
          current = undefined
          if (combined.aborted) break
          // Brief connections must not reset the retry budget and cause rapid flapping.
          if (performance.now() - started >= 30_000) consecutiveFailures = 0
          consecutiveFailures += 1
          const maximum = reconnect.maxAttempts ?? 0
          if (maximum > 0 && consecutiveFailures > maximum) throw error
          const backoff = Math.min(
            reconnect.maxDelayMs ?? 30_000,
            (reconnect.initialDelayMs ?? 1000) * 2 ** Math.min(consecutiveFailures - 1, 20),
          )
          await delay(backoff, undefined, { signal: combined })
        }
      }
    } catch (error) {
      if (!combined.aborted) {
        terminalFailure = error instanceof Error ? error : new Error('SuiteCut live stream failed.')
        throw terminalFailure
      }
    }
    // A deliberate stop is not a terminal publishing failure.
    return await new Promise<never>(() => undefined)
  })()
  void failure.catch(() => undefined)
  return {
    failure,
    health: () => current?.health() ?? { captureLagMs: 0, outputLagMs: 0, progressAgeMs: 0 },
    ready: () => raceWithAbort(Promise.race([firstReady, failure]), combined),
    update: (frame) => {
      if (combined.aborted) return
      latest = frame
      current?.update(frame)
    },
    stop: () => {
      stopping ??= (async () => {
        controller.abort()
        latest = undefined
        await current?.stop().catch(() => undefined)
        current = undefined
        if (terminalFailure !== undefined) throw terminalFailure
      })()
      return stopping
    },
  }
}
