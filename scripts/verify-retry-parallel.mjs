/* eslint-disable @typescript-eslint/no-unsafe-argument, @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return */
import { spawnSync } from 'node:child_process'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

const compiledLibrary = new URL('../.suitecut/retry-parallel-lib/', import.meta.url)
const manifestModule = new URL('manifest.js', compiledLibrary).href
const renderModule = new URL('render.js', compiledLibrary).href
const [{ decodeManifest }, { renderSuiteCut }] = await Promise.all([
  import(manifestModule),
  import(renderModule),
])

const expectedCases = new Map([
  ['ruby', '#dc2626'],
  ['azure', '#2563eb'],
])

function assert(condition, message) {
  if (!condition) throw new Error(message)
}

function attemptKey(caseName, retry) {
  return `${caseName}:retry=${String(retry)}`
}

function hexRgb(value) {
  return [1, 3, 5].map((offset) => Number.parseInt(value.slice(offset, offset + 2), 16))
}

function centerPixel(executable, outputPath, atMs) {
  const result = spawnSync(
    executable,
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-ss',
      (Math.max(0, atMs) / 1_000).toFixed(3),
      '-i',
      outputPath,
      '-frames:v',
      '1',
      '-vf',
      'format=rgb24,crop=1:1:160:90',
      '-f',
      'rawvideo',
      'pipe:1',
    ],
    { encoding: null, maxBuffer: 1024 * 1024 },
  )
  if (result.error !== undefined) throw result.error
  if (result.status !== 0 || result.stdout.length < 3) {
    throw new Error(`Could not sample ${outputPath}: ${result.stderr.toString('utf8').trim()}`)
  }
  return [...result.stdout.subarray(0, 3)]
}

function assertUnique(values, label) {
  assert(new Set(values).size === values.length, `${label} crossed attempt boundaries`)
}

const manifestPath = resolve(process.argv[2] ?? '.suitecut/retry-parallel.json')
const manifest = decodeManifest(JSON.parse(await readFile(manifestPath, 'utf8')))
assert(manifest.status === 'passed', `Expected a passing run, received ${manifest.status}`)
assert(
  manifest.tests.length === expectedCases.size,
  'The manifest must contain both parallel tests',
)

const attempts = []
const allIds = { attempts: [], pages: [], events: [], artifacts: [], media: [] }
const allArtifactPaths = []

for (const [caseName, color] of expectedCases) {
  const recordedTest = manifest.tests.find((test) => test.title.startsWith(`${caseName} `))
  assert(recordedTest !== undefined, `Missing ${caseName} from the manifest`)
  assert(recordedTest.projectName === 'chromium-retry-parallel', `${caseName} used another project`)
  assert(recordedTest.attempts.length === 2, `${caseName} did not record exactly two attempts`)

  for (const retry of [0, 1]) {
    const attempt = recordedTest.attempts.find((candidate) => candidate.retry === retry)
    assert(attempt !== undefined, `Missing ${attemptKey(caseName, retry)}`)
    assert(
      attempt.status === (retry === 0 ? 'failed' : 'passed'),
      `${attemptKey(caseName, retry)} has status ${attempt.status}`,
    )
    assert(attempt.pages.length === 1, `${attemptKey(caseName, retry)} recorded extra pages`)

    const pageIds = new Set(attempt.pages.map((page) => page.id))
    assert(
      attempt.events.every((event) => pageIds.has(event.pageId)),
      `${attemptKey(caseName, retry)} contains an event from another attempt`,
    )
    assert(
      attempt.artifacts.every(
        (artifact) => artifact.pageId === undefined || pageIds.has(artifact.pageId),
      ),
      `${attemptKey(caseName, retry)} contains an artifact from another attempt`,
    )
    assert(
      attempt.videoTiming.every((timing) => pageIds.has(timing.pageId)),
      `${attemptKey(caseName, retry)} contains video timing from another attempt`,
    )

    const checkpointEvent = attempt.events.find((event) => event.type === 'checkpoint')
    const sourceVideo = attempt.artifacts.find((artifact) => artifact.role === 'source-video')
    const checkpoint = attempt.artifacts.find((artifact) => artifact.role === 'checkpoint')
    const markerName = `attempt-marker-${caseName}-retry-${String(retry)}.json`
    const markerArtifact = attempt.artifacts.find((artifact) => artifact.name === markerName)
    assert(checkpointEvent !== undefined, `${attemptKey(caseName, retry)} lacks its event`)
    assert(sourceVideo !== undefined, `${attemptKey(caseName, retry)} lacks its source video`)
    assert(checkpoint !== undefined, `${attemptKey(caseName, retry)} lacks its checkpoint`)
    assert(
      markerArtifact !== undefined,
      `${attemptKey(caseName, retry)} lacks its marker attachment`,
    )
    assert(
      sourceVideo.name.includes(attempt.id),
      `${attemptKey(caseName, retry)} source attachment is not attempt-scoped`,
    )
    assert(
      checkpoint.name.includes(attempt.id),
      `${attemptKey(caseName, retry)} checkpoint attachment is not attempt-scoped`,
    )

    const marker = JSON.parse(await readFile(markerArtifact.path, 'utf8'))
    assert(marker.caseName === caseName, `${attemptKey(caseName, retry)} loaded another marker`)
    assert(marker.retry === retry, `${attemptKey(caseName, retry)} loaded another retry marker`)
    assert(marker.color === color, `${attemptKey(caseName, retry)} loaded another color marker`)
    assert(
      checkpointEvent.label === marker.sentinel,
      `${attemptKey(caseName, retry)} mixed its event and attachment`,
    )

    attempts.push({
      attempt,
      caseName,
      color,
      marker,
      recordedTest,
      sourceVideo,
    })
    allIds.attempts.push(attempt.id)
    allIds.pages.push(...attempt.pages.map((page) => page.id))
    allIds.events.push(...attempt.events.map((event) => event.id))
    allIds.artifacts.push(...attempt.artifacts.map((artifact) => artifact.id))
    allIds.media.push(...attempt.media.map((media) => media.id))
    allArtifactPaths.push(...attempt.artifacts.map((artifact) => artifact.path))
  }
}

for (const [label, values] of Object.entries(allIds)) assertUnique(values, `${label} IDs`)
assertUnique(allArtifactPaths, 'artifact paths')

const firstAttempts = attempts.filter(({ attempt }) => attempt.retry === 0)
assertUnique(
  firstAttempts.map(({ marker }) => marker.workerIndex),
  'first-attempt worker indexes',
)
assertUnique(
  firstAttempts.map(({ marker }) => marker.parallelIndex),
  'first-attempt parallel indexes',
)
const latestStart = Math.max(...firstAttempts.map(({ marker }) => marker.bodyStartedAtEpochMs))
const earliestEnd = Math.min(...firstAttempts.map(({ marker }) => marker.bodyEndedAtEpochMs))
assert(latestStart < earliestEnd, 'The first attempts did not overlap in separate workers')

const renderDirectory = resolve(dirname(manifestPath), 'retry-parallel-renders')
await mkdir(renderDirectory, { recursive: true })
const sourcePaths = attempts.map(({ sourceVideo }) => sourceVideo.path)
const renderEvidence = []

for (const { attempt, caseName, color, recordedTest, sourceVideo } of attempts) {
  const outputPath = join(renderDirectory, `${caseName}-retry-${String(attempt.retry)}.mp4`)
  const report = await renderSuiteCut({
    manifestPath,
    outputPath,
    selection: { testId: recordedTest.id, retry: attempt.retry },
    config: {
      narrationEnabled: false,
      output: { width: 320, height: 180, framesPerSecond: 30, quality: 'standard' },
    },
  })
  assert(report.status === 'rendered', `${attemptKey(caseName, attempt.retry)} did not render`)
  assert(
    report.attemptId === attempt.id,
    `${attemptKey(caseName, attempt.retry)} selected another attempt`,
  )
  assert(
    report.testId === recordedTest.id,
    `${attemptKey(caseName, attempt.retry)} selected another test`,
  )
  assert(
    report.edits.every((edit) => attempt.pages.some((page) => page.id === edit.pageId)),
    `${attemptKey(caseName, attempt.retry)} rendered another attempt's page`,
  )
  const reportArguments = report.ffmpeg?.args ?? []
  assert(
    reportArguments.includes('<input-1>'),
    `${attemptKey(caseName, attempt.retry)} lacks redacted source evidence`,
  )
  assert(
    sourcePaths.every((path) => !reportArguments.some((argument) => argument.includes(path))),
    `${attemptKey(caseName, attempt.retry)} leaked an absolute source path`,
  )

  const executable = report.ffmpeg?.executable
  assert(executable !== undefined, `${attemptKey(caseName, attempt.retry)} lacks FFmpeg evidence`)
  const pixel = centerPixel(executable, outputPath, report.presentationDurationMs - 100)
  const expectedPixel = hexRgb(color)
  assert(
    pixel.every((channel, index) => Math.abs(channel - expectedPixel[index]) <= 20),
    `${attemptKey(caseName, attempt.retry)} rendered RGB ${pixel.join(',')} instead of ${expectedPixel.join(',')}`,
  )

  renderEvidence.push({
    attemptId: attempt.id,
    caseName,
    retry: attempt.retry,
    outputPath,
    pageIds: [...new Set(report.edits.map((edit) => edit.pageId))],
    sourceVideoPath: sourceVideo.path,
    sampledRgb: pixel,
  })
}

const evidencePath = resolve(dirname(manifestPath), 'retry-parallel-evidence.json')
const evidence = {
  manifestPath,
  parallelFirstAttempts: firstAttempts.map(({ attempt, caseName, marker }) => ({
    attemptId: attempt.id,
    caseName,
    parallelIndex: marker.parallelIndex,
    workerIndex: marker.workerIndex,
    startedAt: attempt.clock.startedAt,
    durationMs: attempt.durationMs,
    bodyStartedAtEpochMs: marker.bodyStartedAtEpochMs,
    barrierReleasedAtEpochMs: marker.barrierReleasedAtEpochMs,
    bodyEndedAtEpochMs: marker.bodyEndedAtEpochMs,
  })),
  attempts: attempts.map(({ attempt, caseName, marker, sourceVideo }) => ({
    attemptId: attempt.id,
    caseName,
    retry: attempt.retry,
    status: attempt.status,
    workerIndex: marker.workerIndex,
    parallelIndex: marker.parallelIndex,
    pageIds: attempt.pages.map((page) => page.id),
    eventIds: attempt.events.map((event) => event.id),
    attachmentPaths: attempt.artifacts.map((artifact) => artifact.path),
    sourceVideoPath: sourceVideo.path,
  })),
  renders: renderEvidence,
}
await writeFile(evidencePath, `${JSON.stringify(evidence, null, 2)}\n`, 'utf8')

process.stdout.write(
  `Verified ${attempts.length} isolated attempts across ${firstAttempts.length} parallel workers and ${renderEvidence.length} selected renders. Evidence: ${evidencePath}\n`,
)
