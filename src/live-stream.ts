import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { setTimeout as delay } from 'node:timers/promises'

import { raceWithAbort } from './abort.js'
import { waitForProcessExit } from './process.js'
import { type SuiteCutCaptureSize, type SuiteCutStreamOptions } from './schemas.js'

export interface SuiteCutLiveStream {
  readonly failure: Promise<never>
  ready(): Promise<void>
  update(frame: Buffer): void
  stop(): Promise<void>
}

/** Publishes the latest selected page frame on a continuous wall-clock timeline. */
export function createLiveStreamAttempt(
  ffmpegPath: string,
  options: SuiteCutStreamOptions,
  framesPerSecond: number,
  fallbackSize: SuiteCutCaptureSize,
  signal?: AbortSignal,
  processFrame?: (data: Buffer) => Promise<Buffer>,
): SuiteCutLiveStream {
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
      '-thread_queue_size',
      '16',
      '-f',
      'lavfi',
      '-i',
      'anullsrc=r=48000:cl=stereo',
      '-map',
      '0:v:0',
      '-map',
      '1:a:0',
      '-vf',
      `scale=${size.width}:${size.height}:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=${size.width}:${size.height}:(ow-iw)/2:(oh-ih)/2,setsar=1`,
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
      '-c:a',
      'aac',
      '-b:a',
      '128k',
      '-shortest',
      '-rw_timeout',
      '5000000',
      '-f',
      'flv',
      '-flvflags',
      'no_duration_filesize',
      options.url,
    ],
    { shell: false, stdio: ['pipe', 'ignore', 'pipe', 'pipe'] },
  )

  let latestFrame: Buffer | undefined
  let timer: ReturnType<typeof setTimeout> | undefined
  let closing = false
  let failure: Error | undefined
  let stopping: Promise<void> | undefined
  let writing = Promise.resolve()
  let firstWriteAt: number | undefined
  let writtenFrames = 0
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
    const lines = progress.split('\n')
    progress = lines.pop() ?? ''
    for (const line of lines) {
      if (/^frame=\s*[1-9][0-9]*\s*$/u.test(line)) markReady()
    }
  })

  const fail = (message: string): void => {
    failure ??= new Error(message)
    rejectFailure(failure)
    if (timer !== undefined) clearTimeout(timer)
    ioController.abort(failure)
    child.kill('SIGTERM')
  }
  const completion = new Promise<void>((resolve) => {
    child.once('error', () => {
      fail('SuiteCut could not launch the live encoder. Check the FFmpeg installation.')
      resolve()
    })
    child.once('close', (code) => {
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
    if (timer !== undefined) clearTimeout(timer)
    ioController.abort()
    child.stdin?.end()
  }
  signal?.addEventListener('abort', onAbort, { once: true })
  if (signal?.aborted === true) onAbort()

  const tick = async (): Promise<void> => {
    if (closing || failure !== undefined || latestFrame === undefined) return
    const stdin = child.stdin
    if (stdin === null) throw new Error('SuiteCut live encoder has no frame input.')
    firstWriteAt ??= performance.now()
    const frame = processFrame ? await processFrame(latestFrame) : latestFrame
    if (closing || failure !== undefined) return
    if (!stdin.write(frame)) {
      await once(stdin, 'drain', {
        signal: AbortSignal.any([ioController.signal, AbortSignal.timeout(5000)]),
      })
    }
    writtenFrames += 1
    const nextFrameAt = firstWriteAt + (writtenFrames * 1000) / framesPerSecond
    if (performance.now() - nextFrameAt > 1000) {
      throw new Error('SuiteCut live encoder cannot keep up. Reduce the stream size or bitrate.')
    }
    if (!closing && failure === undefined) {
      timer = setTimeout(schedule, Math.max(0, nextFrameAt - performance.now()))
    }
  }
  const schedule = (): void => {
    writing = tick().catch(() => {
      if (!closing)
        fail(
          'SuiteCut live encoder stalled. Check the connection or reduce the stream size or bitrate.',
        )
    })
  }

  return {
    failure: failed,
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
      if (closing || failure !== undefined) return
      latestFrame = frame
      if (firstWriteAt === undefined) schedule()
    },
    stop: () => {
      stopping ??= (async () => {
        closing = true
        if (timer !== undefined) clearTimeout(timer)
        signal?.removeEventListener('abort', onAbort)
        ioController.abort()
        await writing
        latestFrame = undefined
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
): SuiteCutLiveStream {
  if (options.reconnect === false)
    return createLiveStreamAttempt(
      ffmpegPath,
      options,
      framesPerSecond,
      fallbackSize,
      signal,
      processFrame,
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
