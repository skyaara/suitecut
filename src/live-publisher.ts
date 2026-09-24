import { spawn, type ChildProcess } from 'node:child_process'
import { once } from 'node:events'
import { setTimeout as delay } from 'node:timers/promises'

import { raceWithAbort } from './abort.js'
import { LIVE_AUDIO_DELAY_MS, type LiveAudioTransport } from './live-audio.js'
import { LiveBitrate } from './live-bitrate.js'
import { type LiveStreamDiagnostic } from './live-congestion.js'
import { LIVE_FLV_HEADER, LiveFlvParser, flvTimestamp, stampFlv } from './live-flv.js'
import { createLiveNetwork, type NetworkSample } from './live-network.js'
import { type LiveEncodedOutput, type SuiteCutLiveStream } from './live-stream.js'
import { waitForProcessExit } from './process.js'
import { type SuiteCutCaptureSize, type SuiteCutStreamOptions } from './schemas.js'

type VideoFactory = (
  options: SuiteCutStreamOptions,
  output: LiveEncodedOutput,
  signal: AbortSignal,
) => SuiteCutLiveStream

/** Audio and publisher survive video encoder handoffs. No RTMP handshake occurs on bitrate changes. */
export function createPersistentLiveStream(
  ffmpeg: string,
  options: SuiteCutStreamOptions,
  fps: number,
  _size: SuiteCutCaptureSize,
  signal: AbortSignal | undefined,
  audio: LiveAudioTransport | undefined,
  makeVideo: VideoFactory,
): SuiteCutLiveStream {
  const controller = new AbortController()
  const combined = signal ? AbortSignal.any([controller.signal, signal]) : controller.signal
  const bitrate = new LiveBitrate(options.bitrateKbps ?? 4500)
  let latest: Buffer | undefined
  let latestTimestamp: number | undefined
  let video: SuiteCutLiveStream | undefined
  let warming: SuiteCutLiveStream | undefined
  let change: ((value: number, sample?: NetworkSample) => Promise<void>) | undefined
  let stopping: Promise<void> | undefined
  let finishAttempt: (() => Promise<void>) | undefined
  let markReady = (): void => undefined
  const ready = new Promise<void>((resolve) => {
    markReady = resolve
  })
  let delivery = () => ({
    outputLagMs: 0,
    progressAgeMs: 0,
    networkBlockedMs: 0,
    networkQueuedBytes: 0,
  })
  let terminal: Error | undefined
  const report = (event: LiveStreamDiagnostic): void => {
    try {
      void Promise.resolve(options.onDiagnostic?.(event)).catch(() => undefined)
    } catch {
      /* isolate consumers */
    }
  }
  const bitrateEvent = (
    event: 'bitrate-changing' | 'bitrate-adjusted' | 'bitrate-change-failed',
    previous: number,
    next: number,
    sample?: NetworkSample,
  ): void =>
    report({
      event,
      bottleneck: sample ? 'network-backpressure' : 'unknown',
      lagMs: video?.health().captureLagMs ?? 0,
      processingMs: 0,
      videoWriteMs: 0,
      audioWriteMs: 0,
      skippedMs: 0,
      incidents: 0,
      previousBitrateKbps: previous,
      bitrateKbps: next,
      ...delivery(),
      ...(sample ? { networkBlockedMs: sample.blockedMs, sentKbps: sample.sentKbps } : {}),
    })
  const failure = (async (): Promise<never> => {
    let failures = 0
    while (!combined.aborted) {
      const local = new AbortController()
      const activeSignal = AbortSignal.any([combined, local.signal])
      const children: { child: ChildProcess; done: Promise<void> }[] = []
      const network = await createLiveNetwork(options.url)
      const origin = performance.now() - LIVE_AUDIO_DELAY_MS
      let audioTimer: ReturnType<typeof setTimeout> | undefined
      let monitor: ReturnType<typeof setInterval> | undefined
      let audioWriting = Promise.resolve()
      let writing = Promise.resolve()
      let changing = false
      let activeId = 0
      let nextId = 0
      const retiring = new Set<Promise<void>>()
      let lastVideoTime = -1
      let pendingHandoffReady: (() => void) | undefined
      let outputAt = performance.now()
      let outputFrames = 0
      let submittedFrames = 0
      delivery = () => ({
        outputLagMs: Math.max(0, ((submittedFrames - outputFrames) * 1000) / fps),
        progressAgeMs: performance.now() - outputAt,
        ...network.pressure(),
      })
      let published = false
      let rejectAttempt: (error: Error) => void = () => undefined
      const attemptFailure = new Promise<never>((_resolve, reject) => {
        rejectAttempt = reject
      })
      void attemptFailure.catch(() => undefined)
      const fail = (): void =>
        rejectAttempt(new Error('SuiteCut live publishing pipeline failed or stalled.'))
      const track = (child: ChildProcess): ChildProcess => {
        const done = new Promise<void>((resolve) => {
          child.once('error', () => {
            if (!activeSignal.aborted) fail()
            resolve()
          })
          child.once('close', () => {
            if (!activeSignal.aborted) fail()
            resolve()
          })
        })
        child.stdin?.on('error', () => {
          if (!activeSignal.aborted) fail()
        })
        child.stderr?.resume()
        children.push({ child, done })
        return child
      }
      let cleaned: Promise<void> | undefined
      finishAttempt = () =>
        (cleaned ??= (async () => {
          local.abort()
          clearTimeout(audioTimer)
          clearInterval(monitor)
          change = undefined
          pendingHandoffReady = undefined
          await network.close()
          await Promise.all([
            video?.stop().catch(() => undefined),
            warming?.stop().catch(() => undefined),
          ])
          await Promise.all(retiring)
          video = undefined
          warming = undefined
          await audioWriting.catch(() => undefined)
          await writing.catch(() => undefined)
          for (const { child } of children) child.stdin?.end()
          await Promise.all(
            children.map(({ child, done }) =>
              waitForProcessExit(child, done, { gracefulTimeoutMs: 1000, forceKillAfterMs: 1000 }),
            ),
          )
        })())
      const started = performance.now()
      try {
        if (activeSignal.aborted) break
        audio?.buffer.clear()
        bitrate.resetConnection()
        const publisher = track(
          spawn(
            ffmpeg,
            [
              '-hide_banner',
              '-loglevel',
              'error',
              '-stats_period',
              '0.1',
              '-progress',
              'pipe:3',
              '-f',
              'flv',
              '-probesize',
              '1000000',
              '-analyzeduration',
              '1000000',
              '-i',
              'pipe:0',
              '-map',
              '0:v:0',
              '-map',
              '0:a:0',
              '-c',
              'copy',
              '-max_interleave_delta',
              '100000',
              '-rw_timeout',
              '20000000',
              '-rtmp_tcurl',
              options.url.slice(0, options.url.lastIndexOf('/')),
              '-f',
              'flv',
              '-flvflags',
              'no_duration_filesize',
              network.url,
            ],
            { stdio: ['pipe', 'ignore', 'pipe', 'pipe'] },
          ),
        )
        let progress = ''
        publisher.stdio[3]?.on('data', (chunk: Buffer) => {
          progress = (progress + chunk.toString()).slice(-16384)
          const lines = progress.split('\n')
          progress = lines.pop() ?? ''
          for (const line of lines) {
            if (!/^frame=\s*\d+$/u.test(line)) continue
            const count = Number(line.slice(6))
            if (count > outputFrames) {
              outputFrames = count
              outputAt = performance.now()
              published = true
              markReady()
            }
          }
        })
        const send = (tag: Buffer): Promise<void> => {
          const operation = writing.then(async () => {
            if (activeSignal.aborted) return
            const input = publisher.stdin
            if (!input) throw Error('Missing live publisher input')
            if (!input.write(tag)) await once(input, 'drain', { signal: activeSignal })
          })
          writing = operation.catch(() => {
            if (!activeSignal.aborted) fail()
          })
          return operation
        }
        await send(LIVE_FLV_HEADER)
        const audioEncoder = track(
          spawn(
            ffmpeg,
            [
              '-hide_banner',
              '-loglevel',
              'error',
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
              'pipe:0',
              '-c:a',
              'aac',
              '-b:a',
              '128k',
              '-f',
              'flv',
              '-flvflags',
              'no_duration_filesize',
              'pipe:1',
            ],
            { stdio: ['pipe', 'pipe', 'pipe'] },
          ),
        )
        const audioParser = new LiveFlvParser()
        const audioOutput = audioEncoder.stdout
        audioOutput?.on('data', (chunk: Buffer) => {
          audioOutput.pause()
          void audioParser
            .push(chunk, async (tag) => {
              if (tag[0] === 8) await send(tag)
            })
            .then(() => audioOutput.resume(), fail)
        })
        let audioSamples = 0
        const pumpAudio = (): void => {
          audioWriting = (async () => {
            if (activeSignal.aborted) return
            const at = origin + audioSamples / 48
            const pcm = audio?.buffer.read(at, 960) ?? Buffer.alloc(3840)
            const input = audioEncoder.stdin
            if (!input) throw Error('Missing live audio encoder input')
            if (!input.write(pcm)) await once(input, 'drain', { signal: activeSignal })
            audioSamples += 960
            if (!activeSignal.aborted)
              audioTimer = setTimeout(
                pumpAudio,
                Math.max(0, origin + LIVE_AUDIO_DELAY_MS + audioSamples / 48 - performance.now()),
              )
          })().catch(() => {
            if (!activeSignal.aborted) fail()
          })
        }
        pumpAudio()
        const prepareVideo = (
          value: number,
          resynchronize = false,
        ): { stream: SuiteCutLiveStream; switched: Promise<void> } => {
          const id = ++nextId
          const times = new Map<number, number>()
          const parser = new LiveFlvParser()
          let configuration: Buffer | undefined
          let switched = false
          let handoffReady = false
          const queued: { tag: Buffer; timestamp: number }[] = []
          let resolveSwitch = (): void => undefined
          const committed = new Promise<void>((resolve) => {
            resolveSwitch = resolve
          })
          const stream = makeVideo(
            { ...options, bitrateKbps: value, onDiagnostic: report },
            {
              health: delivery,
              frame: (index, at) => {
                times.set(index, Math.max(0, Math.round(at - origin)))
                if (times.size > 610)
                  throw Error('Live encoded video timestamp queue exceeded its bound')
              },
              data: (chunk) =>
                parser.push(chunk, async (tag) => {
                  if (tag[0] !== 9 || tag[12] === 2) return
                  if (tag[12] === 0) {
                    configuration = tag
                    return
                  }
                  if (tag[12] !== 1) throw Error('Unsupported live video packet')
                  const index = Math.round((flvTimestamp(tag) * fps) / 1000)
                  let timestamp = times.get(index)
                  for (const key of times.keys()) if (key <= index) times.delete(key)
                  if (timestamp === undefined) throw Error('Missing live video capture timestamp')
                  if (id !== activeId) {
                    if (switched || !configuration) return
                    if (queued.length === 0) {
                      if ((tag[11] ?? 0) >> 4 !== 1 || timestamp <= lastVideoTime) return
                      const handoffTimestamp = timestamp
                      handoffReady = video === undefined || resynchronize
                      pendingHandoffReady = () => {
                        if (lastVideoTime >= handoffTimestamp - 1000 / fps) handoffReady = true
                      }
                    }
                    queued.push({ tag, timestamp })
                    if (queued.length > 610)
                      throw Error('Live video handoff queue exceeded its bound')
                    pendingHandoffReady?.()
                    if (!handoffReady) return
                    const previous = video
                    activeId = id
                    switched = true
                    video = stream
                    warming = undefined
                    pendingHandoffReady = undefined
                    const keyframe = queued[0]
                    if (!keyframe) throw Error('Missing live video handoff keyframe')
                    lastVideoTime = Math.max(lastVideoTime + 1, keyframe.timestamp)
                    await send(
                      Buffer.concat([
                        stampFlv(configuration, lastVideoTime),
                        stampFlv(keyframe.tag, lastVideoTime),
                      ]),
                    )
                    submittedFrames++
                    for (const queuedFrame of queued.slice(1)) {
                      if (queuedFrame.timestamp <= lastVideoTime) continue
                      lastVideoTime = queuedFrame.timestamp
                      await send(stampFlv(queuedFrame.tag, lastVideoTime))
                      submittedFrames++
                    }
                    queued.length = 0
                    if (previous && previous !== stream) {
                      const stopped = previous.stop().catch(() => undefined)
                      retiring.add(stopped)
                      void stopped.finally(() => retiring.delete(stopped))
                    }
                    resolveSwitch()
                    return
                  }
                  timestamp = Math.max(timestamp, lastVideoTime + 1)
                  lastVideoTime = timestamp
                  await send(stampFlv(tag, timestamp))
                  submittedFrames++
                  pendingHandoffReady?.()
                  resolveSwitch()
                }),
            },
            activeSignal,
          )
          void stream.failure.catch(() => {
            if (id === activeId && !activeSignal.aborted) fail()
          })
          if (latest) stream.update(latest, latestTimestamp)
          return { stream, switched: committed }
        }
        const initial = prepareVideo(bitrate.current)
        warming = initial.stream
        await raceWithAbort(
          Promise.race([initial.switched, initial.stream.failure, attemptFailure]),
          AbortSignal.any([activeSignal, AbortSignal.timeout(15000)]),
        )
        change = async (value, sample) => {
          if (changing || activeSignal.aborted) return
          if (!Number.isInteger(value) || value < 100 || value > (options.bitrateKbps ?? 4500))
            throw Error('Invalid live bitrate')
          changing = true
          const previous = bitrate.current
          bitrateEvent('bitrate-changing', previous, value, sample)
          const candidate = prepareVideo(value, sample !== undefined)
          warming = candidate.stream
          try {
            await raceWithAbort(
              Promise.race([candidate.switched, candidate.stream.failure]),
              AbortSignal.any([activeSignal, AbortSignal.timeout(15000)]),
            )
            bitrate.current = value
            bitrateEvent('bitrate-adjusted', previous, value, sample)
            await Promise.all(retiring)
          } catch {
            bitrateEvent('bitrate-change-failed', previous, value, sample)
            await candidate.stream.stop().catch(() => undefined)
            warming = undefined
            if (!activeSignal.aborted && video === candidate.stream) fail()
            throw new Error('SuiteCut could not prepare the requested live bitrate.')
          } finally {
            changing = false
          }
        }
        monitor = setInterval(() => {
          if (activeSignal.aborted) return
          if (performance.now() - outputAt > 20000) {
            const state = delivery()
            report({
              event: 'stalled',
              bottleneck: state.networkBlockedMs >= 200 ? 'network-backpressure' : 'unknown',
              lagMs: video?.health().captureLagMs ?? 0,
              processingMs: 0,
              videoWriteMs: 0,
              audioWriteMs: 0,
              skippedMs: 0,
              incidents: 0,
              ...state,
            })
            fail()
            return
          }
          if (changing) return
          const sample = network.sample()
          const healthy =
            published &&
            performance.now() - outputAt < 1000 &&
            (video?.health().captureLagMs ?? Infinity) < 500 &&
            delivery().outputLagMs < 500
          const previous = bitrate.current
          const next = bitrate.update(performance.now(), sample, healthy)
          if (next !== undefined && !changing) {
            bitrate.current = previous
            void change?.(next, sample).catch(() => undefined)
          }
        }, 1000)
        await raceWithAbort(attemptFailure, activeSignal)
      } catch {
        if (!combined.aborted) {
          if (options.reconnect === false) {
            terminal = new Error('SuiteCut live publishing pipeline failed.')
            break
          }
          if (performance.now() - started > 30000) failures = 0
          failures++
          if (
            (options.reconnect?.maxAttempts ?? 0) > 0 &&
            failures > (options.reconnect?.maxAttempts ?? 0)
          ) {
            terminal = new Error('SuiteCut live publishing retry budget exhausted.')
            break
          }
        }
      } finally {
        await finishAttempt()
      }
      if (!combined.aborted)
        await delay(
          Math.min(
            options.reconnect === false ? 1000 : (options.reconnect?.maxDelayMs ?? 30000),
            (options.reconnect === false ? 1000 : (options.reconnect?.initialDelayMs ?? 1000)) *
              2 ** Math.min(failures - 1, 20),
          ),
          undefined,
          { signal: combined },
        ).catch(() => undefined)
    }
    if (terminal) throw terminal
    return await new Promise<never>(() => undefined)
  })()
  void failure.catch(() => undefined)
  return {
    failure,
    ready: () => raceWithAbort(Promise.race([ready, failure]), combined),
    health: () => ({ captureLagMs: video?.health().captureLagMs ?? 0, ...delivery() }),
    update: (frame, timestampMs) => {
      if (combined.aborted) return
      latest = frame
      latestTimestamp = timestampMs
      video?.update(frame, timestampMs)
      if (warming !== video) warming?.update(frame, timestampMs)
    },
    setBitrate: async (value) => {
      if (!change) throw Error('Live publisher is not ready')
      await change(value)
    },
    stop: () =>
      (stopping ??= (async () => {
        controller.abort()
        latest = undefined
        await finishAttempt?.()
        if (terminal) throw terminal
      })()),
  }
}
