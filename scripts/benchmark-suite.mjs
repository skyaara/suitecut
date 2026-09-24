/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript suite keeps inferred report types with JSDoc. */
import { execFile } from 'node:child_process'
import console from 'node:console'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import process from 'node:process'
import { parseArgs, promisify } from 'node:util'

import * as z from 'zod'

const nullableMetric = z.number().nullable()
const BackendSummary = z.object({
  backend: z.enum(['native', 'playwright']),
  uniqueFps: nullableMetric,
  repeatedFramePercent: nullableMetric,
  longestHeldFrameMs: nullableMetric,
  startupMs: z.number(),
  finalizationMs: z.number(),
  totalWallMs: z.number(),
  fileMiB: z.number(),
  peakProcessTreeRssMiB: z.number(),
  sampledProcessTreeCpuSeconds: z.number(),
  sampledProcessTreeCpuPercent: z.number(),
})
const ScenarioReport = z.object({
  revision: z.string(),
  dirty: z.boolean(),
  host: z.object({}).passthrough(),
  methodology: z.record(z.string(), z.string()),
  summary: z.array(BackendSummary),
  runs: z.array(
    z
      .object({
        backend: z.enum(['native', 'playwright']),
        warmup: z.boolean(),
      })
      .passthrough(),
  ),
})

const { values } = parseArgs({
  options: {
    output: { type: 'string' },
    trials: { type: 'string', default: '3' },
    'site-data': { type: 'string' },
  },
})
const trials = z.coerce.number().int().min(1).max(10).parse(values.trials)
if (!process.env.SUITECUT_NATIVE_EXECUTABLE)
  throw new Error('Set SUITECUT_NATIVE_EXECUTABLE before benchmarking')
const directory = resolve(
  values.output ?? `.suitecut/benchmark-suite/${new Date().toISOString().replaceAll(':', '-')}`,
)
await mkdir(directory, { recursive: true })
const scenarios = [
  {
    id: 'static-1080p30',
    name: 'Static page · 1080p30',
    description: 'A settled page with no animation after load.',
    workload: 'static',
    width: 1920,
    height: 1080,
    fps: 30,
    durationMs: 5000,
  },
  {
    id: 'canvas-720p30',
    name: 'Canvas motion · 720p30',
    description: 'Continuous requestAnimationFrame canvas motion at a lightweight output size.',
    workload: 'canvas',
    width: 1280,
    height: 720,
    fps: 30,
    durationMs: 5000,
  },
  {
    id: 'canvas-1080p60',
    name: 'Canvas motion · 1080p60',
    description: 'Continuous canvas motion at the beta default 1080p60 capture target.',
    workload: 'canvas',
    width: 1920,
    height: 1080,
    fps: 60,
    durationMs: 6000,
  },
  {
    id: 'dom-1080p60',
    name: 'DOM and scroll · 1080p60',
    description:
      'A dense card grid continuously translated to exercise layout, paint, and capture.',
    workload: 'dom',
    width: 1920,
    height: 1080,
    fps: 60,
    durationMs: 6000,
  },
  {
    id: 'multi-page-1080p60',
    name: 'Two-page switching · 1080p60',
    description: 'Two active browser sources with two authored page switches during recording.',
    workload: 'multi-page',
    width: 1920,
    height: 1080,
    fps: 60,
    durationMs: 6000,
  },
  {
    id: 'canvas-4k30',
    name: 'Canvas motion · 4K30',
    description: 'Continuous canvas motion at the maximum 3840x2160 capture size.',
    workload: 'canvas',
    width: 3840,
    height: 2160,
    fps: 30,
    durationMs: 5000,
  },
]
const execute = promisify(execFile)
/** @type {Array<(typeof scenarios)[number] & {report: z.infer<typeof ScenarioReport>}>} */
const reports = []
for (const [index, scenario] of scenarios.entries()) {
  console.log(`\n[${index + 1}/${scenarios.length}] ${scenario.name}`)
  const scenarioDirectory = resolve(directory, scenario.id)
  const result = await execute(
    process.execPath,
    [
      'scripts/benchmark-beta.mjs',
      '--scenario',
      scenario.name,
      '--workload',
      scenario.workload,
      '--width',
      String(scenario.width),
      '--height',
      String(scenario.height),
      '--fps',
      String(scenario.fps),
      '--duration-ms',
      String(scenario.durationMs),
      '--trials',
      String(trials),
      '--output',
      scenarioDirectory,
    ],
    { maxBuffer: 16 * 1024 * 1024, timeout: 30 * 60 * 1000 },
  )
  process.stdout.write(result.stdout)
  process.stderr.write(result.stderr)
  const report = ScenarioReport.parse(
    JSON.parse(await readFile(resolve(scenarioDirectory, 'report.json'), 'utf8')),
  )
  reports.push({ ...scenario, report })
}

/**
 * Computes the native result as a percentage difference from Playwright.
 * @param {number | null} native
 * @param {number | null} playwright
 */
const metricDelta = (native, playwright) =>
  native === null || playwright === null || playwright === 0
    ? null
    : ((native - playwright) / playwright) * 100
const publishedScenarios = reports.map(({ report, ...scenario }) => {
  const native = report.summary.find((row) => row.backend === 'native')
  const playwright = report.summary.find((row) => row.backend === 'playwright')
  if (!native || !playwright) throw new Error(`Missing backend summary for ${scenario.id}`)
  return {
    ...scenario,
    summary: { native, playwright },
    deltaPercent: {
      uniqueFps: metricDelta(native.uniqueFps, playwright.uniqueFps),
      repeatedFramePercent: metricDelta(
        native.repeatedFramePercent,
        playwright.repeatedFramePercent,
      ),
      startupMs: metricDelta(native.startupMs, playwright.startupMs),
      finalizationMs: metricDelta(native.finalizationMs, playwright.finalizationMs),
      peakProcessTreeRssMiB: metricDelta(
        native.peakProcessTreeRssMiB,
        playwright.peakProcessTreeRssMiB,
      ),
      sampledProcessTreeCpuPercent: metricDelta(
        native.sampledProcessTreeCpuPercent,
        playwright.sampledProcessTreeCpuPercent,
      ),
      fileMiB: metricDelta(native.fileMiB, playwright.fileMiB),
    },
    runs: report.runs.filter((run) => !run.warmup),
  }
})
const first = reports[0]?.report
if (!first) throw new Error('Benchmark suite produced no reports')
const aggregate = {
  schemaVersion: 1,
  createdAt: new Date().toISOString(),
  revision: first.revision,
  dirty: first.dirty,
  trials,
  host: first.host,
  methodology: first.methodology,
  scenarios: publishedScenarios,
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(aggregate, null, 2))
const rows = publishedScenarios.flatMap((scenario) => [
  `| ${scenario.name} | native | ${scenario.summary.native.uniqueFps?.toFixed(1) ?? 'n/a'} | ${scenario.summary.native.peakProcessTreeRssMiB.toFixed(0)} | ${scenario.summary.native.sampledProcessTreeCpuPercent.toFixed(0)}% | ${scenario.summary.native.startupMs.toFixed(0)} | ${scenario.summary.native.finalizationMs.toFixed(0)} |`,
  `| ${scenario.name} | playwright | ${scenario.summary.playwright.uniqueFps?.toFixed(1) ?? 'n/a'} | ${scenario.summary.playwright.peakProcessTreeRssMiB.toFixed(0)} | ${scenario.summary.playwright.sampledProcessTreeCpuPercent.toFixed(0)}% | ${scenario.summary.playwright.startupMs.toFixed(0)} | ${scenario.summary.playwright.finalizationMs.toFixed(0)} |`,
])
await writeFile(
  resolve(directory, 'report.md'),
  `# SuiteCut backend benchmark suite\n\nRevision ${aggregate.revision}; ${trials} measured trials per backend and scenario.\n\n| Scenario | Backend | Distinct FPS | Peak RSS MiB | Sampled CPU | Startup ms | Finalize ms |\n| --- | --- | ---: | ---: | ---: | ---: | ---: |\n${rows.join('\n')}\n\n${Object.values(aggregate.methodology).join('\n\n')}\n`,
)
if (values['site-data']) {
  const sitePath = resolve(values['site-data'])
  await mkdir(dirname(sitePath), { recursive: true })
  await writeFile(sitePath, JSON.stringify(aggregate, null, 2))
  console.log(`Site data: ${sitePath}`)
}
console.log(`Suite report: ${resolve(directory, 'report.md')}`)
