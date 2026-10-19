// SPDX-License-Identifier: MIT
/* global document, window, Element */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-bounding-box'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 480,
  height: 360,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
const stateSchema = z.object({
  nativeElementBounds: z.number(),
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeHandleDisposals: z.number(),
  nativeHandleDisposalPhysical: z.number(),
  pendingDevTools: z.number(),
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const errors = []
/** @type {unknown[]} */
const observations = []
const rectSchema = z.object({ x: z.number(), y: z.number(), width: z.number(), height: z.number() })
/**
 * Compare actual native geometry while retaining subpixel coordinates.
 * @param {{x:number,y:number,width:number,height:number}|null} actual
 * @param {{x:number,y:number,width:number,height:number}} expected
 */
function near(actual, expected) {
  assert(actual)
  for (const key of z.enum(['x', 'y', 'width', 'height']).options)
    assert(
      Math.abs(actual[key] - expected[key]) < 0.01,
      `${key}: ${actual[key]} vs ${expected[key]}`,
    )
  observations.push({ actual, expected })
}
/**
 * Read the real Inspector quad of the selected main-frame/same-process DOM node.
 * The expression selects the exact proof node; it does not draw or change DOM.
 * @param {string} expression
 */
async function inspectorBox(expression) {
  const object = z.object({ result: z.object({ objectId: z.string() }) }).parse(
    await page.sendDevToolsCommand('Runtime.evaluate', {
      expression,
      returnByValue: false,
      objectGroup: 'suitecut-geometry-proof',
    }),
  ).result.objectId
  try {
    const model = z
      .object({ model: z.object({ border: z.array(z.number()).length(8) }) })
      .parse(await page.sendDevToolsCommand('DOM.getBoxModel', { objectId: object }))
    const quad = model.model.border,
      xs = quad.filter((_value, index) => index % 2 === 0),
      ys = quad.filter((_value, index) => index % 2 === 1)
    observations.push({ actualInspectorQuad: quad })
    return {
      x: Math.min(...xs),
      y: Math.min(...ys),
      width: Math.max(...xs) - Math.min(...xs),
      height: Math.max(...ys) - Math.min(...ys),
    }
  } finally {
    await page.sendDevToolsCommand('Runtime.releaseObjectGroup', {
      objectGroup: 'suitecut-geometry-proof',
    })
  }
}
async function retired() {
  const deadline = performance.now() + 2000
  let state = stateSchema.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = stateSchema.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
const childHTML =
  '<style>html,body{margin:0}#target{position:absolute;left:20px;top:30px;width:60px;height:25px;padding:0;border:0;background:lime}</style><button id="target">Actual</button>'
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end(childHTML)
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
try {
  await page.setContent(
    '<style>html,body{margin:0}</style><button id="target" style="position:absolute;left:20px;top:30px;width:60px;height:25px;border:0;padding:0">Actual</button>',
  )
  const locator = page.locator('#target'),
    fixed = await locator.elementHandle()
  assert(fixed)
  const truth = await inspectorBox('document.querySelector("#target")')
  near(await locator.boundingBox(), truth)
  near(await fixed.boundingBox(), truth)
  checks.push('locator and retained handle agree with the actual Inspector border quad')
  await page.evaluate(() => {
    Element.prototype.getBoundingClientRect = function () {
      throw new Error('Actual author DOMRect poison')
    }
    globalThis.getComputedStyle = function () {
      throw new Error('Actual author style poison')
    }
  })
  near(await locator.boundingBox(), truth)
  near(await fixed.boundingBox(), truth)
  await assert.rejects(
    locator.evaluate((element) => element.getBoundingClientRect().x),
    /Actual author DOMRect poison/u,
  )
  checks.push(
    'native geometry uses actual utility wrappers while public authored evaluation preserves poisoned author semantics',
  )
  await page.evaluate(() => {
    const original = document.querySelector('#target')
    if (!original) throw new Error('Missing original actual target')
    original.remove()
    const newer = document.createElement('button')
    newer.id = 'target'
    newer.style.cssText =
      'position:absolute;left:70px;top:90px;width:40px;height:20px;border:0;padding:0'
    document.body.append(newer)
  })
  assert.equal(await fixed.boundingBox(), null)
  near(await locator.boundingBox(), await inspectorBox('document.querySelector("#target")'))
  await fixed.dispose()
  await retired()
  checks.push(
    'retained identity never rematches a replacement element while the lazy locator resolves the genuine replacement',
  )
  await page.goto('about:blank')
  await page.setContent(
    '<div id="hidden" style="display:none">Hidden</div><div id="zero" style="width:0;height:0"></div><div id="contents" style="display:contents"><span>Actual child</span></div>',
  )
  for (const id of ['hidden', 'zero', 'contents'])
    assert.equal(await page.locator('#' + id).boundingBox(), null)
  checks.push('actual no-layout and zero-size boxes return null without a fabricated rectangle')
  await page.setContent(
    '<style>body{margin:0}</style><iframe id="child" style="position:absolute;left:100px;top:50px;width:160px;height:140px;border:7px solid black"></iframe>',
  )
  await page
    .locator('#child')
    .evaluate((element, html) => element.setAttribute('srcdoc', html), childHTML)
  const child = page.frameLocator('#child').locator('#target')
  await child.waitFor({ timeout: 2000 })
  const sameTruth = await inspectorBox(
    'document.querySelector("iframe").contentDocument.querySelector("#target")',
  )
  near(await child.boundingBox(), sameTruth)
  const sameFixed = await child.elementHandle()
  assert(sameFixed)
  near(await sameFixed.boundingBox(), sameTruth)
  await sameFixed.dispose()
  checks.push(
    'same-process iframe locator and fixed handle return actual main-viewport coordinates including native frame borders',
  )
  await page.goto(`http://127.0.0.1:${address.port}/main`)
  await page.setContent(
    `<style>body{margin:0}</style><iframe id="child" src="http://localhost:${address.port}/child" style="position:absolute;left:100px;top:50px;width:160px;height:140px;border:7px solid black"></iframe>`,
  )
  const cross = page.frameLocator('#child').locator('#target')
  await cross.waitFor({ timeout: 2000 })
  const crossFixed = await cross.elementHandle()
  assert(crossFixed)
  const frame = await crossFixed.ownerFrame()
  assert(frame)
  const local = rectSchema.parse(
    await cross.evaluate((element) => {
      const r = element.getBoundingClientRect()
      return { x: r.x, y: r.y, width: r.width, height: r.height }
    }),
  )
  const frameTruth = await inspectorBox('document.querySelector("iframe")')
  const border = await page
    .locator('iframe')
    .evaluate((element) => ({ left: element.clientLeft, top: element.clientTop }))
  const expected = {
    x: frameTruth.x + border.left + local.x,
    y: frameTruth.y + border.top + local.y,
    width: local.width,
    height: local.height,
  }
  near(await cross.boundingBox(), expected)
  near(await crossFixed.boundingBox(), expected)
  z.object({ sessions: z.number().positive() }).parse(await source.runAutomation('diagnostics'))
  checks.push(
    'a genuine OOPIF border quad maps through its actual parent frame content into the root viewport',
  )
  await frame.evaluate(() => {
    Element.prototype.getBoundingClientRect = function () {
      throw new Error('Actual child DOMRect poison')
    }
    globalThis.getComputedStyle = function () {
      throw new Error('Actual child style poison')
    }
  })
  near(await cross.boundingBox(), expected)
  near(await crossFixed.boundingBox(), expected)
  await crossFixed.dispose()
  await retired()
  checks.push('OOPIF geometry remains genuine after author-world DOMRect and style replacement')
  await page.goto('about:blank')
  await page.setContent(
    '<style>body{margin:0}</style><svg width="200" height="200"><rect id="target" x="30" y="50" width="80" height="90"/></svg>',
  )
  near(
    await page.locator('#target').boundingBox(),
    await inspectorBox('document.querySelector("#target")'),
  )
  checks.push('native SVG geometry reads its actual layout border quad')
  await page.setContent(
    '<style>body{margin:0}</style><div style="height:2000px"></div><button id="target" style="position:absolute;left:20px;top:500px;width:60px;height:25px;border:0;padding:0">Scroll</button>',
  )
  await page.evaluate(() => window.scrollTo(0, 450))
  near(
    await page.locator('#target').boundingBox(),
    await inspectorBox('document.querySelector("#target")'),
  )
  checks.push(
    'scroll offsets retain actual viewport-relative geometry without clamping offscreen coordinates',
  )
  await page.setContent('<button>One</button><button>Two</button>')
  await assert.rejects(
    page.locator('button').boundingBox(),
    /strict mode violation|Strict locator/u,
  )
  checks.push('native strictness rejects real multiple selected nodes before geometry publication')
  await assert.rejects(
    page.locator('#never-attached').boundingBox({ timeout: 30 }),
    /Timeout|timed out/u,
  )
  await retired()
  checks.push(
    'missing attached targets honor the original native timeout and retire their real temporary groups',
  )
  const abort = new AbortController(),
    reason = new Error('Actual bounding box abort')
  const pending = page.locator('#still-absent').boundingBox({ timeout: 0, signal: abort.signal }),
    rejection = assert.rejects(pending, /Actual bounding box abort/u)
  assert(
    stateSchema.parse(await source.runAutomation('diagnostics')).nativeElementBounds > 0,
    'Native missing-target operation must be genuinely admitted before caller cancellation',
  )
  abort.abort(reason)
  await rejection
  const final = await retired()
  checks.push(
    'real caller cancellation preserves its reason and all native operation, handle, group and physical counters retire',
  )
  assert.equal(checks.length, 12)
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ completed: true, checks, observations, state: final }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    directory + '/failure.json',
    JSON.stringify(
      {
        completed: false,
        checks,
        observations,
        error:
          error instanceof Error ? { message: error.message, stack: error.stack } : String(error),
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
if (errors.length) throw errors[0]
