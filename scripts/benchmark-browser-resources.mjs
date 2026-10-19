// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Runs the same public SDK with explicit real browser executables and preserves sampled resource evidence. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFileSync, spawn } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { closeSync, createReadStream, openSync, readSync, readdirSync } from 'node:fs'
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { fileURLToPath, pathToFileURL } from 'node:url'

const args = new Map()
for (let i = 2; i < process.argv.length; i += 2) {
  assert(process.argv[i]?.startsWith('--') && process.argv[i + 1], 'Expected --option value pairs')
  args.set(process.argv[i], process.argv[i + 1])
}
const hashFile = async (path) => {
  const hash = createHash('sha256')
  for await (const chunk of createReadStream(path)) hash.update(chunk)
  return hash.digest('hex')
}
const sdkManifests = new Map(),
  libraryHashes = new Map()
const sdkManifest = async (entry) => {
  if (sdkManifests.has(entry)) return sdkManifests.get(entry)
  const directory = dirname(entry),
    modules = []
  for (const name of readdirSync(directory, { recursive: true, encoding: 'utf8' })
    .filter((name) => name.endsWith('.js'))
    .sort())
    modules.push({ name, sha256: await hashFile(resolve(directory, name)) })
  const manifest = {
    moduleCount: modules.length,
    sha256: createHash('sha256').update(JSON.stringify(modules)).digest('hex'),
    modules,
  }
  sdkManifests.set(entry, manifest)
  return manifest
}
const libraryHash = async (executable) => {
  const path = resolve(dirname(executable), 'libcef.so')
  if (!libraryHashes.has(path)) libraryHashes.set(path, await hashFile(path))
  return libraryHashes.get(path)
}
const procBuffer = Buffer.alloc(65536)
const readProc = (path) => {
  const fd = openSync(path, 'r')
  try {
    const bytes = readSync(fd, procBuffer, 0, procBuffer.length, 0)
    assert(bytes < procBuffer.length, 'Unexpectedly large proc record')
    return procBuffer.toString('utf8', 0, bytes)
  } finally {
    closeSync(fd)
  }
}
const procStat = (pid) => {
  const text = readProc(`/proc/${pid}/stat`),
    fields = text
      .slice(text.lastIndexOf(')') + 2)
      .trim()
      .split(/\s+/)
  return {
    pid,
    parent: Number(fields[1]),
    state: fields[0],
    start: fields[19],
    cpuTicks: Number(fields[11]) + Number(fields[12]),
  }
}
const rollup = (pid) => {
  const text = readProc(`/proc/${pid}/smaps_rollup`)
  return {
    pssKiB: Number(/^Pss:\s+(\d+)/m.exec(text)?.[1] ?? 0),
    rssKiB: Number(/^Rss:\s+(\d+)/m.exec(text)?.[1] ?? 0),
  }
}
const fdCount = (pid) => {
  try {
    return readdirSync(`/proc/${pid}/fd`).length
  } catch {
    return null
  }
}
const currentIdentity = (item) => {
  try {
    const actual = procStat(item.pid)
    return actual.start === item.start ? actual : null
  } catch {
    return null
  }
}

async function worker(config) {
  assert(globalThis.gc, 'Run workers with --expose-gc')
  await mkdir(config.output, { recursive: true })
  const sdk = await import(pathToFileURL(config.sdk).href)
  const tracked = new Map(),
    samples = [],
    stages = [],
    frames = new Map(),
    observers = []
  const started = performance.now()
  let browser, context, page, failure
  const discover = () => {
    const all = readdirSync('/proc')
      .filter((item) => /^\d+$/.test(item))
      .flatMap((item) => {
        try {
          return [procStat(Number(item))]
        } catch {
          return []
        }
      })
    const owned = new Set([process.pid])
    for (let changed = true; changed;) {
      changed = false
      for (const item of all)
        if (owned.has(item.parent) && !owned.has(item.pid)) {
          owned.add(item.pid)
          changed = true
        }
    }
    return all.filter((item) => item.pid !== process.pid && owned.has(item.pid))
  }
  const cpu = () => {
    const alive = discover()
    for (const item of alive) {
      const key = `${item.pid}:${item.start}`,
        old = tracked.get(key)
      let command = old?.command
      if (command === undefined) {
        try {
          command = readProc(`/proc/${item.pid}/cmdline`).replaceAll('\0', ' ')
        } catch {
          command = '[Exited before command read]'
        }
      }
      tracked.set(key, {
        ...old,
        ...item,
        cpuTicks: Math.max(old?.cpuTicks ?? 0, item.cpuTicks),
        firstSeenMs: old?.firstSeenMs ?? performance.now() - started,
        lastSeenMs: performance.now() - started,
        command,
      })
    }
    return {
      browserTicks: [...tracked.values()].reduce((sum, item) => sum + item.cpuTicks, 0),
      nodeCPU: process.cpuUsage(),
      alive,
    }
  }
  const frameTotals = () =>
    [...frames.values()].reduce(
      (sum, item) => ({
        frames: sum.frames + item.frames,
        framePayloadBytes: sum.framePayloadBytes + item.framePayloadBytes,
        audioPackets: sum.audioPackets + item.audioPackets,
        audioPayloadBytes: sum.audioPayloadBytes + item.audioPayloadBytes,
      }),
      { frames: 0, framePayloadBytes: 0, audioPackets: 0, audioPayloadBytes: 0 },
    )
  const watch = (source) => {
    if (!source || frames.has(source.pageId)) return
    const entry = {
      pageId: source.pageId,
      frames: 0,
      framePayloadBytes: 0,
      audioPackets: 0,
      audioPayloadBytes: 0,
      requestedFPS: source.framesPerSecond,
      pixelFormat: source.pixelFormat,
      maxDroppedCounter: 0,
    }
    frames.set(source.pageId, entry)
    let replay = true
    if (config.observeFrames !== false)
      observers.push(
        source.onFrame((frame) => {
          if (!replay) {
            entry.frames++
            entry.framePayloadBytes += frame.data.length
            entry.maxDroppedCounter = Math.max(entry.maxDroppedCounter, frame.droppedFrames)
          }
        }),
      )
    replay = false
    observers.push(
      source.onAudio((packet) => {
        entry.audioPackets++
        entry.audioPayloadBytes += packet.data.length
      }),
    )
  }
  const sample = async (label) => {
    const accounting = cpu(),
      processes = []
    for (const item of accounting.alive) {
      try {
        processes.push({ ...item, ...rollup(item.pid), fds: fdCount(item.pid) })
      } catch {
        /* Process exited between identity and rollup reads. */
      }
    }
    const nodeFDs = fdCount(process.pid)
    await delay(0)
    globalThis.gc()
    const nodePhysical = rollup(process.pid)
    globalThis.gc()
    const entry = {
      label,
      elapsedMs: performance.now() - started,
      browser: {
        pssKiB: processes.reduce((sum, item) => sum + item.pssKiB, 0),
        summedRSSKiB: processes.reduce((sum, item) => sum + item.rssKiB, 0),
        summedFDs: processes.reduce((sum, item) => sum + (item.fds ?? 0), 0),
        processes,
      },
      node: { ...nodePhysical, fds: nodeFDs, postGC: process.memoryUsage() },
      cpu: { browserTicks: accounting.browserTicks, nodeCPU: accounting.nodeCPU },
      nativeObservedPayloads: frameTotals(),
    }
    samples.push(entry)
    return entry
  }
  const phase = async (name, durationMs, setup) => {
    const setupBefore = cpu(),
      setupStarted = performance.now()
    await setup()
    const transitionMs = performance.now() - setupStarted,
      before = await sample(`${name}:start`),
      dwellStarted = performance.now(),
      until = dwellStarted + durationMs
    while (performance.now() < until) {
      await delay(Math.max(0, Math.min(1000, until - performance.now())))
      cpu()
    }
    const after = await sample(`${name}:end`)
    const entry = {
      name,
      transitionMs,
      scheduledDwellMs: durationMs,
      actualDwellMs: performance.now() - dwellStarted,
      transitionBrowserCPUTicks: before.cpu.browserTicks - setupBefore.browserTicks,
      dwellBrowserCPUTicks: after.cpu.browserTicks - before.cpu.browserTicks,
      dwellBrowserCPUms:
        ((after.cpu.browserTicks - before.cpu.browserTicks) * 1000) / config.clockTicks,
      dwellNodeCPUms:
        (after.cpu.nodeCPU.user +
          after.cpu.nodeCPU.system -
          before.cpu.nodeCPU.user -
          before.cpu.nodeCPU.system) /
        1000,
      before,
      after,
    }
    stages.push(entry)
    console.log(
      JSON.stringify({
        run: config.id,
        stage: name,
        browserPSSKiB: after.browser.pssKiB,
        nodePSSKiB: after.node.pssKiB,
        browserCPUms: entry.dwellBrowserCPUms,
        nodeCPUms: entry.dwellNodeCPUms,
      }),
    )
  }
  const mediaState = () => {
    const media = globalThis.document.querySelector('video'),
      counters = globalThis.mediaCounters,
      quality = media.getVideoPlaybackQuality()
    return {
      currentTime: media.currentTime,
      duration: Number.isFinite(media.duration) ? media.duration : null,
      ended: media.ended,
      width: media.videoWidth,
      height: media.videoHeight,
      decodedFrames: quality.totalVideoFrames,
      droppedFrames: quality.droppedVideoFrames,
      error: media.error ? { code: media.error.code, message: media.error.message } : null,
      ...counters,
    }
  }
  const waitMedia = async (predicate) => {
    let last
    const until = performance.now() + 4000
    do {
      last = await page.evaluate(mediaState)
      if (predicate(last)) return last
      await delay(50)
    } while (performance.now() < until)
    return last
  }
  const result = {
    id: config.id,
    kind: config.kind,
    backend: config.backend,
    repetition: config.repetition,
    config,
    status: 'FAIL',
    samples,
    stages,
  }
  try {
    const beforeLaunch = await sample('before-launch'),
      launchStarted = performance.now()
    browser = await sdk.launchBrowser(
      config.backend === 'cef'
        ? {
            executablePath: config.executable,
            headless: true,
            width: 1280,
            height: 720,
            deviceScaleFactor: 1,
            framesPerSecond: 60,
            pixelFormat: 'bgra',
          }
        : {
            backend: 'cdp',
            executablePath: config.executable,
            headless: true,
            viewport: { width: 1280, height: 720 },
            args: config.chromeArgs,
            timeoutMs: 15000,
          },
    )
    result.launchMs = performance.now() - launchStarted
    result.version = browser.version()
    watch(browser.source)
    result.afterLaunch = await sample('after-launch')
    result.startupSampledBrowserCPUTicks =
      result.afterLaunch.cpu.browserTicks - beforeLaunch.cpu.browserTicks
    context = await browser.newContext()
    page = await context.newPage()
    watch(page.source)
    page.setDefaultTimeout(5000)
    await page.goto(`${config.origin}/idle`)
    result.observedViewport = await page.evaluate(() => ({
      width: globalThis.innerWidth,
      height: globalThis.innerHeight,
      dpr: globalThis.devicePixelRatio,
    }))
    assert.deepEqual(result.observedViewport, { width: 1280, height: 720, dpr: 1 })
    if (config.kind === 'codec-gap-probe') {
      await page.goto(`${config.origin}/media?codec=h264`)
      await page.locator('#play').click()
      result.codecProbe = await waitMedia(
        (state) => Boolean(state.ended) || Boolean(state.error) || Boolean(state.playError),
      )
      result.codecProbe.actualDecoded =
        result.codecProbe.decodedFrames > 0 &&
        result.codecProbe.callbacks > 0 &&
        result.codecProbe.cumulativeMediaTime > 0.2
      result.status = 'OBSERVED_FEATURE_GAP_OR_PLAYBACK'
    } else {
      await phase('idle', 4000, () => Promise.resolve())
      await phase('dense', 4000, async () => {
        await page.goto(`${config.origin}/dense`)
        assert((await page.evaluate(() => globalThis.document.querySelectorAll('*').length)) > 5000)
      })
      const extras = []
      await phase('three-tabs', 4000, async () => {
        for (let i = 0; i < 2; i++) {
          const extra = await context.newPage()
          watch(extra.source)
          await extra.goto(`${config.origin}/dense`)
          extras.push(extra)
        }
        assert.equal((await context.pages()).length, 3)
      })
      await phase('vp9-opus-playback', 8000, async () => {
        for (const extra of extras) await extra.close()
        await page.goto(`${config.origin}/media?codec=vp9&loop=1`)
        await page.locator('#play').click()
        const playing = await waitMedia(
          (state) =>
            state.cumulativeMediaTime > 0.2 || Boolean(state.error) || Boolean(state.playError),
        )
        assert(
          !playing.error &&
            !playing.playError &&
            playing.decodedFrames > 0 &&
            playing.callbacks > 0,
          'Common VP9/Opus media did not decode/render',
        )
        result.mediaBefore = playing
      })
      result.mediaAfter = await page.evaluate(mediaState)
      assert(!result.mediaAfter.error && !result.mediaAfter.playError)
      assert(
        result.mediaAfter.decodedFrames > result.mediaBefore.decodedFrames &&
          result.mediaAfter.callbacks > result.mediaBefore.callbacks,
      )
      assert(result.mediaAfter.cumulativeMediaTime - result.mediaBefore.cumulativeMediaTime > 6)
      result.status = 'PASS'
    }
  } catch (error) {
    failure = error
    result.error = error instanceof Error ? error.message : 'Unexpected resource workload failure'
    result.stack = error?.stack
  } finally {
    const closeStarted = performance.now(),
      beforeClose = cpu()
    if (context) {
      try {
        await context.close()
      } catch (error) {
        failure ??= error
        result.contextCloseError = String(error)
      }
    }
    if (browser) {
      try {
        await browser.close()
        if (browser.closed) await browser.closed
        result.shutdown =
          config.backend === 'cef' ? browser.source.processExit?.() : browser.connectionStats()
      } catch (error) {
        failure ??= error
        result.browserCloseError = String(error)
      }
    }
    result.closeMs = performance.now() - closeStarted
    for (const detach of observers) detach()
    result.afterClose = await sample('after-close')
    result.closeSampledBrowserCPUTicks =
      result.afterClose.cpu.browserTicks - beforeClose.browserTicks
    result.trackedIdentities = [...tracked.values()]
    result.remainingIdentities = [...tracked.values()].flatMap((item) => {
      const actual = currentIdentity(item)
      return actual ? [actual] : []
    })
    if (result.remainingIdentities.some((item) => item.state !== 'Z')) {
      failure ??= new Error('Running browser descendants survived SDK close')
      result.status = 'FAIL'
    }
    result.nativeFrameObservers = [...frames.values()]
    result.elapsedMs = performance.now() - started
    if (failure) result.status = 'FAIL'
    await writeFile(resolve(config.output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
  }
  if (result.status === 'FAIL') process.exitCode = 1
}

if (args.has('--worker-config')) {
  await worker(JSON.parse(args.get('--worker-config')))
} else {
  const output = resolve(args.get('--output') ?? '.suitecut/external-native-perf/resources')
  const sdk = resolve(args.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
  const cef = resolve(
    args.get('--cef-executable') ?? '.suitecut/memory-deep/runtime/suitecut-browser',
  )
  const chrome = resolve(
    args.get('--chrome-executable') ??
      '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
  )
  const fixtureRoot = resolve('.suitecut/real-world-review/media/fixtures')
  await mkdir(output, { recursive: true })
  assert(
    readdirSync(output).length === 0,
    'Use a fresh output directory to preserve earlier measurements',
  )
  const dense =
    '<!doctype html><title>Matched dense document</title><style>body{font:14px sans-serif}article{height:16px}</style><main>' +
    Array.from(
      { length: 1024 },
      (_, i) => `<article><div><span>Record ${i}</span><b>${i * 7}</b><i>Ready</i></div></article>`,
    ).join('') +
    '</main>'
  const mediaHTML = (codec, loop) =>
    `<!doctype html><title>Matched decoded media</title><video id="video" width="640" height="360" playsinline ${loop ? 'loop' : ''} src="/file/${codec}"></video><button id="play">Play video</button><script>const video=document.querySelector('video'), counters={callbacks:0,cumulativeMediaTime:0,lastMediaTime:null,wraps:0,trustedPlay:false,playError:null};window.mediaCounters=counters;video.volume=.05;const frame=(now,meta)=>{if(counters.lastMediaTime!==null){if(meta.mediaTime<counters.lastMediaTime){counters.wraps++;counters.cumulativeMediaTime+=Math.max(0,video.duration-counters.lastMediaTime+meta.mediaTime)}else counters.cumulativeMediaTime+=meta.mediaTime-counters.lastMediaTime}counters.lastMediaTime=meta.mediaTime;counters.callbacks++;video.requestVideoFrameCallback(frame)};video.requestVideoFrameCallback(frame);document.querySelector('#play').onclick=event=>{counters.trustedPlay=event.isTrusted;video.play().catch(error=>counters.playError={name:error.name,message:error.message})}</script>`
  const server = createServer((request, response) => {
    void (async () => {
      const url = new URL(request.url ?? '/', 'http://127.0.0.1')
      if (url.pathname.startsWith('/file/')) {
        const path = resolve(
            fixtureRoot,
            url.pathname.endsWith('h264') ? 'h264-aac.mp4' : 'vp9-opus.webm',
          ),
          info = await stat(path)
        response.setHeader('Content-Type', path.endsWith('.mp4') ? 'video/mp4' : 'video/webm')
        response.setHeader('Accept-Ranges', 'bytes')
        const range = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range ?? ''),
          start = range ? Number(range[1]) : 0,
          end = Math.min(info.size - 1, range?.[2] ? Number(range[2]) : info.size - 1)
        if (start > end) {
          response.writeHead(416, { 'Content-Range': `bytes */${info.size}` })
          response.end()
          return
        }
        if (range) {
          response.statusCode = 206
          response.setHeader('Content-Range', `bytes ${start}-${end}/${info.size}`)
        }
        response.setHeader('Content-Length', end - start + 1)
        createReadStream(path, { start, end })
          .on('error', () => response.destroy())
          .pipe(response)
        return
      }
      response.setHeader('Content-Type', 'text/html')
      response.end(
        url.pathname === '/dense'
          ? dense
          : url.pathname === '/media'
            ? mediaHTML(url.searchParams.get('codec'), url.searchParams.get('loop') === '1')
            : '<!doctype html><title>Matched idle page</title><h1>Idle page</h1>',
      )
    })().catch((error) => {
      response.statusCode = 500
      response.end(String(error))
    })
  })
  await new Promise((done) => server.listen(0, '127.0.0.1', done))
  const origin = `http://127.0.0.1:${server.address().port}`,
    clockTicks = Number(execFileSync('getconf', ['CLK_TCK'], { encoding: 'utf8' }).trim())
  const chromeArgs = [
    ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []),
    '--disable-dev-shm-usage',
  ]
  const runs = args.has('--baseline-sdk')
    ? [
        {
          id: 'rep1-baseline-cef',
          backend: 'cef',
          repetition: 1,
          revision: 'baseline',
          sdk: resolve(args.get('--baseline-sdk')),
          executable: resolve(
            args.get('--baseline-cef-executable') ??
              '.suitecut/memory-deep/runtime/suitecut-browser',
          ),
        },
        { id: 'rep1-improved-cef', backend: 'cef', repetition: 1, revision: 'improved' },
        { id: 'rep2-improved-cef', backend: 'cef', repetition: 2, revision: 'improved' },
        {
          id: 'rep2-baseline-cef',
          backend: 'cef',
          repetition: 2,
          revision: 'baseline',
          sdk: resolve(args.get('--baseline-sdk')),
          executable: resolve(
            args.get('--baseline-cef-executable') ??
              '.suitecut/memory-deep/runtime/suitecut-browser',
          ),
        },
      ]
    : [
        { id: 'rep1-cef', backend: 'cef', repetition: 1 },
        { id: 'rep1-chrome', backend: 'cdp', repetition: 1 },
        { id: 'rep2-chrome', backend: 'cdp', repetition: 2 },
        { id: 'rep2-cef', backend: 'cef', repetition: 2 },
        { id: 'h264-gap-cef', backend: 'cef', kind: 'codec-gap-probe' },
        { id: 'h264-gap-chrome', backend: 'cdp', kind: 'codec-gap-probe' },
        ...(args.get('--swiftshader-control') === '1'
          ? [
              {
                id: 'chrome-swiftshader-control',
                backend: 'cdp',
                kind: 'secondary-software-control',
                software: true,
              },
            ]
          : []),
      ]
  const report = {
    scope:
      'Two balanced repetitions of the same 20-second dwell schedule, fresh SDK client/browser per run, no video recording. Optional software-rendering control and codec-gap probes are separate.',
    settings: {
      viewport: { width: 1280, height: 720, dpr: 1 },
      observeNativeFrames: args.get('--observe-frames') !== 'false',
      revisionComparison: args.has('--baseline-sdk'),
      cefRequestedFPS: 60,
      nativePixelFormat: 'bgra',
      scheduleMs: { idle: 4000, dense: 4000, threeTabs: 4000, media: 8000 },
      clockTicks,
      chromeArgs,
    },
    methodology: {
      memory:
        'Browser PSS is summed over observed live browser descendants; summed RSS double-counts shared mappings and is not physical footprint. Node memory is sampled after GC and all proc reads. Browser GC is never forced.',
      cpu: 'Per-identity /proc utime+stime maxima, including last observed ticks for known dead processes. CPU is a lower bound: processes dying before discovery or after their last sample can be missed. Child accounting fields are excluded to avoid double counting. Node CPU includes the sampler overhead.',
      frames:
        'Native observers count new delivered frame/audio callbacks and payload bytes without buffering. Initial synchronous frame replay is excluded. This is not a complete wire-byte measurement: headers and startup frames before subscription are excluded. External Chrome does not export equivalent raw frames; CDP wire bytes are a separate feature cost.',
      rendering:
        'Primary CEF uses its production software/SwiftShader path; primary Chrome uses its normal headless render configuration. These are backend-default comparisons, not equal GPU pipelines. No timing comparison is inferred from codec-gap probes.',
    },
    inputs: {
      sdk: { path: sdk, sha256: await hashFile(sdk) },
      cef: {
        path: cef,
        sha256: await hashFile(cef),
        libcefSHA256: await hashFile(resolve(dirname(cef), 'libcef.so')),
      },
      chrome: { path: chrome, sha256: await hashFile(chrome) },
      fixtures: {
        vp9: await hashFile(resolve(fixtureRoot, 'vp9-opus.webm')),
        h264: await hashFile(resolve(fixtureRoot, 'h264-aac.mp4')),
      },
    },
    results: [],
  }
  if (!report.settings.observeNativeFrames)
    report.methodology.frames =
      'No onFrame callback is registered on either revision. Frame callback counters are necessarily zero and do not measure raw native wire bytes: baseline can still deliver unsolicited raw frames to its SDK. Audio observers are identical and remain enabled. The separate frame-demand smoke passively observes wire headers to verify subscription behavior.'
  if (report.settings.revisionComparison)
    report.methodology.rendering =
      'Both revisions launch CEF at 1280x720/DPR1, requested 60 FPS, BGRA, and its production headless software/SwiftShader configuration. No rendering/FPS reduction or video recording is requested. The same local VP9/Opus file must actually decode and render throughout each media dwell.'
  const began = performance.now()
  try {
    for (const run of runs) {
      const config = {
        ...run,
        kind: run.kind ?? 'primary',
        executable: run.executable ?? (run.backend === 'cef' ? cef : chrome),
        sdk: run.sdk ?? sdk,
        observeFrames: args.get('--observe-frames') !== 'false',
        origin,
        clockTicks,
        chromeArgs: [
          ...chromeArgs,
          ...(run.software ? ['--disable-gpu', '--use-gl=angle', '--use-angle=swiftshader'] : []),
        ],
        output: resolve(output, run.id),
      }
      config.sdkSHA256 = await hashFile(config.sdk)
      config.executableSHA256 = await hashFile(config.executable)
      config.sdkModuleManifest = await sdkManifest(config.sdk)
      if (config.backend === 'cef') config.libcefSHA256 = await libraryHash(config.executable)
      await mkdir(config.output, { recursive: true })
      const child = spawn(
        process.execPath,
        ['--expose-gc', fileURLToPath(import.meta.url), '--worker-config', JSON.stringify(config)],
        { stdio: ['ignore', 'pipe', 'pipe'] },
      )
      let stdout = '',
        stderr = ''
      child.stdout.on('data', (chunk) => {
        stdout += chunk.toString()
        process.stdout.write(chunk)
      })
      child.stderr.on('data', (chunk) => {
        stderr += chunk.toString()
      })
      const exit = await new Promise((done, reject) => {
        child.once('error', reject)
        child.once('close', (code, signal) => done({ code, signal }))
      })
      await writeFile(resolve(config.output, 'stdout.log'), stdout)
      await writeFile(resolve(config.output, 'stderr.log'), stderr)
      const result = JSON.parse(await readFile(resolve(config.output, 'result.json'), 'utf8'))
      result.workerExit = exit
      report.results.push(result)
      await writeFile(resolve(output, 'result.json'), JSON.stringify(report, null, 2) + '\n')
      if (exit.code !== 0 || result.status === 'FAIL') process.exitCode = 1
    }
  } finally {
    server.closeAllConnections()
    await new Promise((done) => server.close(done))
  }
  report.elapsedMs = performance.now() - began
  await writeFile(resolve(output, 'result.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(
    JSON.stringify({ completedRuns: report.results.length, elapsedMs: report.elapsedMs, output }),
  )
}
