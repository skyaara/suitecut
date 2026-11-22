// SPDX-License-Identifier: MIT
// One cold suitecut session: node benchmarks/adapters/suitecut.mjs <config.json>
// Drives the native control CLI, suitecut-native-control, in its persistent
// JSON-lines mode: one control process owns one engine for the whole run, as
// an agent holds one MCP connection, and every call is one request line and
// one reply line. SUITECUT_CONTROL names the CLI and SUITECUT_BROWSER the
// engine executable; both default to the paths benchmarks/README.md builds.
import { Buffer } from 'node:buffer'
import { execFileSync, spawn } from 'node:child_process'
import console from 'node:console'
import { rmSync, writeFileSync } from 'node:fs'
import { basename, dirname, resolve } from 'node:path'
import process from 'node:process'
import { createInterface } from 'node:readline'
import { clearTimeout, setTimeout } from 'node:timers'
import { fileURLToPath } from 'node:url'

import * as z from 'zod'

import { ACTION_MS, message, now, play, readConfig, sleep, thrice } from './common.mjs'

/** @typedef {import('../schema.mjs').BenchRun} BenchRun */
/** @typedef {import('../schema.mjs').BenchSite} BenchSite */
/** @typedef {import('./common.mjs').Candidate} Candidate */

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const config = readConfig()
const control = resolve(
  process.env.SUITECUT_CONTROL ??
    resolve(root, '.suitecut/native-client-bench/suitecut-native-control'),
)
const engine = resolve(
  process.env.SUITECUT_BROWSER ??
    resolve(root, '.suitecut/bench-engine/suitecut-browser.app/Contents/MacOS/suitecut-browser'),
)
/** The longest deadline the native tools accept (`timeoutMs` is capped at 30 s). */
const LONG_MS = 30000

// The engine starts headless at 1280×720 and a device scale factor of 1, in a
// new temporary profile it removes on close; `--tools core` is the surface the
// CLI and MCP give agents by default.
const child = spawn(control, ['--backend', 'cef', '--browser', engine, '--tools', 'core'], {
  stdio: ['pipe', 'pipe', 'pipe'],
})
let stderr = ''
child.stderr.on('data', (chunk) => (stderr = `${stderr}${String(chunk)}`.slice(-4000)))
/** @type {((line: string | Error) => void)[]} */
const waiting = []
const exited = new Promise((done) => child.on('close', done))
// Each request gets exactly one reply line; anything else on stdout is not a reply.
createInterface({ input: child.stdout, crlfDelay: Infinity }).on('line', (line) => {
  if (line.startsWith('{"ok"')) waiting.shift()?.(line)
})
/** Set once the CLI has exited, so later calls fail at once instead of waiting. */
let gone = ''
void exited.then((code) => {
  gone = `suitecut-native-control exited (${String(code)}): ${stderr.trim().slice(-300)}`
  for (const reply of waiting.splice(0)) reply(new Error(gone))
})
child.stdin.on('error', () => undefined)
/** The engine's temporary profile, when the CLI is killed before it can remove it. */
let leaked = ''
/**
 * Kills a CLI that stopped answering. Its engine then exits on the closed
 * pipe, but the profile the CLI owns stays, so the adapter removes it.
 */
function stop() {
  const table = execFileSync('ps', ['-A', '-o', 'ppid=,command='], { encoding: 'utf8' })
  for (const row of table.split('\n')) {
    const match = /^\s*(\d+)\s.*--suitecut-profile=(\S+)/u.exec(row)
    if (match?.[1] === String(child.pid) && basename(match[2] ?? '').startsWith('suitecut-client-'))
      leaked = match[2] ?? ''
  }
  child.kill('SIGKILL')
}
child.on('error', (error) => {
  for (const reply of waiting.splice(0)) reply(error)
})

const Failure = z.object({ message: z.string() }).partial()
const Reply = z.object({
  ok: z.boolean(),
  error: Failure.optional(),
  result: z.unknown(),
})
const Outcome = z.object({ ok: z.boolean().optional(), error: Failure.optional() })
const Observation = z.object({
  controls: z
    .array(z.object({ role: z.string().optional(), name: z.string().optional(), ref: z.string() }))
    .default([]),
})

/**
 * Sends one request and reads its reply line.
 * @param {string} tool The native tool.
 * @param {Record<string, unknown>} args Its arguments.
 * @returns {Promise<{ line: string, result: unknown }>} The reply as printed and its result.
 */
async function call(tool, args = {}) {
  if (gone) throw new Error(gone)
  /** @type {string} */
  const line = await new Promise((done, fail) => {
    const limit = setTimeout(() => {
      stop()
      fail(new Error(`suitecut ${tool} did not answer in 90 s`))
    }, 90000)
    waiting.push((reply) => {
      clearTimeout(limit)
      if (reply instanceof Error) fail(reply)
      else done(reply)
    })
    child.stdin.write(`${JSON.stringify({ tool, arguments: args })}\n`)
  })
  const reply = Reply.parse(JSON.parse(line))
  if (!reply.ok) throw new Error(`suitecut ${tool}: ${String(reply.error?.message)}`)
  const outcome = Outcome.safeParse(reply.result).data
  if (outcome?.ok === false)
    throw new Error(`suitecut ${tool}: ${outcome.error?.message ?? 'failed'}`)
  return { line, result: reply.result }
}

/**
 * Every control in an observation, with the short ref an action takes.
 * @param {string} snapshot The reply line of a snapshot.
 * @returns {Candidate[]} Its controls.
 */
const controls = (snapshot) =>
  Observation.parse(Reply.parse(JSON.parse(snapshot)).result).controls.map((item) => ({
    role: item.role ?? '',
    name: item.name ?? '',
    ref: item.ref,
  }))
// What an agent reads: the compact observation, only the controls it can act
// on with their refs plus the page's text context, as the CLI prints it.
const compact = async () => (await call('observe', { timeoutMs: LONG_MS })).line
// The whole page tree with its controls.
const full = async () => (await call('observe', { format: 'full', timeoutMs: LONG_MS })).line
/**
 * Navigates and waits for the `load` event.
 * @param {string} url Where.
 * @returns {Promise<void>} Once loaded.
 */
const goto = async (url) =>
  void (await call('act', { action: 'navigate', url, response: 'none', timeoutMs: LONG_MS }))
/**
 * One input action, without the observation `act` would otherwise return: the
 * harness takes its own snapshot before every step, as for the other tools.
 * @param {Record<string, unknown>} args The action.
 * @returns {Promise<void>} Once the engine acknowledged it.
 */
const act = async (args) =>
  void (await call('act', { ...args, response: 'none', timeoutMs: ACTION_MS }))

/**
 * The page URL, as the harness checks a task's goal. This reads the URL an
 * observation reports, scoped to `<head>` so it costs a few milliseconds and
 * keeps a deadline. `get url` would be cheaper, but it evaluates
 * `location.href` with no deadline, and after a key press that navigates it
 * can wait for an answer that never comes, which stops the whole session. An
 * observation that fails because the navigation is replacing the document
 * reads as "not there yet", and the harness polls again within the same 20 s
 * goal deadline.
 * @returns {Promise<string>} The URL, or '' mid-navigation.
 */
async function readURL() {
  try {
    const { result: seen } = await call('observe', { scope: 'head', timeoutMs: ACTION_MS })
    return z.object({ url: z.string() }).parse(seen).url
  } catch (error) {
    if (/execution context|Cannot find context/u.test(message(error))) return ''
    throw error
  }
}

/** @type {BenchRun} */
const result = { tool: 'suitecut', sites: [], tasks: [] }
let start = now()
try {
  // The first reply comes once the engine has launched and its page answers.
  await call('get', { kind: 'url' })
  result.launchMs = now() - start
  for (const url of config.sites) {
    /** @type {BenchSite} */
    const site = { url, host: new URL(url).hostname }
    try {
      start = now()
      await goto(url)
      site.loadMs = now() - start
      await sleep(1500)
      const snapshot = await thrice(compact)
      site.snapshot = { ms: snapshot.ms, all: snapshot.all, chars: snapshot.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshot.txt`, snapshot.last)
      const whole = await thrice(full)
      site.snapshotFull = { ms: whole.ms, all: whole.all, chars: whole.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshotFull.txt`, whole.last)
      const shot = await thrice(async () => {
        const { result: image } = await call('observe', { mode: 'screenshot', timeoutMs: LONG_MS })
        return Buffer.from(z.object({ base64: z.string() }).parse(image).base64, 'base64').length
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
        goto,
        snapshot: compact,
        candidates: controls,
        click: (ref) => act({ action: 'click', ref }),
        fill: (ref, text) => act({ action: 'fill', ref, value: text }),
        press: (key) => act({ action: 'press', key }),
        url: readURL,
      }),
    )
} catch (error) {
  result.error = message(error)
} finally {
  // End of input closes the engine and its profile, then the CLI exits.
  start = now()
  child.stdin.end()
  const forced = setTimeout(() => stop(), 30000)
  await exited
  clearTimeout(forced)
  result.closeMs = now() - start
  if (leaked) {
    await sleep(2000)
    rmSync(leaked, { recursive: true, force: true })
  }
}
console.log(JSON.stringify(result))
