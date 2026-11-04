// SPDX-License-Identifier: MIT
// One cold agent-browser session: node benchmarks/adapters/agent-browser.mjs <config.json>
// Uses the agent-browser CLI on PATH, or AGENT_BROWSER. `snapshot -i` is the
// mode its documentation recommends to agents; the full tree is measured too.
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { rmSync, statSync, writeFileSync } from 'node:fs'
import process from 'node:process'

import { ACTION_MS, message, now, play, readConfig, refs, sleep, thrice } from './common.mjs'

/** @typedef {import('../schema.mjs').BenchRun} BenchRun */
/** @typedef {import('../schema.mjs').BenchSite} BenchSite */

const config = readConfig()
const binary = process.env.AGENT_BROWSER ?? 'agent-browser'
const session = `suitecut-bench-${process.pid}`

/**
 * Runs one agent-browser command in this session.
 * @param {...string} args The command and its arguments.
 * @returns {string} What it printed.
 */
function run(...args) {
  const child = spawnSync(binary, ['--session', session, ...args], {
    encoding: 'utf8',
    env: { ...process.env, AGENT_BROWSER_DEFAULT_TIMEOUT: String(ACTION_MS) },
    timeout: 90000,
    maxBuffer: 1 << 28,
  })
  if (child.status !== 0)
    throw new Error(
      `agent-browser ${args.join(' ')}: ${String(child.status)} ${child.stderr.slice(0, 300)}`,
    )
  return child.stdout
}
const version = spawnSync(binary, ['--version'], { encoding: 'utf8' })
  .stdout.trim()
  .replace(/^agent-browser\s+/, '')
/** @type {BenchRun} */
const result = { tool: 'agent-browser', version, sites: [], tasks: [] }
let start = now()
try {
  run('open', 'about:blank')
  result.launchMs = now() - start
  run('set', 'viewport', '1280', '720')
  for (const url of config.sites) {
    /** @type {BenchSite} */
    const site = { url, host: new URL(url).hostname }
    try {
      start = now()
      run('open', url)
      site.loadMs = now() - start
      await sleep(1500)
      const snapshot = await thrice(() => Promise.resolve(run('snapshot', '-i')))
      site.snapshot = { ms: snapshot.ms, all: snapshot.all, chars: snapshot.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshot.txt`, snapshot.last)
      const whole = await thrice(() => Promise.resolve(run('snapshot')))
      site.snapshotFull = { ms: whole.ms, all: whole.all, chars: whole.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshotFull.txt`, whole.last)
      const file = `${config.out}-${site.host}.png`
      const shot = await thrice(() => {
        run('screenshot', file)
        const bytes = statSync(file).size
        rmSync(file)
        return Promise.resolve(bytes)
      })
      site.screenshot = { ms: shot.ms, all: shot.all, bytes: shot.last }
    } catch (error) {
      site.error = message(error)
    }
    result.sites.push(site)
  }
  for (const task of config.tasks)
    result.tasks.push(
      await play(task, {
        goto: (url) => Promise.resolve(void run('open', url)),
        snapshot: () => Promise.resolve(run('snapshot', '-i')),
        candidates: refs,
        click: (ref) => Promise.resolve(void run('click', `@${ref}`)),
        fill: (ref, text) => Promise.resolve(void run('fill', `@${ref}`, text)),
        press: (key) => Promise.resolve(void run('press', key)),
        url: () => Promise.resolve(run('get', 'url').trim()),
      }),
    )
} finally {
  start = now()
  spawnSync(binary, ['--session', session, 'close'], { encoding: 'utf8', timeout: 30000 })
  result.closeMs = now() - start
}
console.log(JSON.stringify(result))
