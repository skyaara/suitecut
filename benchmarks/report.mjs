// SPDX-License-Identifier: MIT
// Summarizes results.json into results.md and summary.json:
//   node benchmarks/report.mjs benchmarks/results/results.json
import console from 'node:console'
import { readFileSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import process from 'node:process'

import { Results } from './schema.mjs'

/** @typedef {import('./schema.mjs').BenchSite} BenchSite */

const file = process.argv[2] ?? ''
const data = Results.parse(JSON.parse(readFileSync(file, 'utf8')))

/**
 * The median of the numbers present; even counts average the middle two.
 * @param {(number | undefined)[]} values Samples, some missing.
 * @returns {number | undefined} Their median.
 */
const median = (values) => {
  const sorted = values
    .filter((value) => typeof value === 'number' && Number.isFinite(value))
    .sort((a, b) => a - b)
  if (!sorted.length) return undefined
  const middle = sorted.length >> 1
  const high = sorted[middle] ?? 0
  return sorted.length % 2 ? high : ((sorted[middle - 1] ?? high) + high) / 2
}
/**
 * A rounded number for a table cell.
 * @param {number | undefined} value The number.
 * @returns {string} The cell.
 */
const show = (value) => (value === undefined ? '–' : Math.round(value).toLocaleString('en-US'))
/**
 * Approximate tokens, as characters / 4.
 * @param {number | undefined} chars Length of a snapshot.
 * @returns {number | undefined} Its approximate token count.
 */
const tokens = (chars) => (chars === undefined ? undefined : chars / 4)
/**
 * The sum of a field over rows, missing values counting as zero.
 * @param {Record<string, Record<string, unknown>>} rows Rows by key.
 * @param {string} key The field.
 * @returns {number} The total.
 */
const sum = (rows, key) =>
  Object.values(rows).reduce(
    (total, row) => total + (typeof row[key] === 'number' ? row[key] : 0),
    0,
  )

const tools = [...new Set(data.runs.map((run) => run.tool))]
const ok = data.runs.filter((run) => !run.error)
const hosts = data.sites.map((url) => new URL(url).hostname)

/**
 * Medians for one tool across its rounds.
 * @param {string} tool Which tool to summarize.
 * @returns The tool's medians, per page and per task.
 */
function summarize(tool) {
  const runs = ok.filter((run) => run.tool === tool)
  /** @type {BenchSite[]} */
  const pages = runs.flatMap((run) => run.sites.filter((site) => !site.error))
  const perSite = Object.fromEntries(
    hosts.map((host) => {
      const rows = pages.filter((site) => site.host === host)
      return [
        host,
        {
          n: rows.length,
          loadMs: median(rows.map((site) => site.loadMs)),
          snapshotMs: median(rows.map((site) => site.snapshot?.ms)),
          snapshotTokens: tokens(median(rows.map((site) => site.snapshot?.chars))),
          fullSnapshotMs: median(rows.map((site) => site.snapshotFull?.ms)),
          fullSnapshotTokens: tokens(median(rows.map((site) => site.snapshotFull?.chars))),
          screenshotMs: median(rows.map((site) => site.screenshot?.ms)),
        },
      ]
    }),
  )
  const perTask = Object.fromEntries(
    data.tasks.map((task) => {
      const rows = runs.flatMap((run) => run.tasks.filter((entry) => entry.name === task.name))
      const passed = rows.filter((entry) => entry.ok)
      return [
        task.name,
        {
          passed: passed.length,
          runs: rows.length,
          ms: median(passed.map((entry) => entry.ms)),
          tokens: tokens(median(passed.map((entry) => entry.chars))),
          retries: rows.reduce((total, entry) => total + entry.retries, 0),
          retryErrors: [...new Set(rows.flatMap((entry) => entry.retryErrors))],
          errors: [...new Set(rows.filter((entry) => !entry.ok).map((entry) => entry.error ?? ''))],
        },
      ]
    }),
  )
  const attempts = runs.flatMap((run) => run.tasks)
  const environment = data.environment.suitecut
  return {
    version: runs[0]?.version ?? (typeof environment === 'string' ? environment : undefined),
    runs: runs.length,
    failedRuns: data.runs
      .filter((run) => run.tool === tool && run.error)
      .map((run) => run.error ?? ''),
    launchMs: median(runs.map((run) => run.launchMs)),
    closeMs: median(runs.map((run) => run.closeMs)),
    cpuSeconds: median(runs.map((run) => run.cpuSeconds)),
    peakMemoryMB: median(runs.map((run) => run.peakMemoryMB)),
    loadMs: median(pages.map((site) => site.loadMs)),
    snapshotMs: median(pages.map((site) => site.snapshot?.ms)),
    snapshotTokens: sum(perSite, 'snapshotTokens'),
    fullSnapshotTokens: sum(perSite, 'fullSnapshotTokens'),
    screenshotMs: median(pages.map((site) => site.screenshot?.ms)),
    siteErrors: runs.flatMap((run) =>
      run.sites.filter((site) => site.error).map((site) => `${site.host}: ${site.error ?? ''}`),
    ),
    tasksPassed: attempts.filter((entry) => entry.ok).length,
    tasksRun: attempts.length,
    retries: attempts.reduce((total, entry) => total + entry.retries, 0),
    taskMs: median(attempts.filter((entry) => entry.ok).map((entry) => entry.ms)),
    taskTokens: sum(perTask, 'tokens'),
    perSite,
    perTask,
  }
}
const summary = Object.fromEntries(tools.map((tool) => [tool, summarize(tool)]))

const env = data.environment
const loads = data.runs.map((run) => run.load1 ?? 0)
const lines = [
  '# Browser agent benchmark',
  '',
  `${String(env.date)}, ${String(env.os)}, ${String(env.cpu)} (${String(env.cores)} cores, ${String(env.memoryGB)} GB). ` +
    `suitecut ${String(env.suitecut)} (${String(env.commit ?? 'unknown commit')}), agent-browser ${String(env.agentBrowser ?? '–')}, ` +
    `browser-use ${String(env.browserUse ?? '–')} on ${String(env.chrome ?? 'Chrome')}. ${String(data.rounds)} cold rounds per tool; ` +
    `1-minute load average at round start ${show(Math.min(...loads))}–${show(Math.max(...loads))}.`,
  '',
  "CPU and memory cover every process a run starts (browser, renderers, GPU, driver, and agent-browser's daemon), sampled every 0.5 s; CPU is the median run's total, memory the peak of the summed resident sizes, which counts shared pages more than once.",
  '',
  "Snapshot and screenshot times are medians of 3 calls per page, then of all pages and rounds. Tokens are characters / 4 of the text an agent reads: suitecut `ariaSnapshot({ mode: 'ai', interactive: true })`, agent-browser `snapshot -i`, browser-use `llm_representation()`.",
  '',
  '## Summary',
  '',
  `| Tool | Launch ms | Snapshot ms | Snapshot tokens (${String(data.sites.length)} pages) | Screenshot ms | Tasks passed | Retries | Task ms | Task tokens (${String(data.tasks.length)} tasks) | CPU s per run | Peak memory MB |`,
  '|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|',
  ...Object.entries(summary).map(
    ([tool, row]) =>
      `| ${tool} | ${show(row.launchMs)} | ${show(row.snapshotMs)} | ${show(row.snapshotTokens)} | ${show(row.screenshotMs)} | ${String(row.tasksPassed)}/${String(row.tasksRun)} | ${String(row.retries)} | ${show(row.taskMs)} | ${show(row.taskTokens)} | ${show(row.cpuSeconds)} | ${show(row.peakMemoryMB)} |`,
  ),
  '',
  '## Tasks',
  '',
  `| Task | Goal | ${tools.map((tool) => `${tool}: passed / ms / tokens`).join(' | ')} |`,
  `|---|---|${tools.map(() => '--:').join('|')}|`,
  ...data.tasks.map(
    (task) =>
      `| ${task.name} | ${task.goal} | ${tools
        .map((tool) => {
          const row = summary[tool]?.perTask[task.name]
          return row
            ? `${String(row.passed)}/${String(row.runs)} / ${show(row.ms)} / ${show(row.tokens)}`
            : '–'
        })
        .join(' | ')} |`,
  ),
  '',
  '## Pages',
  '',
  '| Page | Tool | Load ms | Snapshot ms | Snapshot tokens | Full snapshot tokens | Screenshot ms |',
  '|---|---|--:|--:|--:|--:|--:|',
  ...hosts.flatMap((host) =>
    tools.map((tool) => {
      const row = summary[tool]?.perSite[host]
      return row
        ? `| ${host} | ${tool} | ${show(row.loadMs)} | ${show(row.snapshotMs)} | ${show(row.snapshotTokens)} | ${show(row.fullSnapshotTokens)} | ${show(row.screenshotMs)} |`
        : `| ${host} | ${tool} | – | – | – | – | – |`
    }),
  ),
  '',
  '## Failures',
  '',
]
const problems = Object.entries(summary).flatMap(([tool, row]) => [
  ...row.failedRuns.map((error) => `- ${tool}: run failed: ${error.slice(0, 300)}`),
  ...row.siteErrors.map((error) => `- ${tool}: ${error.slice(0, 300)}`),
  ...Object.entries(row.perTask).flatMap(([name, task]) =>
    task.errors.map((error) => `- ${tool} ${name}: ${error.slice(0, 300)}`),
  ),
])
lines.push(problems.length ? problems.join('\n') : 'None.', '')
// A retry is a step whose first try failed; say why, so a retried column is not a mystery.
const retried = Object.entries(summary).flatMap(([tool, row]) =>
  Object.entries(row.perTask).flatMap(([name, task]) =>
    task.retryErrors.map((error) => `- ${tool} ${name}: ${error.slice(0, 300)}`),
  ),
)
lines.push('## Retries', '', retried.length ? retried.join('\n') : 'None.', '')
const markdown = lines.join('\n')
writeFileSync(join(dirname(file), 'results.md'), markdown)
writeFileSync(
  join(dirname(file), 'summary.json'),
  JSON.stringify({ environment: env, rounds: data.rounds, tools: summary }, null, 2),
)
console.log(markdown)
