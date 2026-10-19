// SPDX-License-Identifier: MIT
/* global document, window, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeBindings,
  NativeSelectors,
} from '../dist/native.js'

const directory = resolve('.suitecut/native-inbound-utf16')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const checks = []
try {
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const page = await context.newPage()
  const value = '\ud800a\0\\"\udfff😀'
  await page.evaluate((text) => {
    const button = document.createElement('button')
    button.textContent = text
    button.setAttribute('aria-label', text)
    button.setAttribute('data-testid', text)
    button.setAttribute('data-unit', text)
    button.id = text
    button.onclick = () => {
      window.clicked = true
    }
    document.body.append(button)
  }, value)
  assert.equal(await page.getByText(value, { exact: true }).count(), 1)
  assert.equal(await page.getByTestId(value).count(), 1)
  assert.equal(await page.getByRole('button', { name: value, exact: true }).count(), 1)
  assert.equal(
    await page.getByText(new RegExp(value.replace(/[.*+?^${}()|[\]\\]/gu, '\\$&'), 'u')).count(),
    1,
  )
  assert.equal(
    await page
      .locator('text="' + value.replace(/\\/gu, '\\\\').replace(/"/gu, '\\"') + '"')
      .count(),
    1,
  )
  checks.push(
    'exact selected text/testId/role/regexp and legacy selector strings retain isolated/pair/NUL/backslash/quote units',
  )
  assert.equal(
    await page
      .locator('button')
      .filter({ hasText: value })
      .and(page.getByRole('button', { name: value, exact: true }))
      .count(),
    1,
  )
  assert.equal(
    await page
      .locator('body')
      .filter({ has: page.getByText(value, { exact: true }) })
      .count(),
    1,
  )
  checks.push('native composed filters and role-name patterns restore only selected query fields')
  const selectors = new NativeSelectors(source)
  const factory = `(()=>({
    query(root, selector) { return this.queryAll(root, selector)[0] ?? null },
    queryAll(root, selector) { return [...root.querySelectorAll('[data-unit]')].filter(node => node.getAttribute('data-unit') === selector) },
  }))`
  await selectors.register('exactMainUnits', factory)
  await selectors.register('exactUtilityUnits', factory, { contentScript: true })
  const main = page.locator(`exactMainUnits=${value}`)
  const utility = page.locator(`exactUtilityUnits=${value}`)
  assert.equal(await main.and(utility).count(), 1)
  const retained = await main.and(utility).elementHandle()
  assert(retained)
  try {
    assert.equal(
      await retained.evaluate(
        (node) =>
          node instanceof Element && node.getAttribute('data-unit') === '\ud800a\0\\"\udfff😀',
      ),
      true,
    )
  } finally {
    await retained.dispose()
  }
  const elementImage = await main.screenshot()
  assert.equal(elementImage.subarray(0, 8).toString('hex'), '89504e470d0a1a0a')
  const maskImage = await page.screenshot({ mask: [main.and(utility)] })
  assert.equal(maskImage.subarray(0, 8).toString('hex'), '89504e470d0a1a0a')
  await page.evaluate((text) => {
    const input = document.createElement('input')
    input.type = 'checkbox'
    input.id = text + 'checked'
    input.setAttribute('data-testid', text + 'checkbox')
    document.body.append(input)
  }, value)
  const checkbox = page.getByTestId(value + 'checkbox')
  await checkbox.check()
  assert.equal(await checkbox.isChecked(), true)
  checks.push(
    'actual main/utility selector engines, node descriptions, scoped PNG/mask capture and form adoption preserve genuine nodes with isolated id/class metadata',
  )
  await page.getByRole('button', { name: value, exact: true }).click()
  assert.equal(await page.evaluate(() => Reflect.get(window, 'clicked') === true), true)
  checks.push('native click resolves the original selected element without replacement selectors')
  await page.evaluate((text) => {
    const frame = document.createElement('iframe')
    frame.setAttribute('data-testid', text + 'frame')
    frame.id = text + 'frame'
    frame.srcdoc = '<button>nested exact scope</button>'
    document.body.append(frame)
  }, value)
  const nested = page
    .getByTestId(value + 'frame')
    .contentFrame()
    .getByText('nested exact scope')
  await nested.waitFor({ timeout: 2000 })
  assert.equal(await nested.count(), 1)
  checks.push('actual child-frame scope resolution preserves encoded frame-path selector steps')
  const author = {
    __suitecutNativeUTF16Scalar: 'd800',
    nameJSON: '"author"',
    selectorJSON: '"author"',
    text: value,
  }
  const name = 'callback' + value
  const registration = await page.exposeFunction(name, (arg) => arg)
  assert.deepEqual(
    await page.evaluate(`globalThis[${JSON.stringify(name)}](${JSON.stringify(author)})`),
    author,
  )
  const release64 = new AbortController()
  const started64 = new AbortController()
  let physical64 = 0
  const started = new Promise((done) =>
    started64.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  const parallelName = name + 'parallel'
  const parallelRegistration = await page.exposeFunction(parallelName, async (arg) => {
    const released = new Promise((done) =>
      release64.signal.addEventListener('abort', () => done(undefined), { once: true }),
    )
    if (++physical64 === 64) started64.abort()
    await released
    return arg
  })
  const evaluations = Array.from({ length: 64 }, (_, index) =>
    page.evaluate(`globalThis[${JSON.stringify(parallelName)}](${index})`),
  )
  await started
  assert.equal(NativeBindings.hostDiagnostics().physical, 64)
  release64.abort()
  const parallel = await Promise.all(evaluations)
  await parallelRegistration.dispose()
  assert.deepEqual(
    parallel,
    Array.from({ length: 64 }, (_, index) => index),
  )
  await registration.dispose()
  assert.equal(await page.evaluate((key) => typeof globalThis[key], name), 'undefined')
  checks.push(
    'native page binding name round-trip, ordinary author tag objects, 64 concurrent resolutions and exact disposal',
  )
  const inheritedName = 'inherited' + value
  const inherited = await context.exposeFunction(inheritedName, (arg) => arg)
  assert.equal(await page.evaluate(`globalThis[${JSON.stringify(inheritedName)}](29)`), 29)
  const next = await context.newPage()
  const firstScript = `globalThis.firstUnitCall=globalThis[${JSON.stringify(inheritedName)}](31)`
  await next.goto(
    'data:text/html;charset=utf-8,' + encodeURIComponent('<script>' + firstScript + '</script>'),
  )
  assert.equal(await next.evaluate('globalThis.firstUnitCall'), 31)
  await next.setContent('<iframe srcdoc="<body>child</body>"></iframe>')
  await next.frameLocator('iframe').locator('body').waitFor()
  const child = next.frames().find((frame) => frame.parentFrame())
  assert(child)
  assert.equal(await child.evaluate(`globalThis[${JSON.stringify(inheritedName)}](37)`), 37)
  await inherited.dispose()
  assert.equal(await next.evaluate((key) => typeof globalThis[key], inheritedName), 'undefined')
  checks.push(
    'context inherited name preserves exact units in existing/future pages and child renderer bootstrap/disposal',
  )
  const canceledName = 'canceled' + value
  const releaseCanceled = new AbortController()
  const admitted = new AbortController()
  const admittedPromise = new Promise((done) =>
    admitted.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  const canceled = await page.exposeFunction(canceledName, async () => {
    const released = new Promise((done) =>
      releaseCanceled.signal.addEventListener('abort', () => done(undefined), { once: true }),
    )
    admitted.abort()
    await released
    return 43
  })
  const waiting = page.evaluate(`globalThis[${JSON.stringify(canceledName)}]()`)

  const rejection = assert.rejects(waiting)
  await admittedPromise
  await canceled.dispose()
  await rejection
  assert.equal(NativeBindings.hostDiagnostics().physical, 1)
  releaseCanceled.abort()
  for (let index = 0; index < 100 && NativeBindings.hostDiagnostics().physical; index++)
    await page.waitForTimeout(5)
  assert.equal(NativeBindings.hostDiagnostics().physical, 0)
  checks.push(
    'isolated-name disposal settles native promise while preserving physical host admission until the real callback settles',
  )
  await next.close()
  await page.waitForTimeout(20)
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      bindingCalls: z.number(),
      bindingArgumentControls: z.number(),
      bindingArgumentBytes: z.number(),
      bindingPhysicalInstallations: z.number(),
      bindingInstallerGroups: z.number(),
      bindingInstallerGroupReleases: z.number(),
    })
    .parse(await page.runAutomation('diagnostics'))
  for (const count of Object.values(state)) assert.equal(count, 0)
  await context.close()
  const report = {
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
