// SPDX-License-Identifier: MIT
/* global document, requestAnimationFrame */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, expectNative, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-assertions')
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
/**
 * Hashes the actual proof input.
 * @param {string} path
 */
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(path))
    .digest('hex')
const executableHash = await hash(executablePath)
const sdkHashes = z
  .record(z.string(), z.string())
  .parse(
    Object.fromEntries(
      await Promise.all(
        (await readdir(resolve('dist')))
          .filter((name) => name.endsWith('.js'))
          .map(async (name) => [name, await hash(resolve('dist', name))]),
      ),
    ),
  )
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
try {
  const page = createNativePage(source)
  await page.setContent(
    '<p>first</p><p>other</p><input value=" a "><div style="display:none">hidden</div>',
  )
  await expectNative(page.locator('p')).toHaveCount(2)
  await expectNative(page.locator('p').first()).toHaveText('first')
  await expectNative(page.locator('p').last()).toHaveText('other')
  await expectNative(page.locator('p')).toHaveText(['first', 'other'])
  await expectNative(page.locator('div')).toBeHidden()
  await expectNative(page.locator('input')).toHaveValue(' a ')
  await assert.rejects(
    expectNative(page.locator('input')).toHaveValue('a', { timeout: 50 }),
    /toHaveValue\(expected\) failed/,
  )
  await assert.rejects(
    expectNative(page.locator('p').first()).not.toHaveText('first', { timeout: 50 }),
    /not\.toHaveText\(expected\) failed/,
  )
  const changed = expectNative(page.locator('p').first()).toHaveText(/SECOND/i)
  await page.evaluate(
    () =>
      new Promise((resolve) =>
        requestAnimationFrame(() => {
          document.querySelector('p').textContent = 'second'
          resolve()
        }),
      ),
  )
  await changed
  await expectNative(page.locator('p').first()).not.toHaveText('first')
  await expectNative(page.locator('p').first()).not.toHaveText(/SECOND/i, { ignoreCase: false })
  await assert.rejects(
    expectNative(page.locator('.missing')).not.toHaveText('anything', { timeout: 50 }),
    /not\.toHaveText\(expected\) failed/,
  )
  await page.setContent(
    '<div class="one two"></div><div class="three"></div><select multiple><option selected value=" a ">A</option><option selected value="b">B</option></select><label for="value">Value</label><input id="value" value="linked"><div role="switch" aria-checked="true"></div>',
  )
  await expectNative(page.locator('div').first()).toContainClass('two one')
  await expectNative(page.locator('div').nth(1)).not.toContainClass('one')
  await expectNative(page.locator('div').first()).not.toHaveClass('two one')
  await expectNative(page.locator('div').first().locator(':scope')).toHaveClass('one two')
  await expectNative(page.locator('div:not([role])')).toHaveClass(['one two', /three/])
  await expectNative(page.locator('div:not([role])')).toContainClass(['two', 'three'])
  await expectNative(page.locator('select')).toHaveValues([' a ', /b/])
  await assert.rejects(
    expectNative(page.locator('select')).toHaveValues(['a', 'b'], { timeout: 50 }),
    /toHaveValues\(expected\) failed/,
  )
  await expectNative(page.locator('label')).toHaveValue('linked')
  await expectNative(page.locator('[role=switch]')).toBeChecked()
  await page.locator('input').evaluate((element) => {
    element.nested = { value: 7 }
  })
  await expectNative(page.locator('input')).toHaveJSProperty('nested.value', 7)
  await page.setContent('<div id="shadow"></div><p>second</p>')
  await page.evaluate(() => {
    document.querySelector('#shadow').attachShadow({ mode: 'open' }).innerHTML =
      '<span>inside</span>'
  })
  await expectNative(page.locator('#shadow')).toHaveText('inside')
  await expectNative(page.locator('#shadow')).not.toHaveText('inside', { useInnerText: true })
  await expectNative(page.locator('p')).toBeInViewport({ ratio: 1 })
  await page.evaluate(() => {
    document.title = '  Native    title  '
  })
  await expectNative(page).toHaveTitle('Native title')
  await expectNative(page).toHaveURL('about:blank')
  const pending = page.waitForFunction(() => document.querySelector('p').textContent === 'third')
  await page.evaluate(
    () =>
      new Promise((resolve) =>
        requestAnimationFrame(() => {
          document.querySelector('p').textContent = 'third'
          resolve()
        }),
      ),
  )
  const handle = await pending
  assert.equal(await handle.jsonValue(), true)
  await handle.dispose()
  const argumentHandle = await page.evaluateHandle(() => 'third')
  assert.equal(
    await page
      .locator('p')
      .first()
      .evaluate((element, arg) => element.textContent === arg, argumentHandle),
    true,
  )
  await argumentHandle.dispose()
  await page.setContent(
    '<button aria-label="Native button" aria-describedby="description">Visible</button><span id="description">A description</span><input aria-invalid="true" aria-errormessage="error"><span id="error">Real error</span>',
  )
  await expectNative(page.locator('button')).toHaveRole('button')
  await expectNative(page.locator('button')).toHaveAccessibleName('native BUTTON', {
    ignoreCase: true,
  })
  await expectNative(page.locator('button')).toHaveAccessibleDescription(/description/)
  await expectNative(page.locator('input')).toHaveAccessibleErrorMessage('Real error')
  await expectNative(page.locator('input')).not.toHaveAccessibleErrorMessage('Invented error')
  await assert.rejects(
    expectNative(page.locator('button')).toHaveAccessibleName('wrong', { timeout: 1 }),
    /Expected: "wrong"[\s\S]*Received: "Native button"/,
  )
  const state = z.record(z.string(), z.number()).parse(await source.runAutomation('diagnostics'))
  const released = [
    'otherOperations',
    'evaluationGroups',
    'activeHandles',
    'pendingHandles',
    'pendingNetworkCommands',
    'pendingUploadGroups',
    'pendingUploadBytes',
    'artifacts',
    'formClicks',
    'temporaryUploadBytes',
    'temporaryUploadInputs',
    'pendingDevTools',
    'pendingSessionTrees',
    'navigations',
    'bodyWaits',
    'activeRequests',
    'pendingDomWorlds',
    'inputWork',
    'queuedInput',
    'pressedKeys',
    'keyModifiers',
    'mouseButtons',
  ]
  for (const key of released) assert.equal(state[key], 0, `${key} retained after assertions`)
  assert.equal(
    await hash(executablePath),
    executableHash,
    'Executable changed during assertion proof',
  )
  for (const [name, digest] of Object.entries(sdkHashes))
    assert.equal(
      await hash(resolve('dist', name)),
      digest,
      `${name} changed during assertion proof`,
    )
  const report = {
    executablePath,
    executableHash,
    sdkHashes,
    cefVersion: source.cefVersion,
    locatorComposition: 'passed',
    exactValueWhitespace: 'passed',
    negativeMismatch: 'passed',
    regex: 'passed',
    regexFlagOverride: 'passed',
    missingNegation: 'passed',
    classTokens: 'passed',
    multiSelectValues: 'passed',
    labelValue: 'passed',
    ariaChecked: 'passed',
    nestedProperty: 'passed',
    shadowText: 'passed',
    viewportRatio: 'passed',
    pageTitle: 'passed',
    pageURL: 'passed',
    nativeRetry: 'passed',
    functionWait: 'passed',
    nestedHandleArgument: 'passed',
    sharedAccessibilityMetadata: 'passed',
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
