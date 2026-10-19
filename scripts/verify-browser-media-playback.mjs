// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises a selectable emitted SDK and real browser-decoded media, preserving runtime capability results. */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { mkdir, stat, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

const run = promisify(execFile)
const output = resolve(process.env.SUITECUT_MEDIA_OUTPUT ?? '.suitecut/real-world-review/media')
const sdkPath = resolve(
  process.env.SUITECUT_MEDIA_SDK ?? 'packages/browser-control/dist/browser.js',
)
const onlyFixtures = (process.env.SUITECUT_MEDIA_FIXTURES ?? '').split(',').filter(Boolean)
const onlyBackends = (process.env.SUITECUT_MEDIA_BACKENDS ?? '').split(',').filter(Boolean)
const onlyModes = (process.env.SUITECUT_MEDIA_MODES ?? '').split(',').filter(Boolean)
const muted = process.env.SUITECUT_MEDIA_MUTED === '1'
const separateWebM = process.env.SUITECUT_MEDIA_SEPARATE_WEBM !== '0'
const fixturesRoot = resolve(output, 'fixtures')
await mkdir(fixturesRoot, { recursive: true })
const sdk = await import(pathToFileURL(sdkPath).href)
const hash = async (path) => {
  const digest = createHash('sha256')
  for await (const chunk of createReadStream(path)) digest.update(chunk)
  return digest.digest('hex')
}
const ffmpeg = await run('ffmpeg', ['-hide_banner', '-encoders'], {
  timeout: 10000,
  maxBuffer: 1048576,
})
const available = ffmpeg.stdout
const video = ['-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=12:duration=1.25']
const audio = ['-f', 'lavfi', '-i', 'sine=frequency=523.25:sample_rate=48000:duration=1.25']
const x264 = [
  '-c:v',
  'libx264',
  '-preset',
  'ultrafast',
  '-tune',
  'zerolatency',
  '-profile:v',
  'baseline',
  '-level:v',
  '3.0',
  '-pix_fmt',
  'yuv420p',
  '-g',
  '6',
]
const vpx = (codec) => [
  '-c:v',
  codec,
  '-deadline',
  'realtime',
  '-cpu-used',
  '8',
  '-b:v',
  '180k',
  '-g',
  '6',
]
const fixtures = [
  {
    id: 'vp8-vorbis',
    file: 'vp8-vorbis.webm',
    kind: 'video',
    mime: 'video/webm; codecs="vp8, vorbis"',
    encoders: ['libvpx', 'libvorbis'],
    args: [...video, ...audio, ...vpx('libvpx'), '-c:a', 'libvorbis'],
  },
  {
    id: 'vp8-opus',
    file: 'vp8-opus.webm',
    kind: 'video',
    mime: 'video/webm; codecs="vp8, opus"',
    encoders: ['libvpx', 'libopus'],
    args: [...video, ...audio, ...vpx('libvpx'), '-c:a', 'libopus'],
  },
  {
    id: 'vp9-opus',
    file: 'vp9-opus.webm',
    kind: 'video',
    mime: 'video/webm; codecs="vp9, opus"',
    encoders: ['libvpx-vp9', 'libopus'],
    args: [...video, ...audio, ...vpx('libvpx-vp9'), '-c:a', 'libopus'],
    mse: true,
  },
  {
    id: 'av1-opus',
    file: 'av1-opus.webm',
    kind: 'video',
    mime: 'video/webm; codecs="av01.0.00M.08, opus"',
    encoders: ['libaom-av1', 'libopus'],
    args: [
      ...video,
      ...audio,
      '-c:v',
      'libaom-av1',
      '-cpu-used',
      '8',
      '-b:v',
      '180k',
      '-g',
      '6',
      '-c:a',
      'libopus',
    ],
  },
  {
    id: 'h264-aac',
    file: 'h264-aac.mp4',
    kind: 'video',
    mime: 'video/mp4; codecs="avc1.42C01E, mp4a.40.2"',
    encoders: ['libx264', 'aac'],
    args: [
      ...video,
      ...audio,
      ...x264,
      '-c:a',
      'aac',
      '-movflags',
      '+frag_keyframe+empty_moov+default_base_moof',
    ],
    mse: true,
  },
  {
    id: 'h264-only',
    file: 'h264-only.mp4',
    kind: 'video',
    mime: 'video/mp4; codecs="avc1.42C01E"',
    encoders: ['libx264'],
    args: [...video, ...x264, '-an', '-movflags', '+faststart'],
  },
  {
    id: 'hevc-only',
    file: 'hevc-only.mp4',
    kind: 'video',
    mime: 'video/mp4; codecs="hvc1.1.6.L30.B0"',
    encoders: ['libx265'],
    args: [
      ...video,
      '-c:v',
      'libx265',
      '-preset',
      'ultrafast',
      '-x265-params',
      'pools=1:frame-threads=1:log-level=error',
      '-pix_fmt',
      'yuv420p',
      '-tag:v',
      'hvc1',
      '-an',
      '-movflags',
      '+faststart',
    ],
  },
  {
    id: 'aac',
    file: 'aac.m4a',
    kind: 'audio',
    mime: 'audio/mp4; codecs="mp4a.40.2"',
    encoders: ['aac'],
    args: [...audio, '-c:a', 'aac', '-movflags', '+faststart'],
  },
  {
    id: 'mp3',
    file: 'audio.mp3',
    kind: 'audio',
    mime: 'audio/mpeg',
    encoders: ['libmp3lame'],
    args: [...audio, '-c:a', 'libmp3lame'],
  },
  {
    id: 'wav-pcm',
    file: 'audio.wav',
    kind: 'audio',
    mime: 'audio/wav; codecs="1"',
    encoders: ['pcm_s16le'],
    args: [...audio, '-c:a', 'pcm_s16le'],
  },
  {
    id: 'ogg-vorbis',
    file: 'vorbis.ogg',
    kind: 'audio',
    mime: 'audio/ogg; codecs="vorbis"',
    encoders: ['libvorbis'],
    args: [...audio, '-c:a', 'libvorbis'],
  },
  {
    id: 'ogg-opus',
    file: 'opus.ogg',
    kind: 'audio',
    mime: 'audio/ogg; codecs="opus"',
    encoders: ['libopus'],
    args: [...audio, '-c:a', 'libopus'],
  },
  {
    id: 'flac',
    file: 'audio.flac',
    kind: 'audio',
    mime: 'audio/flac',
    encoders: ['flac'],
    args: [...audio, '-c:a', 'flac'],
  },
]
if (separateWebM) {
  fixtures.find((item) => item.id === 'vp9-opus').mseTracks = [
    { id: 'vp9-track', mime: 'video/webm; codecs="vp9"' },
    { id: 'opus-track', mime: 'audio/webm; codecs="opus"' },
  ]
  fixtures.push({
    id: 'vp9-track',
    file: 'vp9-track.webm',
    kind: 'video',
    mime: 'video/webm; codecs="vp9"',
    supportOnly: true,
    encoders: ['libvpx-vp9'],
    args: [...video, ...vpx('libvpx-vp9'), '-an', '-live', '1'],
  })
  fixtures.push({
    id: 'opus-track',
    file: 'opus-track.webm',
    kind: 'audio',
    mime: 'audio/webm; codecs="opus"',
    supportOnly: true,
    encoders: ['libopus'],
    args: [...audio, '-c:a', 'libopus', '-live', '1'],
  })
}
const generated = []
for (const fixture of fixtures.filter(
  (item) => !onlyFixtures.length || onlyFixtures.includes(item.id) || item.supportOnly,
)) {
  const missing = fixture.encoders.filter((encoder) => !available.includes(` ${encoder} `))
  if (missing.length) {
    generated.push({ id: fixture.id, status: 'SKIPPED_MISSING_ENCODER', missing })
    continue
  }
  const path = resolve(fixturesRoot, fixture.file)
  try {
    await run(
      'ffmpeg',
      [
        '-hide_banner',
        '-loglevel',
        'error',
        '-y',
        ...fixture.args,
        '-threads',
        '2',
        '-shortest',
        path,
      ],
      { timeout: 20000, maxBuffer: 1048576 },
    )
    const probe = await run(
      'ffprobe',
      ['-v', 'error', '-show_streams', '-show_format', '-of', 'json', path],
      { timeout: 10000, maxBuffer: 1048576 },
    )
    fixture.ready = true
    generated.push({
      id: fixture.id,
      status: 'GENERATED',
      file: fixture.file,
      bytes: (await stat(path)).size,
      sha256: await hash(path),
      probe: JSON.parse(probe.stdout),
    })
  } catch (error) {
    generated.push({ id: fixture.id, status: 'FIXTURE_GENERATION_FAILED', error: String(error) })
  }
}
await writeFile(resolve(output, 'fixtures.json'), JSON.stringify(generated, null, 2) + '\n')
const byId = new Map(
  fixtures.filter((fixture) => fixture.ready).map((fixture) => [fixture.id, fixture]),
)
const player = (
  fixture,
  mse,
) => `<!doctype html><meta charset="utf-8"><title>Actual media playback</title><style>button{margin:8px}video{width:320px;height:180px;display:block}audio{display:block}</style><${fixture.kind} id="media" controls playsinline preload="none"></${fixture.kind}><button id="play">Play media</button><button id="pause">Pause media</button><button id="resume">Resume media</button><button id="seek">Seek media</button><script>
const media=document.querySelector('#media'), state={events:[],trustedPlay:false,playError:null,mseError:null,appendedChunks:0,seeked:0,frameCallbacks:0,lastFrameMediaTime:0};window.mediaState=state;
for(const name of ['loadedmetadata','loadeddata','playing','pause','seeking','seeked','ended','error'])media.addEventListener(name,()=>{state.events.push(name);if(name==='seeked')state.seeked++});
if(media.requestVideoFrameCallback){const frame=(now,meta)=>{state.frameCallbacks++;state.lastFrameMediaTime=meta.mediaTime;media.requestVideoFrameCallback(frame)};media.requestVideoFrameCallback(frame)}
const error=e=>({name:e.name,message:e.message});
document.querySelector('#play').onclick=async event=>{state.trustedPlay=event.isTrusted;media.muted=${muted};media.volume=.05;try{
if(${mse}){const source=new MediaSource;media.src=URL.createObjectURL(source);await new Promise((resolve,reject)=>{source.addEventListener('sourceopen',resolve,{once:true});setTimeout(()=>reject(new Error('MediaSource sourceopen timeout')),2000)});const tracks=${JSON.stringify(fixture.mseTracks ?? [{ id: fixture.id, mime: fixture.mime }])};let buffers;try{buffers=tracks.map(track=>source.addSourceBuffer(track.mime))}catch(e){state.mseError=error(e);throw e}let playing;
for(let index=0;index<tracks.length;index++){const buffer=buffers[index],bytes=new Uint8Array(await(await fetch('/media/'+tracks[index].id)).arrayBuffer());for(let offset=0;offset<bytes.length;offset+=8192){const updated=new Promise((resolve,reject)=>{buffer.addEventListener('updateend',resolve,{once:true});buffer.addEventListener('error',()=>reject(new Error('SourceBuffer decode error')),{once:true})});buffer.appendBuffer(bytes.slice(offset,offset+8192));await updated;state.appendedChunks++;if(!playing&&buffers.every(item=>item.buffered.length)){playing=media.play();playing.catch(e=>state.playError=error(e))}await new Promise(resolve=>setTimeout(resolve,10))}}source.endOfStream();await(playing||media.play());
}else{media.src=${JSON.stringify(`/media/${fixture.id}`)};await media.play()}
}catch(e){state.playError=error(e)}};
document.querySelector('#pause').onclick=()=>media.pause();document.querySelector('#resume').onclick=()=>media.play().catch(e=>state.playError=error(e));document.querySelector('#seek').onclick=()=>{media.currentTime=.6};
</script>`
const requests = { pages: 0, media: 0, ranges: 0 }
const server = createServer((request, response) => {
  void (async () => {
    const url = new URL(request.url ?? '/', 'http://127.0.0.1')
    const fixture = byId.get(
      url.pathname.startsWith('/media/') ? url.pathname.slice(7) : url.searchParams.get('fixture'),
    )
    if (!fixture) {
      response.writeHead(404)
      response.end('Unknown fixture')
      return
    }
    if (!url.pathname.startsWith('/media/')) {
      requests.pages++
      response.setHeader('Content-Type', 'text/html')
      response.end(player(fixture, url.searchParams.get('mse') === '1'))
      return
    }
    requests.media++
    const path = resolve(fixturesRoot, fixture.file),
      info = await stat(path)
    response.setHeader('Content-Type', fixture.mime.split(';')[0])
    response.setHeader('Accept-Ranges', 'bytes')
    const range = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range ?? '')
    let start = 0,
      end = info.size - 1
    if (range) {
      requests.ranges++
      start = Number(range[1])
      end = Math.min(end, range[2] ? Number(range[2]) : end)
      if (start > end) {
        response.writeHead(416, { 'Content-Range': `bytes */${info.size}` })
        response.end()
        return
      }
      response.statusCode = 206
      response.setHeader('Content-Range', `bytes ${start}-${end}/${info.size}`)
    }
    response.setHeader('Content-Length', end - start + 1)
    createReadStream(path, { start, end })
      .on('error', () => response.destroy())
      .pipe(response)
  })().catch((error) => {
    response.statusCode = 500
    response.end(String(error))
  })
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const origin = `http://127.0.0.1:${server.address().port}`
const readPlayer = () => {
  const media = globalThis.document.querySelector('#media'),
    state = globalThis.mediaState
  const quality =
    typeof media.getVideoPlaybackQuality === 'function' ? media.getVideoPlaybackQuality() : null
  return {
    currentTime: media.currentTime,
    duration: Number.isFinite(media.duration) ? media.duration : null,
    ended: media.ended,
    paused: media.paused,
    readyState: media.readyState,
    networkState: media.networkState,
    width: media.videoWidth ?? null,
    height: media.videoHeight ?? null,
    totalVideoFrames: quality?.totalVideoFrames ?? media.webkitDecodedFrameCount ?? null,
    droppedVideoFrames: quality?.droppedVideoFrames ?? null,
    decodedAudioBytes: media.webkitAudioDecodedByteCount ?? null,
    error: media.error ? { code: media.error.code, message: media.error.message } : null,
    ...state,
  }
}
const waitPlayer = async (page, predicate, timeoutMs = 4500) => {
  const deadline = performance.now() + timeoutMs
  let last
  do {
    last = await page.evaluate(readPlayer)
    if (predicate(last)) return last
    await delay(60)
  } while (performance.now() < deadline)
  return last
}
const candidates = [
  {
    name: 'CEF',
    backend: 'cef',
    executablePath: resolve(
      process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/memory-deep/runtime/suitecut-browser',
    ),
  },
  {
    name: 'Chrome',
    backend: 'cdp',
    executablePath: resolve(
      process.env.SUITECUT_CHROME_EXECUTABLE ??
        '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
    ),
  },
  {
    name: 'Firefox',
    backend: 'bidi',
    executablePath: resolve(
      process.env.SUITECUT_FIREFOX_EXECUTABLE ?? '.suitecut/browser-protocol/tools/firefox/firefox',
    ),
  },
].filter((item) => !onlyBackends.length || onlyBackends.includes(item.backend))
const result = {
  scope:
    'Known tiny local fixtures and actual browser playback, not every codec/profile/DRM delivery format. No video recordings or backend speed ranking.',
  playbackMuted: muted,
  separateWebM,
  sdk: sdkPath,
  sdkSHA256: await hash(sdkPath),
  ffmpegVersion: (await run('ffmpeg', ['-version'])).stdout.split('\n')[0],
  generated,
  results: [],
}
try {
  for (const candidate of candidates) {
    let browser
    const backend = {
      ...candidate,
      binarySHA256: await hash(candidate.executablePath),
      tests: [],
      launchError: null,
    }
    if (candidate.backend === 'cef')
      backend.librarySHA256 = await hash(resolve(dirname(candidate.executablePath), 'libcef.so'))
    if (candidate.backend === 'bidi')
      backend.librarySHA256 = await hash(resolve(dirname(candidate.executablePath), 'libxul.so'))
    try {
      browser = await sdk.launchBrowser(
        candidate.backend === 'cef'
          ? {
              executablePath: candidate.executablePath,
              headless: true,
              width: 800,
              height: 600,
              framesPerSecond: 60,
            }
          : {
              backend: candidate.backend,
              executablePath: candidate.executablePath,
              headless: true,
              args: candidate.backend === 'cdp' ? ['--no-sandbox', '--disable-dev-shm-usage'] : [],
              timeoutMs: 15000,
            },
      )
      backend.version = browser.version()
      const context = await browser.newContext(),
        page = await context.newPage()
      page.setDefaultTimeout(5000)
      await page.goto(`${origin}/player?fixture=${[...byId.keys()][0]}`)
      backend.advertised = await page.evaluate(
        async (cases) => {
          const video = globalThis.document.createElement('video'),
            audio = globalThis.document.createElement('audio')
          const videoCodecs = [
            'vp8',
            'vp09.00.10.08',
            'av01.0.00M.08',
            'avc1.42C01E',
            'hvc1.1.6.L30.B0',
          ]
          const audioCodecs = ['opus', 'vorbis', 'mp4a.40.2', 'mp3', 'flac']
          const check = async (api, config) => {
            if (!api) return { available: false }
            try {
              const value = await api.isConfigSupported(config)
              return { available: true, supported: value.supported }
            } catch (error) {
              return { available: true, error: { name: error.name, message: error.message } }
            }
          }
          return {
            media: cases.map((item) => ({
              id: item.id,
              mime: item.mime,
              canPlayType: (item.kind === 'audio' ? audio : video).canPlayType(item.mime),
              mediaSource:
                typeof globalThis.MediaSource === 'function'
                  ? globalThis.MediaSource.isTypeSupported(item.mime)
                  : null,
            })),
            webCodecsVideo: await Promise.all(
              videoCodecs.map(async (codec) => ({
                codec,
                ...(await check(globalThis.VideoDecoder, {
                  codec,
                  codedWidth: 320,
                  codedHeight: 180,
                })),
              })),
            ),
            webCodecsAudio: await Promise.all(
              audioCodecs.map(async (codec) => ({
                codec,
                ...(await check(globalThis.AudioDecoder, {
                  codec,
                  sampleRate: 48000,
                  numberOfChannels: 1,
                })),
              })),
            ),
          }
        },
        [...byId.values()].map((item) => ({ id: item.id, mime: item.mime, kind: item.kind })),
      )
      for (const fixture of [...byId.values()].filter((item) => !item.supportOnly))
        for (const mode of (fixture.mse ? ['file', 'mse'] : ['file']).filter(
          (item) => !onlyModes.length || onlyModes.includes(item),
        )) {
          const test = {
            id: fixture.id,
            mode,
            kind: fixture.kind,
            mime: fixture.mime,
            playbackMuted: muted,
            mseLayout: fixture.mseTracks
              ? 'separate-video-and-audio-source-buffers'
              : 'single-multiplexed-source-buffer',
            outcome: 'NOT_RUN',
          }
          try {
            await page.goto(
              `${origin}/player?fixture=${fixture.id}&mse=${mode === 'mse' ? '1' : '0'}`,
            )
            await page.locator('#play').click()
            const initial = await waitPlayer(
              page,
              (state) =>
                state.currentTime >= 0.25 ||
                Boolean(state.ended) ||
                Boolean(state.error) ||
                Boolean(state.playError),
            )
            test.initialPlayback = initial
            if (!initial.error && !initial.playError && initial.currentTime >= 0.25) {
              await page.locator('#pause').click()
              const paused = await page.evaluate(readPlayer)
              await delay(80)
              const held = await page.evaluate(readPlayer)
              test.pause = {
                paused: held.paused,
                deltaSeconds: held.currentTime - paused.currentTime,
                passed: held.paused && Math.abs(held.currentTime - paused.currentTime) < 0.05,
              }
              if (mode === 'file') {
                await page.locator('#seek').click()
                const seek = await waitPlayer(page, (state) => state.seeked > 0, 1000)
                test.seek = {
                  time: seek.currentTime,
                  seekedEvents: seek.seeked,
                  passed: Math.abs(seek.currentTime - 0.6) < 0.1 && seek.seeked > 0,
                }
              }
              await page.locator('#resume').click()
            }
            const final = await waitPlayer(
              page,
              (state) => Boolean(state.ended) || Boolean(state.error) || Boolean(state.playError),
            )
            test.final = final
            test.actualTimeAdvance = initial.currentTime >= 0.25
            test.actualDecodedVideoFrames =
              fixture.kind === 'video'
                ? (final.totalVideoFrames ?? 0) > 0 || final.frameCallbacks > 0
                : null
            test.trustedPlayClick = final.trustedPlay
            test.outcome =
              final.error || final.playError
                ? final.error?.message?.includes('OnMediaSinkAudioError')
                  ? 'AUDIO_OUTPUT_SINK_ERROR'
                  : final.playError?.name === 'NotAllowedError'
                    ? 'AUTOPLAY_OR_PERMISSION_REJECTED'
                    : final.mseError
                      ? 'MSE_SETUP_UNSUPPORTED_OR_FAILED'
                      : final.error?.code === 3
                        ? 'MEDIA_DECODE_ERROR'
                        : final.error?.code === 4 || final.playError?.name === 'NotSupportedError'
                          ? 'SOURCE_DEMUX_OR_CODEC_UNSUPPORTED'
                          : 'MEDIA_PLAYBACK_ERROR'
                : final.ended &&
                    test.actualTimeAdvance &&
                    (fixture.kind !== 'video' || test.actualDecodedVideoFrames) &&
                    test.pause?.passed &&
                    (mode === 'mse' || test.seek?.passed)
                  ? 'PASS_ACTUAL_PLAYBACK'
                  : 'INCOMPLETE_PLAYBACK'
          } catch (error) {
            test.outcome = 'SDK_OR_FIXTURE_ERROR'
            test.error = String(error)
            test.stack = error?.stack
          }
          backend.tests.push(test)
          console.log(
            JSON.stringify({
              backend: candidate.name,
              id: fixture.id,
              mode,
              outcome: test.outcome,
              timeAdvance: test.actualTimeAdvance,
              decodedFrames: test.final?.totalVideoFrames,
              callbacks: test.final?.frameCallbacks,
              error: test.final?.error ?? test.error ?? test.final?.playError,
            }),
          )
          await writeFile(
            resolve(output, `${candidate.name.toLowerCase()}-progress.json`),
            JSON.stringify(backend, null, 2) + '\n',
          )
        }
      if (candidate.backend === 'cef')
        backend.nativeDiagnostics = await page.source.runAutomation('diagnostics')
      await context.close()
    } catch (error) {
      backend.launchError = String(error)
      backend.stack = error?.stack
    } finally {
      if (browser) {
        try {
          await browser.close()
          if (browser.closed) await browser.closed
          backend.shutdown =
            candidate.backend === 'cef' ? browser.source.processExit?.() : browser.connectionStats()
        } catch (error) {
          backend.shutdownError = String(error)
        }
      }
      result.results.push(backend)
      await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
    }
  }
} finally {
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
result.requests = requests
result.summary = result.results.map((backend) => ({
  name: backend.name,
  version: backend.version,
  launchError: backend.launchError,
  tested: backend.tests.length,
  actualPlaybackPass: backend.tests.filter((test) => test.outcome === 'PASS_ACTUAL_PLAYBACK')
    .length,
  outcomes: Object.fromEntries(
    [...new Set(backend.tests.map((test) => test.outcome))].map((outcome) => [
      outcome,
      backend.tests.filter((test) => test.outcome === outcome).length,
    ]),
  ),
}))
await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
console.log(JSON.stringify(result.summary))
assert.equal(result.results.length, candidates.length)
if (
  result.results.some(
    (backend) =>
      Boolean(backend.launchError) ||
      Boolean(backend.shutdownError) ||
      backend.tests.some((test) => test.outcome === 'SDK_OR_FIXTURE_ERROR'),
  )
)
  process.exitCode = 1
