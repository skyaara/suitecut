// SPDX-License-Identifier: MIT
/* global window, HTMLElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts, expectNative } from '../dist/native.js'

const directory = '.suitecut/native-contenteditable-states'
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (line) => {
    stderr += line
    assert(stderr.length < 1048576)
  },
})
const checks = []
try {
  const context = await new NativeBrowserContexts(source).newContext(),
    page = await context.newPage()
  for (const html of [
    '<div contenteditable id="target">Actual editor</div>',
    '<button><div contenteditable id="target">Actual editor</div></button>',
    '<button><div contenteditable><span id="target">Actual editor</span></div></button>',
  ]) {
    await page.setContent(html)
    assert.equal(
      await page
        .locator('#target')
        .evaluate((node) => node instanceof HTMLElement && node.isContentEditable),
      true,
    )
    const fixed = await page.$('#target')
    assert(fixed)
    assert.equal(await fixed.isEditable(), true)
    assert.equal(await page.locator('#target').isEditable(), true)
    await expectNative(page.locator('#target')).toBeEditable()
    await fixed.waitForElementState('editable', { timeout: 1000 })
    assert.equal(await fixed.getAttribute('id'), 'target')
    await fixed.dispose()
  }
  checks.push(
    'actual direct and inherited selected contenteditable nodes keep their genuine editability inside native buttons',
  )
  await page.setContent(
    '<button id="button" disabled><span id="target" contenteditable="false">Actual noneditor</span></button><label id="label" for="control">Actual label</label><input id="control" readonly>',
  )
  assert.equal(
    await page
      .locator('#target')
      .evaluate((node) => node instanceof HTMLElement && node.isContentEditable),
    false,
  )
  assert.equal(await page.locator('#target').isEnabled(), false)
  await assert.rejects(page.locator('#target').isEditable(), /Element is not an <input>/u)
  assert.equal(await page.locator('#target').getAttribute('id'), 'target')
  assert.equal(await page.locator('#label').isEditable(), false)
  assert.equal(await page.locator('#label').getAttribute('id'), 'label')
  checks.push(
    'genuine contenteditable=false nodes still follow the enclosing button while ordinary labels retain their real native associated-control state',
  )
  await page.setContent('<button><div contenteditable id="target">Actual editor</div></button>')
  await page.locator('#target').fill('Actual filled')
  await page.locator('#target').selectText()
  assert.equal(await page.evaluate(() => window.getSelection()?.toString()), 'Actual filled')
  assert.equal(await page.locator('#target').textContent(), 'Actual filled')
  checks.push(
    'the original selected contenteditable node remains the actual fill and native selection target inside its button',
  )
  const stateSchema = z.object({
    otherOperations: z.number(),
    activeHandles: z.number(),
    pendingHandles: z.number(),
    evaluationGroups: z.number(),
    nativeDOMGetters: z.number(),
    nativeSelectorWaits: z.number(),
    nativeSelectorWaitPhysical: z.number(),
    nativeElementStates: z.number(),
    nativeElementStatePhysical: z.number(),
    nativeElementStateReleases: z.number(),
  })
  const cleanupBegan = performance.now(),
    samples = []
  let state = stateSchema.parse(await page.source.runAutomation('diagnostics'))
  samples.push({ elapsedMs: performance.now() - cleanupBegan, state })
  await writeFile(directory + '/cleanup-samples.json', JSON.stringify(samples, null, 2) + '\n')
  while (
    Object.values(state).some((value) => value !== 0) &&
    performance.now() - cleanupBegan < 1000
  ) {
    await delay(1)
    state = stateSchema.parse(await page.source.runAutomation('diagnostics'))
    samples.push({ elapsedMs: performance.now() - cleanupBegan, state })
  }
  await writeFile(directory + '/cleanup-samples.json', JSON.stringify(samples, null, 2) + '\n')
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await context.close()
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ checks, state, samples }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state, samples: samples.length }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '')
