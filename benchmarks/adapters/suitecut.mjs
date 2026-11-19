// SPDX-License-Identifier: MIT
// One cold suitecut session: node benchmarks/adapters/suitecut.mjs <config.json>
// Set SUITECUT_BROWSER to the built suitecut-browser executable.
import console from 'node:console'
import { writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import process from 'node:process'

import { launchBrowser } from '../../dist/browser.js'

import { ACTION_MS, message, now, play, readConfig, refs, sleep, thrice } from './common.mjs'

/** @typedef {import('../schema.mjs').BenchRun} BenchRun */
/** @typedef {import('../schema.mjs').BenchSite} BenchSite */

const config = readConfig()
const executablePath = resolve(
  process.env.SUITECUT_BROWSER ??
    '.suitecut/cef-codecs-2026-11-19/build/suitecut-browser.app/Contents/MacOS/suitecut-browser',
)
/** @type {BenchRun} */
const result = { tool: 'suitecut', sites: [], tasks: [] }
let start = now()
const browser = await launchBrowser({
  backend: 'cef',
  executablePath,
  headless: true,
  width: 1280,
  height: 720,
  deviceScaleFactor: 1,
})
const page = browser.defaultContext().pages()[0]
if (!page) throw new Error('suitecut opened no page')
await page.evaluate('document.readyState')
result.launchMs = now() - start
// What an agent reads: only the elements it can act on, with refs.
const compact = () => page.ariaSnapshot({ mode: 'ai', interactive: true, timeout: 60000 })
// The whole page, as Playwright's AI snapshot prints it.
const full = () => page.ariaSnapshot({ mode: 'ai', timeout: 60000 })
try {
  for (const url of config.sites) {
    /** @type {BenchSite} */
    const site = { url, host: new URL(url).hostname }
    try {
      start = now()
      await page.goto(url, { waitUntil: 'load', timeout: 45000 })
      site.loadMs = now() - start
      await sleep(1500)
      const snapshot = await thrice(compact)
      site.snapshot = { ms: snapshot.ms, all: snapshot.all, chars: snapshot.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshot.txt`, snapshot.last)
      const whole = await thrice(full)
      site.snapshotFull = { ms: whole.ms, all: whole.all, chars: whole.last.length }
      writeFileSync(`${config.out}-${site.host}.snapshotFull.txt`, whole.last)
      const shot = await thrice(async () => (await page.screenshot()).length)
      site.screenshot = { ms: shot.ms, all: shot.all, bytes: shot.last }
    } catch (error) {
      site.error = message(error)
    }
    result.sites.push(site)
  }
  for (const task of config.tasks)
    result.tasks.push(
      await play(task, {
        goto: async (url) => void (await page.goto(url, { waitUntil: 'load', timeout: 45000 })),
        snapshot: compact,
        candidates: refs,
        click: (ref) => page.getByRef(ref).click({ timeout: ACTION_MS }),
        fill: (ref, text) => page.getByRef(ref).fill(text, { timeout: ACTION_MS }),
        press: (key) => page.keyboard.press(key),
        url: () => Promise.resolve(page.url()),
      }),
    )
} finally {
  start = now()
  await browser.close()
  result.closeMs = now() - start
}
console.log(JSON.stringify(result))
