// SPDX-License-Identifier: MIT
/* global document, HTMLIFrameElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, expectNative, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const paths = [
  executable,
  resolve('dist/native-aria.js'),
  resolve('dist/native-expect.js'),
  resolve('dist/native-page.js'),
]
/**
 * Hashes the exact runtime or SDK file used by this proof.
 * @param {string} path
 */
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(path))
    .digest('hex')
const before = await Promise.all(paths.map(hash))
const directory = resolve('.suitecut/native-aria')
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 640,
  height: 480,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent(
    '<h1>Native title</h1><ul id=notifications><li>Database initialised.</li></ul><button aria-pressed=mixed>Toggle</button><a href="../raw#anchor">Native link</a>',
  )
  assert.equal(
    await page.locator('#notifications').ariaSnapshot(),
    '- list:\n  - listitem: Database initialised.',
  )
  await expectNative(page.locator('#notifications')).toMatchAriaSnapshot(
    '- list:\n  - listitem: Database initialised.',
  )
  await expectNative(page).toMatchAriaSnapshot(
    '- heading "Native title" [level=1]\n- button [pressed=mixed]',
  )
  await expectNative(page).toMatchAriaSnapshot('- link "Native link":\n  - /url: ../raw#anchor')
  await expectNative(page).not.toMatchAriaSnapshot('- heading "Wrong"')
  checks.push(
    'genuine AX scoped capture and renderer matching preserve native roles/names/states/raw href',
  )
  await expectNative(page).toMatchAriaSnapshot('- heading /Native .*/', { timeout: 1 })
  const mismatch = await expectNative(page)
    .toMatchAriaSnapshot('- heading "Wrong"', { timeout: 29 })
    .catch((error) => (error instanceof Error ? error : new Error(String(error))))
  assert(mismatch instanceof Error)
  assert.match(mismatch.message, /expect\(page\)\.toMatchAriaSnapshot\(expected\) failed/u)
  assert.match(mismatch.message, /Native title/u)
  checks.push('tiny native first observation and original-deadline failure retain actual snapshot')
  await page.setContent('<ul id=list><li>one</li><li>two</li><li>three</li></ul>')
  await expectNative(page).toMatchAriaSnapshot('- list:\n  - listitem: one\n  - listitem: three')
  await assert.rejects(
    expectNative(page).toMatchAriaSnapshot(
      '- list:\n  - /children: equal\n  - listitem: one\n  - listitem: three',
      { timeout: 29 },
    ),
    /toMatchAriaSnapshot/u,
  )
  checks.push('ordered subset and exact-child constraints execute on actual captured tree')
  await page.setContent('<div id=changing role=group>Waiting</div>')
  const changed = expectNative(page.locator('#changing')).toMatchAriaSnapshot('- group: Ready', {
    timeout: 1000,
  })
  await delay(40)
  await page.evaluate(() => {
    document.querySelector('#changing').textContent = 'Ready'
  })
  await changed
  checks.push(
    'native retries observe authentic DOM/accessibility changes without resetting timeout',
  )
  await page.setContent('<iframe id=child></iframe>')
  await page.evaluate(() => {
    const frame = document.querySelector('#child')
    if (!(frame instanceof HTMLIFrameElement) || !frame.contentDocument)
      throw new Error('Expected the actual child document')
    frame.contentDocument.body.innerHTML = '<h2>Child snapshot</h2>'
  })
  await expectNative(page.frameLocator('#child').locator('h2')).toMatchAriaSnapshot(
    '- heading "Child snapshot" [level=2]',
  )
  checks.push('native frame-path scope captures its actual child AX identity')
  await page.setContent('<div>one</div><div>two</div>')
  await assert.rejects(page.locator('div').ariaSnapshot(), /strict/u)
  await page.goto('data:text/html,<button>Fresh document</button>')
  await expectNative(page).toMatchAriaSnapshot('- button "Fresh document"')
  checks.push('strict scope errors and navigation recovery preserve original native identities')
  const state = z
    .object({
      pendingAXCaptures: z.number(),
      pendingAXPhysicalCommands: z.number(),
      pendingAXCaptureBytes: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      otherOperations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(state)) assert.equal(value, 0)
  const after = await Promise.all(paths.map(hash))
  assert.deepEqual(after, before, 'Native runtime or SDK changed during the actual ARIA proof')
  const report = {
    cefVersion: source.cefVersion,
    paths,
    hashes: before,
    unchanged: true,
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
