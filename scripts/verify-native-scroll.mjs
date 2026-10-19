// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_SCROLL_DIRECTORY ?? '.suitecut/native-scroll-proof',
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
/** Gives actual native asynchronous layout observation a rendering opportunity. */
const rendered = () =>
  page.evaluate(
    () =>
      new Promise((resolve) =>
        globalThis.requestAnimationFrame(() =>
          globalThis.requestAnimationFrame(() => resolve(null)),
        ),
      ),
  )
try {
  await page.setContent(`<!DOCTYPE html><style>body{margin:0}.box{height:40px;width:80px}
    @keyframes move{from{margin-left:0}to{margin-left:200px}}
    #moving{animation:move 2s linear infinite alternate}
  </style><div id=hidden class=box style="display:none">Hidden</div>
  <div id=invisible class=box style="visibility:hidden">Invisible</div><div id=zero style="height:0">Zero</div>
  <button id=disabled disabled>Disabled</button><div id=moving class=box>Moving</div>`)
  const hidden = await page.$('#hidden')
  assert(hidden)
  let hiddenCompleted = false
  const waiting = hidden.scrollIntoViewIfNeeded({ timeout: 5000 }).then(() => {
    hiddenCompleted = true
  })
  await rendered()
  assert.equal(hiddenCompleted, false)
  await hidden.evaluate((e) => {
    if (!(e instanceof globalThis.HTMLElement))
      throw new Error('Hidden fixture is not an HTML element')
    e.style.display = 'block'
  })
  await waiting
  assert.equal(hiddenCompleted, true)
  checks.push('native-pinned-hidden-node-waits-for-layout')
  await page.locator('#invisible').scrollIntoViewIfNeeded({ timeout: 1000 })
  await page.locator('#zero').scrollIntoViewIfNeeded({ timeout: 1000 })
  await page.locator('#disabled').scrollIntoViewIfNeeded({ timeout: 1000 })
  checks.push('visibility-hidden-zero-sized-disabled-allowed')
  const moving = await page.$('#moving')
  assert(moving)
  let movingCompleted = false
  const stable = moving.scrollIntoViewIfNeeded({ timeout: 5000 }).then(() => {
    movingCompleted = true
  })
  await rendered()
  assert.equal(movingCompleted, false)
  await moving.evaluate((e) => {
    if (!(e instanceof globalThis.HTMLElement))
      throw new Error('Moving fixture is not an HTML element')
    e.style.animation = 'none'
  })
  await stable
  checks.push('native-original-deadline-waits-real-layout-stability')
  await hidden.evaluate((e) => {
    if (!(e instanceof globalThis.Element)) throw new Error('Pinned fixture is not an element')
    e.id = 'pinned'
    const decoy = globalThis.document.createElement('div')
    decoy.id = 'hidden'
    e.after(decoy)
  })
  await hidden.scrollIntoViewIfNeeded({ timeout: 1000 })
  await hidden.evaluate((e) => {
    if (!(e instanceof globalThis.Element)) throw new Error('Pinned fixture is not an element')
    e.remove()
  })
  await assert.rejects(
    hidden.scrollIntoViewIfNeeded({ timeout: 1000 }),
    /Element is not attached to the DOM/u,
  )
  await hidden.dispose()
  await moving.dispose()
  checks.push('renamed-handle-identity-preserved-detachment-rejects')

  await page.setContent(`<!DOCTYPE html><style>
    html,body{margin:0;padding:0;width:100%;height:100%}::-webkit-scrollbar{display:none}*{scrollbar-width:none}
  </style><div id=scroller style="width:200px;height:200px;overflow:scroll;border:1px solid black">
    <div style="margin-top:500px;background:red"><div style="height:50px;width:100px;background:cyan">
      <div id=contents style="display:contents">Hello</div></div></div></div>`)
  await page.locator('#contents').scrollIntoViewIfNeeded({ timeout: 1000 })
  const first = z.number().parse(await page.locator('#scroller').evaluate((e) => e.scrollTop))
  assert(Math.abs(first - 350) < 1, `Actual native display:contents scrollTop=${first}`)
  await page.locator('#contents').scrollIntoViewIfNeeded({ timeout: 1000 })
  assert.equal(await page.locator('#scroller').evaluate((e) => e.scrollTop), first)
  checks.push('genuine-DOM-command-display-contents-minimal-scroll-no-repeat')

  await page.setContent(
    '<!DOCTYPE html><div class=duplicate>A</div><div class=duplicate>B</div><div id=waiting style="display:none">Wait</div>',
  )
  await assert.rejects(
    page.locator('.duplicate').scrollIntoViewIfNeeded({ timeout: 1000 }),
    /Strict locator matched multiple elements/u,
  )
  page.setDefaultTimeout(80)
  await assert.rejects(page.locator('#waiting').scrollIntoViewIfNeeded(), (error) => {
    assert(error instanceof Error)
    assert.match(error.message, /Timeout/u)
    assert.match(error.message, /element is not visible/u)
    assert.match(error.message, /retrying scroll into view action/u)
    return true
  })
  page.setDefaultTimeout(5000)
  const controller = new AbortController(),
    reason = new Error('actual-native-scroll-caller-cancel')
  const canceled = page
    .locator('#waiting')
    .scrollIntoViewIfNeeded({ timeout: 0, signal: controller.signal })
  await rendered()
  await rendered()
  const during = z
    .object({ evaluationGroups: z.number(), activeHandles: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(during.activeHandles, 0)
  assert.equal(
    during.evaluationGroups,
    1,
    'One original command group retained across actual retries',
  )
  controller.abort(reason)
  await assert.rejects(canceled, (error) => {
    assert(error instanceof Error)
    assert.match(error.message, /actual-native-scroll-caller-cancel/u)
    return true
  })
  checks.push('strictness-default-timeout-and-cancel-original-owner-with-bounded-retry-group')

  await page.setContent(
    '<!DOCTYPE html><iframe id=child srcdoc="<!DOCTYPE html><style>body{margin:0}</style><div style=height:900px></div><button id=target disabled>Target</button>"></iframe>',
  )
  await page.frameLocator('#child').locator('#target').scrollIntoViewIfNeeded({ timeout: 5000 })
  const frameHandle = await page.$('#child')
  assert(frameHandle)
  const frame = await frameHandle.contentFrame()
  assert(frame)
  const target = await frame.$('#target')
  assert(target)
  assert.equal(
    await target.evaluate((e) => {
      if (!(e instanceof globalThis.Element)) throw new Error('Child fixture is not an element')
      const r = e.getBoundingClientRect()
      return r.top >= 0 && r.bottom <= globalThis.innerHeight
    }),
    true,
  )
  await target.scrollIntoViewIfNeeded({ timeout: 1000 })
  await target.dispose()
  await frameHandle.dispose()
  checks.push('lazy-and-pinned-child-frame-native-scrolling')
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
  for (const [key, value] of Object.entries(stats)) assert.equal(value, 0, `Final actual ${key}`)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, displayContentsScrollTop: first, stats }, null, 2) +
      '\n',
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
