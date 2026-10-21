// SPDX-License-Identifier: MIT
/* global document, getComputedStyle */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { EventEmitter, once } from 'node:events'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import image from 'sharp'
import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { nativeBrowserModuleFiles } from './native-browser-source-files.mjs'

const directory = resolve('.suitecut/native-artifact-documents')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const files = [
  executablePath,
  resolve('.suitecut/native-build/suitecut-browser'),
  ...[
    'dist/native-page.js',
    'dist/native-artifacts.js',
    ...(await nativeBrowserModuleFiles()),
    'native/browser/artifact_controls.h',
    'native/browser/controllers/artifacts/artifact_documents.inc',
    'native/browser/controllers/artifacts/artifact_navigation.inc',
    'native/browser/controllers/artifacts/artifact_before_screenshot.inc',
    'native/browser/controllers/artifacts/artifact_geometry.inc',
    'native/browser/controllers/artifacts/artifact_masks.inc',
    'native/browser/controllers/artifacts/artifact_view_lease.inc',
  ].map((path) => resolve(path)),
]
const provenance = z.record(z.string(), z.string()).parse({})
for (const file of files)
  provenance[file] = createHash('sha256')
    .update(await readFile(file))
    .digest('hex')
const fontControl = new EventEmitter()
let fontRequests = 0
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  if (request.url === '/pending-font') {
    ++fontRequests
    const release = () => {
      response.statusCode = 404
      response.end()
    }
    fontControl.once('release', release)
    response.once('close', () => fontControl.off('release', release))
    return
  }
  response.setHeader('Content-Type', 'text/html')
  if (request.url?.startsWith('/child')) {
    response.end(
      '<style>html,body{margin:0;background:white}#target{position:absolute;left:10px;top:10px;width:40px;height:20px;background:red}#input{position:absolute;left:5px;top:50px;width:80px;height:16px;border:0;font-size:12px;background:white;color:black}</style><div id="target"></div><input id="input" value="abcdef" style="caret-color:red!important">',
    )
    return
  }
  response.end(
    '<style>html,body{margin:0;background:blue}body{height:1000px}#target{position:absolute;left:5px;top:5px;width:20px;height:20px;background:red}iframe{position:absolute;width:120px;height:100px;border:3px solid black}#same{left:20px;top:30px}#cross{left:190px;top:30px;transform:scale(1.25);transform-origin:0 0}#below{left:20px;top:600px}</style><div id="target"></div>',
  )
})
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 360,
  height: 220,
  deviceScaleFactor: 2,
  headless: true,
  onDiagnostic: (message) => {
    diagnostics += message.slice(0, Math.max(0, 1024 * 1024 - diagnostics.length))
  },
}).catch((error) => {
  server.close()
  throw error
})
/**
 * Decode a real output pixel independently of native metadata.
 * @param {Buffer} encoded
 * @param {number} x
 * @param {number} y
 */
async function pixel(encoded, x, y) {
  const { data, info } = await image(encoded)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  return [...data.subarray((y * info.width + x) * 4, (y * info.width + x) * 4 + 4)]
}
try {
  const page = createNativePage(source)
  await page.goto(`http://127.0.0.1:${address.port}/`)
  await page.evaluate((port) => {
    for (const [id, host] of [
      ['same', '127.0.0.1'],
      ['cross', 'localhost'],
      ['below', 'localhost'],
    ]) {
      const frame = document.createElement('iframe')
      frame.id = id
      frame.src = `http://${host}:${port}/child`
      document.body.append(frame)
    }
  }, address.port)
  const same = page.frameLocator('#same').locator('#target')
  const cross = page.frameLocator('#cross').locator('#target')
  const below = page.frameLocator('#below').locator('#target')
  for (const target of [same, cross, below]) await target.waitFor()
  const initial = z
    .object({ sessions: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert(initial.sessions > 0, 'Child proof requires a genuine cross-site renderer session')
  const styled = await page.screenshot({
    fullPage: true,
    scale: 'css',
    style: '#target{background:lime!important}',
    path: `${directory}/all-documents-styled.png`,
  })
  assert.deepEqual(await pixel(styled, 10, 10), [0, 255, 0, 255])
  assert.deepEqual(await pixel(styled, 40, 50), [0, 255, 0, 255])
  assert.deepEqual(await pixel(styled, 218, 55), [0, 255, 0, 255])
  assert.deepEqual(await pixel(styled, 40, 620), [0, 255, 0, 255])
  const restored = await page.screenshot({
    fullPage: true,
    scale: 'css',
    path: `${directory}/all-documents-restored.png`,
  })
  for (const [x, y] of [
    [10, 10],
    [40, 50],
    [218, 55],
    [40, 620],
  ])
    assert.deepEqual(await pixel(restored, x, y), [255, 0, 0, 255])
  const element = await cross.screenshot({
    scale: 'css',
    style: '#target{width:24px!important;background:lime!important}',
  })
  assert.deepEqual(
    await image(element)
      .metadata()
      .then(({ width, height }) => [width, height]),
    [30, 25],
  )
  assert.deepEqual(await pixel(element, 10, 10), [0, 255, 0, 255])
  assert.equal(await cross.evaluate((target) => getComputedStyle(target).width), '40px')
  const masked = await page.screenshot({
    fullPage: true,
    scale: 'css',
    style: '#target{background:lime!important}',
    mask: [cross, below],
    path: `${directory}/styled-mapped-masks.png`,
  })
  assert.deepEqual(await pixel(masked, 218, 55), [255, 0, 255, 255])
  assert.deepEqual(await pixel(masked, 40, 620), [255, 0, 255, 255])

  const body = page.frameLocator('#cross').locator('body')
  await body.evaluate((body) => {
    const host = body.ownerDocument.createElement('div')
    host.id = 'host'
    host.style.cssText = 'position:absolute;left:5px;top:75px'
    body.append(host)
    host.attachShadow({ mode: 'open' }).innerHTML =
      '<style>@keyframes moving{from{transform:translateX(0)}to{transform:translateX(40px)}}#moving{width:10px;height:10px;background:purple;animation:moving 80ms linear infinite}</style><div id="moving"></div>'
  })
  const animation = await page.screenshot({
    animations: 'disabled',
    scale: 'css',
    path: `${directory}/child-shadow-animation.png`,
  })
  for (let i = 0; i < 3; ++i) {
    await new Promise((resolve) => globalThis.setTimeout(resolve, 100))
    assert.deepEqual(await page.screenshot({ animations: 'disabled', scale: 'css' }), animation)
  }
  assert.equal(
    await body.evaluate((body) =>
      body
        .querySelector('#host')
        ?.shadowRoot?.getAnimations()
        .every((animation) => animation.playState === 'running'),
    ),
    true,
  )
  const input = page.frameLocator('#cross').locator('#input')
  await input.focus()
  const caret = await input.evaluate((input) => [
    input.style.getPropertyValue('caret-color'),
    input.style.getPropertyPriority('caret-color'),
  ])
  const caretHidden = await page.screenshot({
    animations: 'disabled',
    caret: 'hide',
    scale: 'css',
    path: `${directory}/child-caret-hidden.png`,
  })
  for (let i = 0; i < 3; ++i) {
    await new Promise((resolve) => globalThis.setTimeout(resolve, 200))
    assert.deepEqual(
      await page.screenshot({ animations: 'disabled', caret: 'hide', scale: 'css' }),
      caretHidden,
    )
  }
  assert.deepEqual(
    await input.evaluate((input) => [
      input.style.getPropertyValue('caret-color'),
      input.style.getPropertyPriority('caret-color'),
    ]),
    caret,
  )

  // Hold a real child font fetch after applying temporary styles and inline
  // caret changes. Cancellation must restore that exact realm while its promise
  // can still complete later; no fresh capture may inherit those DOM effects.
  await body.evaluate((body) => {
    const style = body.ownerDocument.createElement('style')
    style.id = 'pending-font-style'
    style.textContent =
      '@font-face{font-family:Pending;src:url(/pending-font)}#input{font-family:Pending}'
    body.append(style)
  })
  const controller = new AbortController()
  const pending = page
    .screenshot({
      style: '#target{background:lime!important}',
      animations: 'disabled',
      scale: 'css',
      signal: controller.signal,
      timeout: 5000,
    })
    .then(
      () => {
        throw new Error('Held child font must keep capture pending')
      },
      (error) => String(error),
    )
  const waiting = performance.now() + 1500
  while (
    (await cross.evaluate((target) => getComputedStyle(target).backgroundColor)) !==
    'rgb(0, 255, 0)'
  ) {
    assert(
      performance.now() < waiting,
      'Child screenshot preparation must reach the real OOPIF document',
    )
    await new Promise((resolve) => globalThis.setTimeout(resolve, 10))
  }
  assert(fontRequests > 0, 'Cancellation must occur during an actual child font request')
  assert.equal(
    await input.evaluate((input) => input.style.getPropertyValue('caret-color')),
    'transparent',
  )
  const during = z
    .object({
      artifactDocumentJobs: z.number(),
      artifactDocumentContexts: z.number(),
      artifactPhysicalCapture: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(during.artifactDocumentJobs, 1)
  assert(during.artifactDocumentContexts > 0)
  assert.equal(during.artifactPhysicalCapture, 0)
  controller.abort(new Error('abort child font capture'))
  assert.match(String(await pending), /abort child font capture|cancel|abort/i)
  assert.equal(
    await cross.evaluate((target) => getComputedStyle(target).backgroundColor),
    'rgb(255, 0, 0)',
  )
  assert.deepEqual(
    await input.evaluate((input) => [
      input.style.getPropertyValue('caret-color'),
      input.style.getPropertyPriority('caret-color'),
    ]),
    caret,
  )
  assert.equal(
    await body.evaluate((body) =>
      body
        .querySelector('#host')
        ?.shadowRoot?.getAnimations()
        .every((animation) => animation.playState === 'running'),
    ),
    true,
  )
  await body.evaluate((body) => body.querySelector('#pending-font-style')?.remove())
  fontControl.emit('release')
  await body.evaluate((body) => body.ownerDocument.fonts.ready.then(() => true))
  const after = await page.screenshot({ animations: 'disabled', scale: 'css' })
  assert.deepEqual(await pixel(after, 218, 55), [255, 0, 0, 255])
  const state = z
    .object({
      otherOperations: z.number(),
      artifacts: z.number(),
      artifactPhysicalCapture: z.number(),
      artifactDocumentJobs: z.number(),
      artifactDocumentContexts: z.number(),
      pausedActions: z.number(),
      contextRetryQueued: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(state)) assert.equal(value, 0)
  for (const file of files)
    assert.equal(
      createHash('sha256')
        .update(await readFile(file))
        .digest('hex'),
      provenance[file],
      'Child document proof requires immutable runtime and SDK inputs',
    )
  await writeFile(
    `${directory}/report.json`,
    `${JSON.stringify({ result: 'passed', cefVersion: source.cefVersion, provenance, genuineCrossSiteSessions: initial.sessions, stylesAndRestorationAllActualDocuments: 'passed', offscreenOOPIFStylesAndMappedMaskPixels: 'passed', styledOOPIFLocatorGeometry: 'passed', childOpenShadowInfiniteAnimationPixelsAndRestoration: 'passed', childImportantCaretPixelsAndRestoration: 'passed', actualChildFontCancellationAndLateReadiness: { requests: fontRequests, preparedContexts: during.artifactDocumentContexts }, finalRegistries: state }, null, 2)}\n`,
  )
  console.info(
    'Native child document screenshot styles, animations, carets, OOPIF geometry/masks, cancellation and restoration passed',
  )
} catch (error) {
  await writeFile(
    `${directory}/failure.json`,
    `${JSON.stringify({ error: String(error), provenance, fontRequests }, null, 2)}\n`,
  )
  throw error
} finally {
  fontControl.emit('release')
  await writeFile(`${directory}/native-diagnostics.log`, diagnostics)
  await source.close()
  server.close()
}
