/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript benchmark needs JSDoc types. */
import { execFile, spawn } from 'node:child_process'
import console from 'node:console'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { arch, cpus, platform, release } from 'node:os'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { parseArgs, promisify } from 'node:util'

import * as z from 'zod'

const { values } = parseArgs({
  options: {
    'duration-ms': { type: 'string', default: '10000' },
    trials: { type: 'string', default: '3' },
    width: { type: 'string', default: '1920' },
    height: { type: 'string', default: '1080' },
    fps: { type: 'string', default: '60' },
    workload: { type: 'string', default: 'canvas' },
    scenario: { type: 'string', default: 'custom' },
    output: { type: 'string' },
  },
})
const config = z
  .object({
    durationMs: z.coerce.number().int().min(1000).max(120000),
    trials: z.coerce.number().int().min(1).max(10),
    width: z.coerce.number().int().min(320).max(3840).multipleOf(2),
    height: z.coerce.number().int().min(320).max(2160).multipleOf(2),
    fps: z.coerce.number().pipe(z.union([z.literal(30), z.literal(60)])),
    workload: z.enum(['static', 'canvas', 'dom', 'multi-page']),
    scenario: z.string().trim().min(1),
  })
  .parse({
    durationMs: values['duration-ms'],
    trials: values.trials,
    width: values.width,
    height: values.height,
    fps: values.fps,
    workload: values.workload,
    scenario: values.scenario,
  })
if (!['darwin', 'linux'].includes(platform()))
  throw new Error('This beta benchmark currently supports macOS/Linux process sampling')
if (!process.env.SUITECUT_NATIVE_EXECUTABLE)
  throw new Error('Set SUITECUT_NATIVE_EXECUTABLE before benchmarking')
const directory = resolve(
  values.output ?? `.suitecut/beta-benchmark/${new Date().toISOString().replaceAll(':', '-')}`,
)
await mkdir(directory, { recursive: true })
const execute = promisify(execFile)
let interrupted = false
process.once('SIGINT', () => {
  interrupted = true
})
process.once('SIGTERM', () => {
  interrupted = true
})
const nullableMetric = z.number().nullable()
const Result = z
  .object({
    backend: z.enum(['native', 'playwright']),
    browserVersion: z.string(),
    workload: z.enum(['static', 'canvas', 'dom', 'multi-page']),
    startupMs: z.number(),
    totalWallMs: z.number(),
    finalizationMs: z.number(),
    fileBytes: z.number(),
    sourceCount: z.number(),
    animationFrames: z.number(),
    decodedActiveFrames: nullableMetric,
    uniqueFrames: nullableMetric,
    uniqueFps: nullableMetric,
    repeatedFramePercent: nullableMetric,
    longestHeldFrameMs: nullableMetric,
  })
  .passthrough()

/**
 * Converts the process time format emitted by ps into seconds.
 * @param {string} time
 */
function cpuSeconds(time) {
  const pieces = time.split(':').map(Number)
  return pieces.reduce((sum, part) => sum * 60 + part, 0)
}

/**
 * Measures one isolated worker and its process tree.
 * @param {'native' | 'playwright'} backend
 * @param {number} trial
 * @param {boolean} warmup
 */
async function run(backend, trial, warmup) {
  if (interrupted) throw new Error('Benchmark interrupted')
  const runDirectory = resolve(directory, `${warmup ? 'warmup' : `trial-${trial}`}-${backend}`)
  const options = {
    ...config,
    backend,
    directory: runDirectory,
    durationMs: warmup ? 1000 : config.durationMs,
  }
  const child = spawn(
    process.execPath,
    ['scripts/benchmark-beta-worker.mjs', JSON.stringify(options)],
    { stdio: ['ignore', 'pipe', 'pipe'], detached: true },
  )
  if (!child.pid) throw new Error('Benchmark worker did not start')
  const rootPid = child.pid
  let diagnostics = ''
  child.stdout.resume()
  child.stderr.on('data', (chunk) => {
    diagnostics = (diagnostics + String(chunk)).slice(-16000)
  })
  let closed = false
  /** @type {Promise<void>} */
  const completed = new Promise((resolvePromise, reject) => {
    child.once('error', reject)
    child.once('close', (code) => {
      closed = true
      if (code === 0) resolvePromise()
      else reject(new Error(`${backend} benchmark failed (${code}): ${diagnostics}`))
    })
  })
  void completed.catch(() => undefined)
  const known = new Set([rootPid])
  /** @type {Map<number, number>} */
  const cpuByPid = new Map()
  let peakRssBytes = 0
  let samples = 0
  const started = performance.now()
  try {
    while (!closed) {
      if (interrupted) throw new Error('Benchmark interrupted')
      if (performance.now() - started > options.durationMs + 120000)
        throw new Error('Benchmark worker timed out')
      const { stdout } = await execute('ps', ['-axo', 'pid=,ppid=,rss=,time='], {
        maxBuffer: 4 * 1024 * 1024,
      })
      const rows = stdout
        .trim()
        .split('\n')
        .map((line) => {
          const [pid, parent, rss, cpu] = line.trim().split(/\s+/u)
          return {
            pid: Number(pid),
            parent: Number(parent),
            rss: Number(rss) * 1024,
            cpu: cpuSeconds(cpu ?? '0'),
          }
        })
      let added = true
      while (added) {
        added = false
        for (const row of rows)
          if (known.has(row.parent) && !known.has(row.pid)) {
            known.add(row.pid)
            added = true
          }
      }
      let rss = 0
      for (const row of rows)
        if (known.has(row.pid)) {
          rss += row.rss
          cpuByPid.set(row.pid, Math.max(cpuByPid.get(row.pid) ?? 0, row.cpu))
        }
      peakRssBytes = Math.max(peakRssBytes, rss)
      samples++
      if (!closed) await delay(200)
    }
    await completed
    const data = Result.parse(
      JSON.parse(await readFile(resolve(runDirectory, 'result.json'), 'utf8')),
    )
    const sampledProcessTreeCpuSeconds = [...cpuByPid.values()].reduce(
      (sum, value) => sum + value,
      0,
    )
    const result = {
      ...data,
      trial,
      warmup,
      peakProcessTreeRssMiB: peakRssBytes / 1048576,
      sampledProcessTreeCpuSeconds,
      sampledProcessTreeCpuPercent:
        data.totalWallMs > 0 ? (sampledProcessTreeCpuSeconds * 100000) / data.totalWallMs : 0,
      resourceSamples: samples,
    }
    const cadence = result.uniqueFps === null ? 'cadence n/a' : `${result.uniqueFps.toFixed(1)} FPS`
    console.log(
      `${warmup ? 'warmup' : `trial ${trial}`} ${backend}: ${cadence}, ${result.peakProcessTreeRssMiB.toFixed(0)} MiB peak RSS, ${result.sampledProcessTreeCpuPercent.toFixed(0)}% sampled CPU`,
    )
    return result
  } finally {
    if (!closed) {
      child.kill('SIGTERM')
      await Promise.race([completed.catch(() => undefined), delay(5000)])
      try {
        if (!closed) process.kill(-rootPid, 'SIGKILL')
      } catch {
        /* The process group may already have exited. */
      }
      await completed.catch(() => undefined)
    }
  }
}

/** @type {Awaited<ReturnType<typeof run>>[]} */
const results = []
/** @type {('native' | 'playwright')[]} */
const backends = ['native', 'playwright']
for (const backend of backends) results.push(await run(backend, 0, true))
for (let trial = 1; trial <= config.trials; trial++) {
  const order = trial % 2 ? backends : backends.toReversed()
  for (const backend of order) {
    results.push(await run(backend, trial, false))
    await writeFile(resolve(directory, 'runs.json'), JSON.stringify(results, null, 2))
  }
}

/**
 * Returns the median of a non-empty numeric sample.
 * @param {number[]} values
 */
function median(values) {
  const sorted = values.toSorted((a, b) => a - b)
  const index = Math.floor(sorted.length / 2)
  return sorted.length % 2 ? sorted[index] : (sorted[index - 1] + sorted[index]) / 2
}

/**
 * Returns the median of the available values in a nullable sample.
 * @param {(number | null)[]} values
 */
function medianNullable(values) {
  const present = values.filter((value) => value !== null)
  return present.length === 0 ? null : median(present)
}

const summary = ['native', 'playwright'].map((backend) => {
  const runs = results.filter((run) => !run.warmup && run.backend === backend)
  return {
    backend,
    uniqueFps: medianNullable(runs.map((run) => run.uniqueFps)),
    repeatedFramePercent: medianNullable(runs.map((run) => run.repeatedFramePercent)),
    longestHeldFrameMs: medianNullable(runs.map((run) => run.longestHeldFrameMs)),
    startupMs: median(runs.map((run) => run.startupMs)),
    finalizationMs: median(runs.map((run) => run.finalizationMs)),
    totalWallMs: median(runs.map((run) => run.totalWallMs)),
    fileMiB: median(runs.map((run) => run.fileBytes / 1048576)),
    peakProcessTreeRssMiB: median(runs.map((run) => run.peakProcessTreeRssMiB)),
    sampledProcessTreeCpuSeconds: median(runs.map((run) => run.sampledProcessTreeCpuSeconds)),
    sampledProcessTreeCpuPercent: median(runs.map((run) => run.sampledProcessTreeCpuPercent)),
  }
})
let revision = 'unknown'
let dirty = true
try {
  revision = (await execute('git', ['rev-parse', '--short=12', 'HEAD'])).stdout.trim()
  dirty =
    (
      await execute('git', ['status', '--porcelain', '--', '.', ':(exclude)node_modules'])
    ).stdout.trim().length > 0
} catch {
  /* Git metadata is optional for a local benchmark. */
}
const report = {
  schemaVersion: 2,
  createdAt: new Date().toISOString(),
  revision,
  dirty,
  host: {
    platform: platform(),
    arch: arch(),
    release: release(),
    cpu: cpus()[0]?.model,
    logicalCpus: cpus().length,
    node: process.version,
  },
  config,
  methodology: {
    workload:
      'Local fixture served inside each isolated worker. Static, canvas, DOM-scroll, and multi-page workloads use the same authored flow on both backends.',
    ordering:
      'One excluded warmup run per backend, then alternating paired trials in fresh worker/browser/encoder process trees.',
    frameMetric:
      'Canvas and DOM workloads encode monotonically increasing frame IDs into pixels; FFmpeg decodes them from source WebM. Static and multi-page workloads omit cadence scores.',
    scope:
      'End-to-end silent recording from worker launch through media probing. Native uses CEF/I420/VP9; Playwright uses Chromium screencast MJPEG/VP9. Audio is off because Playwright recording does not expose page-audio capture; embedded recording audio and both live-stream audio paths are verified separately. Streaming performance is outside this recording suite.',
    resourceMetric:
      '200 ms ps samples sum RSS and cumulative CPU across the worker and discovered descendants. CPU percent is sampled CPU seconds divided by total wall time and may exceed 100% across cores.',
    limitations:
      'RSS can double-count shared pages, short-lived processes can escape sampling, and GPU memory and energy are not measured. Results describe this host and revision, not every website or platform.',
  },
  summary,
  runs: results,
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
/**
 * Formats a nullable report metric.
 * @param {number | null} value
 * @param {number} [digits]
 */
const metric = (value, digits = 1) => (value === null ? 'n/a' : value.toFixed(digits))
const table = [
  '| Backend | Distinct FPS | Repeated | Startup ms | Finalize ms | Peak RSS MiB | CPU | File MiB |',
  '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |',
  ...summary.map(
    (row) =>
      `| ${row.backend} | ${metric(row.uniqueFps)} | ${row.repeatedFramePercent === null ? 'n/a' : `${metric(row.repeatedFramePercent)}%`} | ${row.startupMs.toFixed(0)} | ${row.finalizationMs.toFixed(0)} | ${row.peakProcessTreeRssMiB.toFixed(0)} | ${row.sampledProcessTreeCpuPercent.toFixed(0)}% | ${row.fileMiB.toFixed(2)} |`,
  ),
].join('\n')
await writeFile(
  resolve(directory, 'report.md'),
  `# ${config.scenario}\n\n${config.width}x${config.height}, ${config.workload}, requested ${config.fps} FPS, ${config.trials} trials of ${config.durationMs / 1000}s per backend. Values are medians.\n\n${table}\n\n${Object.values(report.methodology).join('\n\n')}\n`,
)
console.log(`Report: ${resolve(directory, 'report.md')}`)
