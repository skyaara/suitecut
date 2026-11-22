// SPDX-License-Identifier: MIT
// Runs the whole benchmark:
//   node benchmarks/run.mjs [--rounds 3] [--tools suitecut,agent-browser,browser-use]
//     [--sites news.ycombinator.com,...] [--tasks hn-open-comments,...] [--out DIR]
// --sites and --tasks run a subset (hosts and task names; empty for none); by default all run.
// Each round starts every tool cold (new browser, new profile) and rotates the
// order so no tool always runs first. Results land in benchmarks/results/.
import { execFileSync, spawn, spawnSync } from 'node:child_process'
import console from 'node:console'
import { mkdirSync, readFileSync, statfsSync, writeFileSync } from 'node:fs'
import { cpus, loadavg, release, totalmem } from 'node:os'
import { dirname, join } from 'node:path'
import process from 'node:process'
import { clearInterval, clearTimeout, setInterval, setTimeout } from 'node:timers'
import { fileURLToPath } from 'node:url'
import { parseArgs } from 'node:util'

import * as z from 'zod'

import { Run } from './schema.mjs'
import { sites as allSites, tasks as allTasks } from './tasks.mjs'

/** @typedef {import('./schema.mjs').BenchRun} BenchRun */

const here = dirname(fileURLToPath(import.meta.url))
const { values } = parseArgs({
  options: {
    rounds: { type: 'string', default: '3' },
    tools: { type: 'string', default: 'suitecut,agent-browser,browser-use' },
    out: { type: 'string', default: join(here, 'results') },
    sites: { type: 'string' },
    tasks: { type: 'string' },
  },
})
/**
 * The entries a comma-separated option names, or all of them.
 * @template T
 * @param {T[]} all Every entry.
 * @param {string | undefined} option The option's value.
 * @param {(entry: T) => string} key What the option names an entry by.
 * @returns {T[]} The selected entries.
 */
const select = (all, option, key) => {
  if (option === undefined) return all
  const names = option.split(',').filter(Boolean)
  const unknown = names.filter((name) => !all.some((entry) => key(entry) === name))
  if (unknown.length) throw new Error(`Unknown: ${unknown.join(', ')}`)
  return all.filter((entry) => names.includes(key(entry)))
}
const sites = select(allSites, values.sites, (url) => new URL(url).hostname)
const tasks = select(allTasks, values.tasks, (task) => task.name)
const rounds = Number(values.rounds)
const out = values.out
const python = process.env.BROWSER_USE_PYTHON ?? 'python3'
/** @type {Record<string, [string, string[]]>} */
const adapters = {
  suitecut: ['node', [join(here, 'adapters/suitecut.mjs')]],
  'agent-browser': ['node', [join(here, 'adapters/agent-browser.mjs')]],
  'browser-use': [python, ['-I', join(here, 'adapters/browser_use.py')]],
}
const tools = values.tools.split(',').filter((tool) => tool in adapters)
mkdirSync(join(out, 'snapshots'), { recursive: true })

/**
 * Runs a command for its output, if it is installed.
 * @param {string} command The program.
 * @param {string[]} args Its arguments.
 * @returns {string | undefined} What it printed.
 */
const quiet = (command, args) => {
  try {
    return execFileSync(command, args, {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    }).trim()
  } catch {
    return undefined
  }
}
/**
 * A file's text, if it exists.
 * @param {string} path The file.
 * @returns {string | undefined} Its text.
 */
const quietRead = (path) => {
  try {
    return readFileSync(path, 'utf8')
  } catch {
    return undefined
  }
}
const freeGB = () => {
  const disk = statfsSync('/')
  return (disk.bavail * disk.bsize) / 2 ** 30
}
const environment = {
  date: new Date().toISOString(),
  os: `${process.platform} ${release()}`,
  cpu: cpus()[0]?.model,
  cores: cpus().length,
  memoryGB: Math.round(totalmem() / 2 ** 30),
  node: process.version,
  suitecut: z
    .object({ version: z.string() })
    .safeParse(JSON.parse(quietRead(join(here, '../package.json')) ?? '{}')).data?.version,
  // A run on uncommitted changes says so, so its numbers are not credited to the commit.
  commit: quiet('git', ['-C', here, 'describe', '--always', '--dirty']),
  agentBrowser: quiet(process.env.AGENT_BROWSER ?? 'agent-browser', ['--version'])?.replace(
    /^agent-browser\s+/,
    '',
  ),
  browserUse: quiet(python, [
    '-I',
    '-c',
    "import importlib.metadata as m; print(m.version('browser-use'))",
  ]),
  chrome: quiet(
    process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--version'],
  ),
}
/**
 * Every process with its parent, resident memory (KiB), CPU time (s) and command.
 * @returns {{ pid: number, ppid: number, rss: number, cpu: number, command: string }[]} The table.
 */
const processes = () =>
  (quiet('ps', ['-A', '-o', 'pid=,ppid=,rss=,time=,command=']) ?? '')
    .split('\n')
    .flatMap((line) => {
      const match =
        /^\s*(\d+)\s+(\d+)\s+(\d+)\s+(?:(\d+)-)?(?:(\d+):)?(\d+):([\d.]+)\s+(.*)$/u.exec(line)
      if (!match) return []
      const [, pid, ppid, rss, days, hours, minutes, seconds, command] = match
      const cpu =
        Number(days ?? 0) * 86400 +
        Number(hours ?? 0) * 3600 +
        Number(minutes) * 60 +
        Number(seconds)
      return [
        { pid: Number(pid), ppid: Number(ppid), rss: Number(rss), cpu, command: command ?? '' },
      ]
    })
/**
 * Runs one adapter while sampling every process it starts, including
 * agent-browser's daemon, which detaches from its caller. CPU time sums each
 * process's last sample; memory is the peak of the summed resident sizes.
 * @param {string} command The adapter program.
 * @param {string[]} args Its arguments.
 * @returns {Promise<{ stdout: string, stderr: string, cpuSeconds: number, peakMemoryMB: number }>} Its output and footprint.
 */
function profile(command, args) {
  const before = new Set(processes().map((row) => row.pid))
  const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] })
  let stdout = ''
  let stderr = ''
  child.stdout.on('data', (chunk) => (stdout += String(chunk)))
  child.stderr.on('data', (chunk) => (stderr += String(chunk)))
  /** @type {Map<number, number>} */
  const cpu = new Map()
  let peak = 0
  const sample = () => {
    const table = processes()
    const roots = new Set([child.pid ?? -1])
    for (const row of table)
      if (!before.has(row.pid) && row.ppid === 1 && row.command.includes('agent-browser'))
        roots.add(row.pid)
    /** @type {Map<number, number[]>} */
    const children = new Map()
    for (const row of table) children.set(row.ppid, [...(children.get(row.ppid) ?? []), row.pid])
    const owned = new Set()
    const stack = [...roots]
    while (stack.length) {
      const pid = stack.pop() ?? -1
      if (owned.has(pid)) continue
      owned.add(pid)
      stack.push(...(children.get(pid) ?? []))
    }
    let rss = 0
    for (const row of table)
      if (owned.has(row.pid)) {
        rss += row.rss
        cpu.set(row.pid, Math.max(cpu.get(row.pid) ?? 0, row.cpu))
      }
    peak = Math.max(peak, rss)
  }
  const timer = setInterval(sample, 500)
  const limit = setTimeout(() => child.kill('SIGKILL'), 900000)
  return new Promise((done) => {
    child.on('close', () => {
      clearInterval(timer)
      clearTimeout(limit)
      const cpuSeconds = [...cpu.values()].reduce((total, value) => total + value, 0)
      done({ stdout, stderr, cpuSeconds, peakMemoryMB: peak / 1024 })
    })
  })
}
/** @type {BenchRun[]} */
const runs = []
for (let round = 0; round < rounds; round++) {
  for (let offset = 0; offset < tools.length; offset++) {
    const tool = tools[(offset + round) % tools.length] ?? ''
    const adapter = adapters[tool]
    if (!adapter) continue
    if (freeGB() < 2) throw new Error('Less than 2 GB of free disk; stopping')
    const config = join(out, `config-${tool}.json`)
    writeFileSync(
      config,
      JSON.stringify({ sites, tasks, out: join(out, 'snapshots', `${tool}-r${String(round)}`) }),
    )
    const [command, args] = adapter
    const load = loadavg()[0] ?? 0
    const child = await profile(command, [...args, config])
    const line = child.stdout
      .trim()
      .split('\n')
      .reverse()
      .find((text) => text.startsWith('{"tool'))
    const run = line
      ? Run.parse(JSON.parse(line))
      : Run.parse({
          tool,
          error: `${child.stderr}${child.stdout}`.trim().split('\n').slice(-6).join(' | '),
        })
    run.round = round
    run.load1 = load
    run.cpuSeconds = child.cpuSeconds
    run.peakMemoryMB = child.peakMemoryMB
    runs.push(run)
    const passed = run.tasks.filter((task) => task.ok).length
    const status = run.error
      ? `ERROR ${run.error.slice(0, 200)}`
      : `launch ${String(Math.round(run.launchMs ?? 0))} ms, tasks ${String(passed)}/${String(tasks.length)}`
    console.log(`${tool.padEnd(14)} r${String(round)} ${status} (load ${load.toFixed(1)})`)
    writeFileSync(
      join(out, 'results.json'),
      JSON.stringify({ environment, sites, tasks, rounds, runs }, null, 2),
    )
  }
}
spawnSync('node', [join(here, 'report.mjs'), join(out, 'results.json')], { stdio: 'inherit' })
