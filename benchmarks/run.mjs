// SPDX-License-Identifier: MIT
// Runs the whole benchmark:
//   node benchmarks/run.mjs [--rounds 3] [--tools suitecut,agent-browser,browser-use]
// Each round starts every tool cold (new browser, new profile) and rotates the
// order so no tool always runs first. Results land in benchmarks/results/.
import { execFileSync, spawnSync } from 'node:child_process'
import console from 'node:console'
import { mkdirSync, readFileSync, statfsSync, writeFileSync } from 'node:fs'
import { cpus, loadavg, release, totalmem } from 'node:os'
import { dirname, join } from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'
import { parseArgs } from 'node:util'

import * as z from 'zod'

import { Run } from './schema.mjs'
import { sites, tasks } from './tasks.mjs'

/** @typedef {import('./schema.mjs').BenchRun} BenchRun */

const here = dirname(fileURLToPath(import.meta.url))
const { values } = parseArgs({
  options: {
    rounds: { type: 'string', default: '3' },
    tools: { type: 'string', default: 'suitecut,agent-browser,browser-use' },
    out: { type: 'string', default: join(here, 'results') },
  },
})
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
    .parse(JSON.parse(readFileSync(join(here, '../package.json'), 'utf8'))).version,
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
    const child = spawnSync(command, [...args, config], {
      encoding: 'utf8',
      timeout: 900000,
      maxBuffer: 1 << 26,
    })
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
