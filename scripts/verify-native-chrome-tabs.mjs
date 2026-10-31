// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdtempSync } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchBrowser } from '../dist/browser.js'

// In Chrome window mode a context's later pages open as tabs beside its newest
// page, like Chrome; `newWindow` and `newPages: 'windows'` give a window
// instead. Tabs share their window's screen position while each new window is
// cascaded, so position tells the two apart. Closing a tab used to leave a
// phantom page behind that blocked browser shutdown. macOS only.
const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const dir = resolve(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-chrome-tabs')
await mkdir(dir, { recursive: true })
/** @type {Record<string, unknown>} */
const report = { result: 'running' }
let stderr = ''

const State = z.object({ url: z.string(), history: z.number(), window: z.string() })
/**
 * The page's URL, history length and window position.
 * @param {{ evaluate(expression: string): Promise<unknown> }} page
 */
const state = async (page) =>
  State.parse(
    await page.evaluate(
      '({ url: location.href, history: history.length, window: screenX + "," + screenY })',
    ),
  )

/**
 * Opens pages under one launch default and checks which became tabs.
 * @param {'tabs' | 'windows'} newPages
 */
async function run(newPages) {
  const browser = await launchBrowser({
    executablePath,
    userDataDir: mkdtempSync(join(tmpdir(), 'suitecut-chrome-tabs-')),
    windowMode: 'chrome',
    newPages,
    onDiagnostic: (m) => (stderr += m),
    width: 1000,
    height: 700,
  })
  try {
    const context = browser.defaultContext()
    const first = await state(context.pages()[0])
    const page = await context.newPage()
    const opened = await state(page)
    // A new page starts on about:blank, never Chrome's New Tab page.
    assert.deepEqual([opened.url, opened.history, page.url()], ['about:blank', 1, 'about:blank'])
    await page.setContent('<input id=field>')
    await page.click('#field')
    await page.keyboard.type('typed')
    assert.equal(await page.evaluate('field.value'), 'typed')
    const otherPage = await context.newPage({ newWindow: newPages === 'tabs' })
    const other = await state(otherPage)
    if (newPages === 'tabs') {
      assert.equal(opened.window, first.window, 'the default page should be a tab')
      assert.notEqual(other.window, opened.window, 'newWindow should open a window')
    } else {
      assert.notEqual(opened.window, first.window, 'windows should be the launch default')
      assert.equal(other.window, opened.window, 'newWindow: false should open a tab')
    }
    await otherPage.close()
    await page.close()
    assert.equal(context.pages().length, 1)
    return { first, opened, other }
  } finally {
    const started = Date.now()
    await browser.close()
    report[newPages + 'CloseMs'] = Date.now() - started
  }
}

try {
  report.tabs = await run('tabs')
  report.windows = await run('windows')
  report.result = 'pass'
} catch (error) {
  report.error = String(error)
  throw error
} finally {
  await writeFile(dir + '/report.json', JSON.stringify(report, null, 2))
  await writeFile(dir + '/native-stderr.log', stderr)
}
console.log(JSON.stringify(report))
