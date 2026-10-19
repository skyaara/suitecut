// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_HOVER_DIRECTORY ?? '.suitecut/native-hover-proof',
)
await mkdir(directory, { recursive: true })
const nativeStderr = await captureNativeStderr(directory)
const behaviorErrors = z.array(z.unknown()).parse([])
const checks = []
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: nativeStderr.observe,
})
const page = createNativePage(source)
const eventsSchema = z.array(
  z.object({
    target: z.string(),
    x: z.number(),
    y: z.number(),
    trusted: z.boolean(),
    shift: z.boolean(),
    control: z.boolean(),
  }),
)
/** Reads actual captured pointer events from the page. */
const events = async () => eventsSchema.parse(await page.evaluate('globalThis.hoverEvents'))
try {
  await page.setContent(`<!DOCTYPE html><style>
    body{margin:0}button{position:absolute;width:140px;height:40px}
    #disabled{left:20px;top:20px}#aria{left:190px;top:20px}
    #parent{left:20px;top:90px;width:260px;height:80px}
    #selected{position:absolute;left:180px;top:10px;width:30px;height:20px}
    #blocked{left:20px;top:210px;width:140px;height:50px}
    #overlay{position:absolute;left:20px;top:210px;width:140px;height:50px;z-index:3;background:red}
    #pinned{left:350px;top:210px}#replacement{left:350px;top:270px}
    .duplicate{top:340px}.duplicate:first-of-type{left:20px}
    iframe{position:absolute;left:350px;top:20px;width:220px;height:120px;border:4px solid black}
  </style><button id=disabled disabled>Disabled</button>
  <div aria-disabled=true><button id=aria>Aria descendant</button></div>
  <button id=parent>Parent<span id=selected>Child</span></button>
  <button id=blocked>Blocked</button><div id=overlay>Overlay</div>
  <button id=pinned>Pinned</button><button id=replacement>Replacement identity</button>
  <button class=duplicate id=first style="left:20px">First</button>
  <button class=duplicate id=second style="left:190px">Second</button>
  <iframe id=child srcdoc="<!DOCTYPE html><style>body{margin:0}button{position:absolute;left:20px;top:20px;width:140px;height:50px}</style><button id=framebutton disabled>Frame disabled</button>"></iframe>`)
  await page.evaluate(() => {
    /** @type {{target:string,x:number,y:number,trusted:boolean,shift:boolean,control:boolean}[]} */
    const captured = []
    globalThis.hoverEvents = captured
    globalThis.document.addEventListener(
      'pointermove',
      (event) => {
        if (!(event.target instanceof globalThis.Element))
          throw new Error('Actual pointer target is not an element')
        captured.push({
          target: event.target.id,
          x: event.clientX,
          y: event.clientY,
          trusted: event.isTrusted,
          shift: event.shiftKey,
          control: event.ctrlKey,
        })
      },
      true,
    )
  })

  assert.equal(await page.evaluate('document.compatMode'), 'CSS1Compat')
  await page.hover('#disabled', { timeout: 1000 })
  assert.equal(await page.evaluate('document.querySelector("#disabled").matches(":hover")'), true)
  assert((await events()).some((event) => event.target === 'disabled' && event.trusted))
  checks.push('visible-disabled-control-receives-genuine-hover')

  await page.locator('#aria').hover({ timeout: 1000 })
  assert.equal(await page.evaluate('document.querySelector("#aria").matches(":hover")'), true)
  assert((await events()).some((event) => event.target === 'aria' && event.trusted))
  checks.push('aria-disabled-ancestor-does-not-block-hover')

  const selected = z.object({ x: z.number(), y: z.number() }).parse(
    await page.locator('#selected').evaluate((element) => {
      const rect = element.getBoundingClientRect()
      return { x: rect.left + 3, y: rect.top + 4 }
    }),
  )
  await page
    .locator('#selected')
    .hover({ position: { x: 3, y: 4 }, steps: 4, modifiers: ['Shift'], timeout: 1000 })
  const selectedEvent = (await events()).at(-1)
  assert(selectedEvent)
  assert.equal(selectedEvent.target, 'selected')
  assert.equal(selectedEvent.x, Math.floor(selected.x))
  assert.equal(selectedEvent.y, Math.floor(selected.y))
  assert(selectedEvent.trusted && selectedEvent.shift)
  await page.mouse.move(0, 0)
  assert.equal((await events()).at(-1)?.shift, false)
  checks.push('exact-child-offset-and-trusted-modifiers-without-ancestor-retarget')

  const trialBefore = await events()
  await page.locator('#selected').hover({ trial: true, timeout: 1000 })
  assert.deepEqual(await events(), trialBefore)
  checks.push('trial-validates-without-moving-pointer')

  await assert.rejects(page.locator('#blocked').hover({ timeout: 120 }), (error) => {
    assert(error instanceof Error)
    assert.match(error.message, /Timeout|timed out/u)
    return true
  })
  assert.equal(await page.evaluate('document.querySelector("#blocked").matches(":hover")'), false)
  await page.locator('#blocked').hover({ force: true, timeout: 1000 })
  const forced = (await events()).at(-1)
  assert(forced?.trusted)
  assert.equal(forced.target, 'overlay')
  await page.locator('#overlay').evaluate((element) => element.remove())
  await page.locator('#blocked').hover({ timeout: 1000 })
  assert.equal(await page.evaluate('document.querySelector("#blocked").matches(":hover")'), true)
  checks.push('obstruction-times-out-force-preserves-real-hit-target-then-uncovered-hover')

  const pinned = await page.$('#pinned')
  assert(pinned)
  try {
    await pinned.evaluate((element) => {
      if (!(element instanceof globalThis.HTMLElement))
        throw new Error('Pinned fixture is not an HTML element')
      element.id = 'renamed'
      const decoy = globalThis.document.createElement('button')
      decoy.id = 'pinned'
      decoy.style.cssText = 'left:350px;top:400px'
      globalThis.document.body.append(decoy)
    })
    await pinned.hover({ timeout: 1000 })
    assert.equal((await events()).at(-1)?.target, 'renamed')
    assert.equal(await page.evaluate('document.querySelector("#pinned").matches(":hover")'), false)
  } finally {
    await pinned.dispose()
  }
  const detached = await page.$('#replacement')
  assert(detached)
  try {
    await detached.evaluate((element) => {
      if (!(element instanceof globalThis.Element))
        throw new Error('Detached fixture is not an element')
      element.replaceWith(element.cloneNode(true))
    })
    const before = await events()
    await assert.rejects(detached.hover({ timeout: 1000 }), /not attached|detached/u)
    assert.deepEqual(await events(), before)
    assert.equal(
      await page.evaluate('document.querySelector("#replacement").matches(":hover")'),
      false,
    )
  } finally {
    await detached.dispose()
  }
  checks.push('pinned-handle-retains-node-and-detached-handle-never-hovers-replacement')

  const owner = await page.$('#child')
  assert(owner)
  const frame = await owner.contentFrame()
  await owner.dispose()
  assert(frame)
  await frame.locator('#framebutton').waitFor()
  assert.equal(await frame.evaluate('document.compatMode'), 'CSS1Compat')
  await frame.evaluate(() => {
    globalThis.frameHover = null
    globalThis.document.addEventListener(
      'pointermove',
      (event) => {
        if (!(event.target instanceof globalThis.Element))
          throw new Error('Actual frame pointer target is not an element')
        globalThis.frameHover = {
          target: event.target.id,
          trusted: event.isTrusted,
          control: event.ctrlKey,
        }
      },
      true,
    )
  })
  const button = await frame.$('#framebutton')
  assert(button)
  try {
    await button.hover({ modifiers: ['Control'], position: { x: 4, y: 5 }, timeout: 1000 })
    const actual = z
      .object({ target: z.string(), trusted: z.boolean(), control: z.boolean() })
      .parse(await frame.evaluate('globalThis.frameHover'))
    assert.deepEqual(actual, { target: 'framebutton', trusted: true, control: true })
    assert.equal(
      await frame.evaluate('document.querySelector("#framebutton").matches(":hover")'),
      true,
    )
  } finally {
    await button.dispose()
  }
  await page.mouse.move(0, 0)
  await page.frameLocator('#child').locator('#framebutton').hover({ timeout: 1000 })
  assert.equal(
    await frame.evaluate('document.querySelector("#framebutton").matches(":hover")'),
    true,
  )
  checks.push('pinned-frame-and-lazy-frame-hover-use-genuine-child-coordinate-routing')

  await page.hover('.duplicate', { timeout: 1000 })
  assert.equal((await events()).at(-1)?.target, 'first')
  await assert.rejects(page.locator('.duplicate').hover({ timeout: 1000 }), /Strict|strict/u)
  checks.push('legacy-page-first-match-and-strict-locator-remain-distinct')

  const diagnostics = z
    .object({
      activeHandles: z.literal(0),
      pendingHandles: z.literal(0),
      evaluationGroups: z.literal(0),
      inputWork: z.literal(0),
      queuedInput: z.literal(0),
      pressedKeys: z.literal(0),
      keyModifiers: z.literal(0),
      mouseButtons: z.literal(0),
    })
    .parse(await source.runAutomation('diagnostics'))
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        checks,
        diagnostics,
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        scope:
          'Authored real native hover state, actual trusted pointer events, source coordinates and ownership. No synthetic event dispatch.',
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
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
  await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
}
