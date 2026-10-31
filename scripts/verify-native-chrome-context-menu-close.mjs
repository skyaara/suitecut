// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdtempSync } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchBrowser } from '../dist/browser.js'

// In Chrome window mode a right-click shows Chromium's real context menu.
// Closing the page while it was open left the menu behind, and its dismissal
// later crashed the browser process inside CEF. macOS only: it needs windows.
const ROUNDS = 3
const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const dir = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-chrome-context-menu-close',
)
await mkdir(dir, { recursive: true })
const html =
  'data:text/html,<body style="height:600px"><select style="appearance:base-select"><option>a<option>b</select></body>'
/** @type {{result:string,closedWithMenuOpen:number,laterPagesAnswered:number,processExit?:unknown,error?:string}} */
const report = { result: 'running', closedWithMenuOpen: 0, laterPagesAnswered: 0 }
let stderr = ''
const browser = await launchBrowser({
  executablePath,
  userDataDir: mkdtempSync(join(tmpdir(), 'suitecut-context-menu-')),
  windowMode: 'chrome',
  onDiagnostic: (m) => (stderr += m),
  width: 1000,
  height: 700,
})
try {
  const context = browser.defaultContext()
  for (let i = 0; i < ROUNDS; i++) {
    const page = await context.newPage()
    await page.goto(html)
    await page.mouse.move(20, 15)
    await page.mouse.down({ button: 'right' })
    await page.mouse.up({ button: 'right' })
    await delay(500)
    await page.close()
    report.closedWithMenuOpen++
    const next = await context.newPage()
    await next.goto(html)
    await next.mouse.move(20, 15)
    await next.mouse.down()
    await next.mouse.up()
    // The crash surfaced about a second after the close.
    await delay(1500)
    assert.equal(await next.evaluate('1+1'), 2)
    report.laterPagesAnswered++
    await next.close()
  }
  report.result = 'pass'
} catch (error) {
  report.error = String(error)
  throw error
} finally {
  report.processExit = browser.source.processExit?.() ?? null
  await writeFile(dir + '/report.json', JSON.stringify(report, null, 2))
  try {
    await browser.close()
  } finally {
    await writeFile(dir + '/native-stderr.log', stderr)
  }
}
assert.equal(report.laterPagesAnswered, ROUNDS)
console.log(JSON.stringify(report))
