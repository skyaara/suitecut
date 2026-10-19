// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Checks the explicit real-browser verifier's persisted runtime evidence before accepting a codec build. */
import { spawn } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream, createWriteStream } from 'node:fs'
import { mkdir, readFile, readdir, realpath, stat, writeFile } from 'node:fs/promises'
import { dirname, relative, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { finished } from 'node:stream/promises'
import { clearTimeout, setTimeout } from 'node:timers'
import { fileURLToPath } from 'node:url'

const options = new Map()
const allowed = new Set(['--executable', '--output', '--sdk', '--profile', '--timeout-ms'])
for (let i = 2; i < process.argv.length; i += 2) {
  const flag = process.argv[i],
    value = process.argv[i + 1]
  if (!allowed.has(flag) || !value || value.startsWith('--') || options.has(flag))
    throw new Error(
      'Usage: node scripts/verify-browser-codec-runtime.mjs --executable PATH --output NEW_DIRECTORY [--sdk PATH] [--profile web|web-hevc] [--timeout-ms 180000]',
    )
  options.set(flag, value)
}
if (!options.get('--executable') || !options.get('--output'))
  throw new Error('Both --executable and --output are required; the CEF runtime must be explicit.')
const executable = resolve(options.get('--executable'))
const output = resolve(options.get('--output'))
const sdk = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const profile = options.get('--profile') ?? 'web'
const timeoutMs = Number(options.get('--timeout-ms') ?? 180000)
if (!['web', 'web-hevc'].includes(profile)) throw new Error('Profile must be web or web-hevc.')
if (!Number.isInteger(timeoutMs) || timeoutMs < 1000 || timeoutMs > 600000)
  throw new Error('Timeout must be an integer from 1000 to 600000 milliseconds.')
await mkdir(output, { recursive: true })
if ((await readdir(output)).length)
  throw new Error('Output directory must be empty; existing acceptance evidence is preserved.')
const verifier = fileURLToPath(new URL('./verify-browser-media-playback.mjs', import.meta.url))
const started = performance.now()
const report = {
  schemaVersion: 1,
  status: 'FAIL_SETUP',
  profile,
  scope:
    'Native CEF actual playback gate for the specified small codec/container profiles; no claim of every codec, hardware decoder, DRM, HDR or native HLS support.',
  executable,
  sdk,
  timeoutMs,
  requirements: [],
  cases: [],
  failures: [],
}
const hashFile = async (path) => {
  const digest = createHash('sha256')
  for await (const chunk of createReadStream(path)) digest.update(chunk)
  return digest.digest('hex')
}
const identity = async (path) => ({
  path,
  realPath: await realpath(path),
  bytes: (await stat(path)).size,
  sha256: await hashFile(path),
})
const sdkManifest = async () => {
  const root = dirname(sdk),
    files = []
  const walk = async (directory) => {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      const path = resolve(directory, entry.name)
      if (entry.isDirectory()) await walk(path)
      else if (entry.name.endsWith('.js') || entry.name === 'package.json') files.push(path)
    }
  }
  await walk(root)
  const modules = []
  for (const path of files.sort())
    modules.push({ path: relative(root, path), sha256: await hashFile(path) })
  return {
    root,
    sha256: createHash('sha256').update(JSON.stringify(modules)).digest('hex'),
    modules,
  }
}
const inputs = async () => ({
  executable: await identity(executable),
  libcef: await identity(resolve(dirname(executable), 'libcef.so')),
  sdk: await identity(sdk),
  sdkModules: await sdkManifest(),
  playbackVerifier: await identity(verifier),
})
const required = [
  ['vp8-vorbis', 'file'],
  ['vp8-opus', 'file'],
  ['vp9-opus', 'file'],
  ['vp9-opus', 'mse'],
  ['av1-opus', 'file'],
  ['h264-only', 'file'],
  ['h264-aac', 'file'],
  ['h264-aac', 'mse'],
  ['aac', 'file'],
  ['mp3', 'file'],
  ['wav-pcm', 'file'],
  ['ogg-vorbis', 'file'],
  ['ogg-opus', 'file'],
  ['flac', 'file'],
]
if (profile === 'web-hevc') required.push(['hevc-only', 'file'])
report.requirements = required.map(([fixture, mode]) => ({ fixture, mode }))
const childRun = async () => {
  const stdout = createWriteStream(resolve(output, 'playback.stdout.log'))
  const stderr = createWriteStream(resolve(output, 'playback.stderr.log'))
  const stdoutDone = finished(stdout),
    stderrDone = finished(stderr)
  const child = spawn(process.execPath, ['--expose-gc', verifier], {
    detached: process.platform !== 'win32',
    stdio: ['ignore', 'pipe', 'pipe'],
    env: {
      ...process.env,
      SUITECUT_NATIVE_EXECUTABLE: executable,
      SUITECUT_MEDIA_SDK: sdk,
      SUITECUT_MEDIA_OUTPUT: resolve(output, 'playback'),
      SUITECUT_MEDIA_BACKENDS: 'cef',
      SUITECUT_MEDIA_FIXTURES: '',
      SUITECUT_MEDIA_MODES: '',
      SUITECUT_MEDIA_MUTED: '0',
      SUITECUT_MEDIA_SEPARATE_WEBM: '1',
    },
  })
  child.stdout.pipe(stdout)
  child.stderr.pipe(stderr)
  child.stdout.on('data', (chunk) => process.stdout.write(chunk))
  let timedOut = false,
    cleanupSignalRequested = false,
    hardKill
  const signal = (name) => {
    cleanupSignalRequested = true
    try {
      if (process.platform !== 'win32' && child.pid) process.kill(-child.pid, name)
      else child.kill(name)
    } catch (error) {
      if (error?.code !== 'ESRCH') throw error
    }
  }
  const deadline = setTimeout(() => {
    timedOut = true
    signal('SIGTERM')
    hardKill = setTimeout(() => signal('SIGKILL'), 2000)
  }, timeoutMs)
  try {
    const exit = await new Promise((done, reject) => {
      child.once('error', reject)
      child.once('close', (code, exitSignal) => done({ code, signal: exitSignal }))
    })
    await Promise.all([stdoutDone, stderrDone])
    return { ...exit, timedOut, cleanupSignalRequested }
  } finally {
    clearTimeout(deadline)
    if (hardKill && !timedOut) clearTimeout(hardKill)
    // The timeout's final group signal is kept until it fires: the direct child
    // may exit before an unresponsive browser descendant leaves the group.
  }
}
try {
  report.inputs = await inputs()
  report.mediaProcess = await childRun()
  const media = JSON.parse(await readFile(resolve(output, 'playback/result.json'), 'utf8'))
  report.mediaReport = 'playback/result.json'
  report.inputsAfter = await inputs()
  report.inputIntegrity = JSON.stringify(report.inputs) === JSON.stringify(report.inputsAfter)
  const backend =
    media.results.length === 1 && media.results[0].backend === 'cef' ? media.results[0] : undefined
  report.runtimeVersion = backend?.version ?? null
  report.identityMatchesPlayback =
    Boolean(backend) &&
    backend.binarySHA256 === report.inputs.executable.sha256 &&
    backend.librarySHA256 === report.inputs.libcef.sha256 &&
    media.sdkSHA256 === report.inputs.sdk.sha256 &&
    resolve(backend.executablePath) === executable &&
    resolve(media.sdk) === sdk
  report.fixtureEvidence = media.generated
  report.advertisedOnlyForReference = backend?.advertised ?? null
  const requiredKeys = new Set(required.map(([fixture, mode]) => `${fixture}:${mode}`))
  for (const [fixture, mode] of [
    ...required,
    ...(profile === 'web' ? [['hevc-only', 'file']] : []),
  ]) {
    const test = backend?.tests.find((item) => item.id === fixture && item.mode === mode)
    const generated = media.generated.find((item) => item.id === fixture)
    const video = test?.kind === 'video',
      audioTrack = !['h264-only', 'hevc-only'].includes(fixture)
    const checks = {
      fixtureGenerated: generated?.status === 'GENERATED',
      actualPlaybackPassed: test?.outcome === 'PASS_ACTUAL_PLAYBACK',
      trustedPlayClick: test?.trustedPlayClick === true,
      actualTimeAdvanced:
        test?.initialPlayback?.currentTime >= 0.25 && test?.actualTimeAdvance === true,
      playbackEnded: test?.final?.ended === true,
      noMediaError: Boolean(test) && test.final?.error === null && test.final?.playError === null,
      pauseResume: test?.pause?.passed === true,
      seek: mode === 'mse' || test?.seek?.passed === true,
      decodedVideoFrames: !video || test?.final?.totalVideoFrames > 0,
      renderedVideoFrames: !video || test?.final?.frameCallbacks > 0,
      videoDimensions: !video || (test?.final?.width > 0 && test?.final?.height > 0),
      decodedAudioBytes: !audioTrack || test?.final?.decodedAudioBytes > 0,
      mediaSourceAppended: mode !== 'mse' || test?.final?.appendedChunks > 0,
      unmutedPlayback: test?.playbackMuted === false && media.playbackMuted === false,
    }
    const accepted = Object.values(checks).every((value) => value === true)
    const needed = requiredKeys.has(`${fixture}:${mode}`)
    report.cases.push({
      fixture,
      mode,
      required: needed,
      accepted,
      outcome: test?.outcome ?? 'MISSING_CASE',
      checks,
      initialPlayback: test?.initialPlayback ?? null,
      final: test?.final ?? null,
      fixtureSHA256: generated?.sha256 ?? null,
      actualStreams: generated?.probe?.streams ?? null,
    })
    if (needed && !accepted)
      report.failures.push({
        fixture,
        mode,
        outcome: test?.outcome ?? 'MISSING_CASE',
        failedChecks: Object.keys(checks).filter((key) => checks[key] !== true),
      })
  }
  report.nativeShutdown = backend?.shutdown ?? null
  const harnessPassed =
    report.mediaProcess.code === 0 &&
    report.mediaProcess.signal === null &&
    !report.mediaProcess.timedOut &&
    !backend?.launchError &&
    !backend?.shutdownError &&
    backend?.shutdown?.code === 0 &&
    backend?.shutdown?.signal === null
  report.status =
    !report.inputIntegrity || !report.identityMatchesPlayback
      ? 'FAIL_RUNTIME_IDENTITY'
      : !harnessPassed
        ? 'FAIL_HARNESS'
        : report.failures.length
          ? 'FAIL_CODEC_REQUIREMENTS'
          : 'PASS'
} catch (error) {
  report.error = error instanceof Error ? error.message : 'Unexpected verification error'
  report.stack = error instanceof Error ? error.stack : undefined
}
report.elapsedMs = performance.now() - started
await writeFile(resolve(output, 'acceptance.json'), JSON.stringify(report, null, 2) + '\n')
console.log(
  JSON.stringify({
    status: report.status,
    profile,
    runtimeVersion: report.runtimeVersion,
    requiredCases: report.requirements.length,
    failedRequiredCases: report.failures.length,
    report: resolve(output, 'acceptance.json'),
  }),
)
if (report.status !== 'PASS') process.exitCode = 1
