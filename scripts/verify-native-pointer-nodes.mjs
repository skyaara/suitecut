// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_POINTER_NODES_DIRECTORY ?? '.suitecut/native-pointer-nodes-proof',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory),
  errors = z.array(z.unknown()).parse([]),
  checks = []
const source = await launchNativeBrowser({
  executablePath: z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
const click = z.object({ trusted: z.boolean(), target: z.string() })
try {
  await page.setContent(
    '<!DOCTYPE html><style>body{margin:0}</style><div id=outer><div id=inner style="width:50px;height:1800px">Tall nested content</div>Direct parent text.</div>',
  )
  await page.evaluate(() => {
    globalThis.document.addEventListener('click', (event) => {
      globalThis.actualTextClick = {
        trusted: event.isTrusted,
        target: event.target instanceof globalThis.Element ? event.target.id : null,
      }
    })
  })
  const text = await page.evaluateHandle(
    () => globalThis.document.querySelector('#outer')?.lastChild,
  )
  const actualText = text.asElement()
  assert(actualText)
  try {
    await actualText.click({ timeout: 5000 })
    assert.deepEqual(click.parse(await page.evaluate('globalThis.actualTextClick')), {
      trusted: true,
      target: 'outer',
    })
    assert(z.number().parse(await page.evaluate('scrollY')) > 0)
    checks.push('actual-text-Range-offscreen-native-scroll-trusted-click-selected-parent')
    await page.mouse.move(0, 0)
    await actualText.hover({ timeout: 1000 })
    assert.equal(await page.evaluate('document.querySelector("#outer").matches(":hover")'), true)
    checks.push('actual-text-Range-hover')
    await page.evaluate(() => {
      Reflect.deleteProperty(globalThis, 'Node')
    })
    await actualText.click({ timeout: 1000 })
    assert.deepEqual(click.parse(await page.evaluate('globalThis.actualTextClick')), {
      trusted: true,
      target: 'outer',
    })
    checks.push('authored-Node-global-deletion-does-not-alter-native-utility-readiness')
    await actualText.evaluate((node) => {
      if (!(node instanceof globalThis.Text)) throw new Error('Actual fixture is not Text')
      node.remove()
    })
    await assert.rejects(actualText.click({ timeout: 1000 }), /Element is not attached to the DOM/u)
    checks.push('actual-detached-Text-identity-rejects')
  } finally {
    await text.dispose()
  }

  await page.setContent(
    '<!DOCTYPE html><button id=disabled disabled>Disabled text</button><br id=br><span id=empty></span><span id=hidden style="display:none">Hidden</span><span id=invisible style="visibility:hidden">Invisible</span>',
  )
  const disabled = await page.evaluateHandle(
    () => globalThis.document.querySelector('#disabled')?.firstChild,
  )
  const disabledText = disabled.asElement()
  assert(disabledText)
  try {
    await disabledText.hover({ timeout: 1000 })
    await assert.rejects(disabledText.click({ timeout: 150 }), /Timeout/u)
    checks.push('Text-parent-disabled-allows-hover-and-blocks-click')
  } finally {
    await disabled.dispose()
  }
  for (const selector of ['#br', '#empty'])
    await assert.rejects(
      page.locator(selector).click({ force: true, timeout: 1000 }),
      /Element is outside of the viewport/u,
    )
  for (const selector of ['#hidden', '#invisible'])
    await assert.rejects(
      page.locator(selector).click({ force: true, timeout: 1000 }),
      /Element is not visible/u,
    )
  checks.push('real-zero-area-layout-has-no-viewport-point-hidden-layout-keeps-visibility-error')

  await page.setContent(
    '<!DOCTYPE html><iframe id=child srcdoc="<!DOCTYPE html><style>body{margin:0}</style><div id=parent><div style=height:1000px>Inner</div>Frame parent text.</div>"></iframe>',
  )
  const iframe = await page.$('#child')
  assert(iframe)
  const frame = await iframe.contentFrame()
  await iframe.dispose()
  assert(frame)
  await frame.evaluate(() => {
    globalThis.document.addEventListener('click', (event) => {
      globalThis.actualFrameTextClick = {
        trusted: event.isTrusted,
        target: event.target instanceof globalThis.Element ? event.target.id : null,
      }
    })
  })
  const childText = await frame.evaluateHandle(
    () => globalThis.document.querySelector('#parent')?.lastChild,
  )
  const child = childText.asElement()
  assert(child)
  try {
    await child.click({ timeout: 5000 })
    assert.deepEqual(click.parse(await frame.evaluate('globalThis.actualFrameTextClick')), {
      trusted: true,
      target: 'parent',
    })
    checks.push('pinned-child-frame-Text-scroll-and-trusted-root-mapped-input')
  } finally {
    await childText.dispose()
  }
  const stats = z
    .object({
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      inputWork: z.number(),
      queuedInput: z.number(),
      pressedKeys: z.number(),
      keyModifiers: z.number(),
      mouseButtons: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(stats)) assert.equal(value, 0, `Actual final ${key}`)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, stats }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, checks, error: error instanceof Error ? error.stack : String(error) },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
