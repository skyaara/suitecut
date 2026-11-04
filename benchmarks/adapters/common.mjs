// SPDX-License-Identifier: MIT
// Helpers the JavaScript adapters share. The Python adapter mirrors them.
import { readFileSync } from 'node:fs'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import { Config } from '../schema.mjs'

/** @typedef {import('../schema.mjs').BenchTask} BenchTask */
/** @typedef {import('../schema.mjs').BenchTaskRun} BenchTaskRun */
/** @typedef {{ role: string, name: string, ref: string }} Candidate */

/**
 * The milliseconds since the process started.
 * @returns {number} High-resolution time.
 */
/** How long any tool may take on one click, fill or key press before the step retries. */
export const ACTION_MS = 5000
/**
 * How long a step waits before its retry, as an agent that saw a page still
 * loading would wait before looking again.
 */
export const RETRY_MS = 500
export const now = () => performance.now()

/**
 * The configuration file the orchestrator passes as the first argument.
 * @returns {import('zod').infer<typeof Config>} Sites, tasks and output prefix.
 */
export const readConfig = () =>
  Config.parse(JSON.parse(readFileSync(process.argv[2] ?? '', 'utf8')))

/**
 * The middle value, as each measurement is summarized.
 * @param {number[]} values Samples.
 * @returns {number} Their median.
 */
export const median = (values) => {
  const sorted = [...values].sort((a, b) => a - b)
  return sorted[Math.floor(sorted.length / 2)] ?? Number.NaN
}

/**
 * Waits, as an agent lets a page settle before reading it.
 * @param {number} ms How long.
 * @returns {Promise<void>} After that long.
 */
export const sleep = (ms) => delay(ms).then(() => undefined)

/**
 * Runs `call` three times and keeps the median, as each tool is measured.
 * @template T
 * @param {() => Promise<T>} call One measured call.
 * @returns {Promise<{ ms: number, all: number[], last: T }>} The timings and the last result.
 */
export async function thrice(call) {
  const all = []
  /** @type {T | undefined} */
  let last
  for (let index = 0; index < 3; index++) {
    const start = now()
    last = await call()
    all.push(now() - start)
  }
  return { ms: median(all), all, last: /** @type {T} */ (last) }
}

/**
 * Lines of the form `- role "name" ... Ref=X`, as suitecut and agent-browser
 * print them.
 * @param {string} snapshot The snapshot text.
 * @returns {Candidate[]} Every element with a ref.
 */
export function refs(snapshot) {
  const found = []
  for (const match of snapshot.matchAll(
    /^\s*- ([\w-]+)(?: "((?:[^"\\]|\\.)*)")?[^\n]*?\bref=([A-Za-z0-9]+)/gmu,
  )) {
    let name = match[2] ?? ''
    try {
      name = String(JSON.parse(`"${name}"`))
    } catch {
      // Keep the raw text when the name is not a JSON string.
    }
    found.push({ role: match[1] ?? '', name, ref: match[3] ?? '' })
  }
  return found
}

/**
 * The first element whose role and name match the step's target.
 * @param {{ role: string, name: string }} target The step's role and name pattern.
 * @param {Candidate[]} candidates Elements from the snapshot.
 * @returns {Candidate | undefined} The match.
 */
export function pick(target, candidates) {
  const role = new RegExp(`^(?:${target.role})$`, 'u')
  const name = new RegExp(target.name, 'iu')
  return candidates.find((item) => role.test(item.role) && name.test(item.name.trim()))
}

/**
 * Polls the page URL until it matches, as an agent checks its goal.
 * @param {() => Promise<string>} url Reads the current URL.
 * @param {string} pattern The goal.
 * @param {number} [timeout] How long to wait.
 * @returns {Promise<string>} The matching URL.
 */
export async function waitForURL(url, pattern, timeout = 20000) {
  const expected = new RegExp(pattern, 'u')
  const deadline = now() + timeout
  let current = ''
  while (now() < deadline) {
    current = await url()
    if (expected.test(current)) return current
    await sleep(100)
  }
  throw new Error(`URL did not reach ${pattern}; last ${current}`)
}

/**
 * Plays one task: before each step the agent reads a snapshot, then acts on
 * the element it names. Every snapshot read is counted.
 * @param {BenchTask} task What to do and how to tell it is done.
 * @param {{
 *   goto: (url: string) => Promise<void>,
 *   snapshot: () => Promise<string>,
 *   candidates: (snapshot: string) => Candidate[],
 *   click: (ref: string) => Promise<void>,
 *   fill: (ref: string, text: string) => Promise<void>,
 *   press: (key: string) => Promise<void>,
 *   url: () => Promise<string>,
 * }} tool The tool's actions.
 * @returns {Promise<BenchTaskRun>} Whether it reached the goal, how fast and how many tokens.
 */
export async function play(task, tool) {
  const start = now()
  /** @type {BenchTaskRun} */
  const result = {
    name: task.name,
    ok: false,
    ms: 0,
    snapshots: 0,
    chars: 0,
    snapshotMs: 0,
    retries: 0,
    retryErrors: [],
  }
  try {
    await tool.goto(task.url)
    for (const step of task.steps) {
      if ('press' in step) {
        await tool.press(step.press)
        if (step.until) await waitForURL(tool.url, step.until)
        continue
      }
      const target = 'click' in step ? step.click : step.fill
      // An agent whose action fails takes a new snapshot and tries once more,
      // so every tool gets one retry per step, on a fresh ref.
      for (let attempt = 0; ; attempt++) {
        const read = now()
        const text = await tool.snapshot()
        result.snapshotMs += now() - read
        result.snapshots++
        result.chars += text.length
        const found = pick(target, tool.candidates(text))
        try {
          if (!found) throw new Error(`No ${target.role} named /${target.name}/ in the snapshot`)
          if ('click' in step) await tool.click(found.ref)
          else await tool.fill(found.ref, step.text)
          if ('until' in step && step.until) await waitForURL(tool.url, step.until)
          break
        } catch (error) {
          if (attempt > 0) throw error
          result.retries++
          result.retryErrors.push(message(error).split('\n')[0] ?? '')
          await delay(RETRY_MS)
        }
      }
    }
    await waitForURL(tool.url, task.done)
    result.ok = true
  } catch (error) {
    result.error = error instanceof Error ? error.message : String(error)
  }
  result.ms = now() - start
  return result
}

/**
 * The message of a thrown value.
 * @param {unknown} error What was thrown.
 * @returns {string} Its message.
 */
export const message = (error) => (error instanceof Error ? error.message : String(error))
