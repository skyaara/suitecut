// SPDX-License-Identifier: MIT
/* global document, getComputedStyle, matchMedia, KeyframeEffect, window */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { promisify } from 'node:util'

import image from 'sharp'
import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { nativeBrowserModuleFiles } from './native-browser-source-files.mjs'

const directory = resolve('.suitecut/native-artifacts')
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const provenancePaths = [
  executablePath,
  ...(await nativeBrowserModuleFiles()),
  resolve('dist/native-artifacts.js'),
  resolve('dist/native-browser.js'),
  resolve('dist/native-page.js'),
  resolve('dist/native-protocol.js'),
  resolve('native/browser/artifact_controls.h'),
  resolve('native/browser/controllers/artifacts/artifact_geometry.inc'),
  resolve('native/browser/controllers/artifacts/artifact_masks.inc'),
  resolve('native/browser/controllers/artifacts/artifact_view_lease.inc'),
  resolve('native/browser/controllers/artifacts/artifact_before_screenshot.inc'),
]
const provenance = z.record(z.string(), z.string()).parse({})
await Promise.all(
  provenancePaths.map(async (path) => {
    provenance[path] = createHash('sha256')
      .update(await readFile(path))
      .digest('hex')
  }),
)
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 200,
  height: 120,
  deviceScaleFactor: 2,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
/**
 * Reads encoded image dimensions independently of native metadata.
 * @param {Buffer} buffer
 */
const dimensions = async (buffer) => {
  const { width, height, format } = await image(buffer).metadata()
  return { width, height, format }
}
/**
 * Decodes an actual output pixel as RGBA.
 * @param {Buffer} buffer
 * @param {number} x
 * @param {number} y
 */
const pixel = async (buffer, x, y) => {
  const { data, info } = await image(buffer)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  return [...data.subarray((y * info.width + x) * 4, (y * info.width + x) * 4 + 4)]
}
try {
  const page = createNativePage(source)
  await page.setContent(
    '<style>html,body{margin:0}body{background:rgb(0,0,255);height:1000px}</style><p>Native artifact proof</p>',
  )
  const css = await page.screenshot({ scale: 'css', path: `${directory}/viewport-css.png` })
  assert.deepEqual(await dimensions(css), { width: 200, height: 120, format: 'png' })
  assert.deepEqual(await pixel(css, 160, 119), [0, 0, 255, 255])
  assert.deepEqual(await readFile(`${directory}/viewport-css.png`), css)
  const device = await page.screenshot({ path: `${directory}/viewport-device.png` })
  assert.deepEqual(await dimensions(device), { width: 400, height: 240, format: 'png' })
  const full = await page.screenshot({
    fullPage: true,
    scale: 'css',
    path: `${directory}/fullpage.png`,
  })
  assert.deepEqual(await dimensions(full), { width: 200, height: 1000, format: 'png' })
  assert.deepEqual(await pixel(full, 160, 999), [0, 0, 255, 255])
  const clip = await page.screenshot({
    fullPage: true,
    clip: { x: 10, y: 800, width: 35, height: 45 },
    scale: 'css',
  })
  assert.deepEqual(await dimensions(clip), { width: 35, height: 45, format: 'png' })
  assert.deepEqual(await pixel(clip, 34, 44), [0, 0, 255, 255])
  await assert.rejects(
    page.screenshot({ clip: { x: 10, y: 800, width: 35, height: 45 } }),
    /outside the resulting image/,
  )
  const intersected = await page.screenshot({
    clip: { x: 160, y: 100, width: 100, height: 50 },
    scale: 'css',
  })
  assert.deepEqual(await dimensions(intersected), { width: 40, height: 20, format: 'png' })
  for (const type of ['jpeg', 'webp']) {
    const captured = await page.screenshot({
      type,
      quality: 80,
      scale: 'css',
      path: `${directory}/viewport.${type}`,
    })
    assert.deepEqual(await dimensions(captured), { width: 200, height: 120, format: type })
  }
  const [red, green] = await Promise.all([
    page.screenshot({ scale: 'css', style: 'body{background:red!important}' }),
    page.screenshot({ scale: 'css', style: 'body{background:lime!important}' }),
  ])
  assert.deepEqual(await pixel(red, 160, 119), [255, 0, 0, 255])
  assert.deepEqual(await pixel(green, 160, 119), [0, 255, 0, 255])
  assert.equal(
    await page.evaluate(() => getComputedStyle(document.body).backgroundColor),
    'rgb(0, 0, 255)',
  )
  await page.setContent(
    '<style>html,body{margin:0;background:transparent}</style><div style="width:50px;height:50px;background:red"></div>',
  )
  const transparent = await page.screenshot({
    omitBackground: true,
    scale: 'css',
    path: `${directory}/transparent.png`,
  })
  assert.deepEqual(await pixel(transparent, 199, 119), [0, 0, 0, 0])
  assert.deepEqual(
    await pixel(await page.screenshot({ scale: 'css' }), 199, 119),
    [255, 255, 255, 255],
  )
  await page.setContent(
    '<style>html,body{margin:0}@keyframes shade{from{background:red}to{background:lime}}div{width:50px;height:50px;background:blue;display:inline-block}#finite{animation:shade 60s linear forwards}#infinite{animation:shade 60s linear infinite}</style><div id="finite"></div><div id="infinite"></div>',
  )
  const disabled = await page.screenshot({ animations: 'disabled', scale: 'css' })
  assert.deepEqual(await pixel(disabled, 25, 25), [0, 255, 0, 255])
  assert.deepEqual(await pixel(disabled, 75, 25), [0, 0, 255, 255])
  assert.equal(
    await page.evaluate(
      () =>
        document
          .getAnimations()
          .find(
            (animation) =>
              animation.effect instanceof KeyframeEffect &&
              animation.effect.target?.id === 'infinite',
          )?.playState,
    ),
    'running',
  )
  await page.emulateMedia({ colorScheme: 'dark', reducedMotion: 'reduce', media: 'screen' })
  assert.equal(await page.evaluate(() => matchMedia('(prefers-color-scheme:dark)').matches), true)
  await page.emulateMedia({ reducedMotion: null })
  assert.equal(await page.evaluate(() => matchMedia('(prefers-color-scheme:dark)').matches), true)
  assert.equal(
    await page.evaluate(() => matchMedia('(prefers-reduced-motion:reduce)').matches),
    false,
  )
  await page.emulateMedia({ colorScheme: null, media: null })
  await page.setContent('<style>html,body{margin:0}</style>')
  await page.evaluate(() => {
    const root = document.body.attachShadow({ mode: 'open' })
    root.innerHTML =
      '<style>div{position:absolute;left:20px;top:20px;width:40px;height:20px;background:red;animation:spin 1s linear infinite}@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}</style><div></div>'
  })
  const shadowDisabled = await page.screenshot({ scale: 'css', animations: 'disabled' })
  for (let index = 0; index < 5; ++index) {
    await page.evaluate(
      () =>
        new Promise((resolve) =>
          window.requestAnimationFrame(() => window.requestAnimationFrame(resolve)),
        ),
    )
    assert.deepEqual(
      await page.screenshot({ scale: 'css', animations: 'disabled' }),
      shadowDisabled,
      'Disabled open-shadow CSS animations must produce stable actual pixels',
    )
  }
  assert.equal(
    await page.evaluate(() => document.body.shadowRoot.getAnimations()[0]?.playState),
    'running',
  )
  await page.setContent(
    '<style>html,body{margin:0}#cancel-mask{position:absolute;left:0;top:0;width:10px;height:10px}</style><canvas width="3000" height="2000"></canvas><div id="cancel-mask"></div>',
  )
  await page.evaluate(() => {
    const canvas = document.querySelector('canvas')
    const context = canvas.getContext('2d')
    const pixels = context.createImageData(canvas.width, canvas.height)
    let random = 0x12345678
    for (let index = 0; index < pixels.data.length; index += 4) {
      random ^= random << 13
      random ^= random >>> 17
      random ^= random << 5
      pixels.data[index] = random & 255
      pixels.data[index + 1] = (random >>> 8) & 255
      pixels.data[index + 2] = (random >>> 16) & 255
      pixels.data[index + 3] = 255
    }
    context.putImageData(pixels, 0, 0)
  })
  const cancellation = new AbortController()
  let captureSettled = false
  const capturing = page
    .screenshot({
      fullPage: true,
      scale: 'css',
      omitBackground: true,
      mask: [page.locator('#cancel-mask')],
      signal: cancellation.signal,
    })
    .then(
      () => {
        captureSettled = true
        return { canceled: false, error: '' }
      },
      (error) => {
        captureSettled = true
        return { canceled: true, error: String(error) }
      },
    )
  const PhysicalSchema = z.object({ artifactPhysicalCapture: z.number() })
  let observedPhysicalCapture = false
  const phaseDeadline = performance.now() + 10_000
  for (
    let attempt = 0;
    attempt < 100_000 && !captureSettled && performance.now() < phaseDeadline;
    ++attempt
  ) {
    const phase = PhysicalSchema.parse(await source.runAutomation('diagnostics'))
    if (phase.artifactPhysicalCapture === 1) {
      observedPhysicalCapture = true
      break
    }
  }
  assert.equal(
    observedPhysicalCapture,
    true,
    'Must cancel a real physical capture, not a queued operation',
  )
  cancellation.abort()
  const following = page.screenshot({
    scale: 'css',
    style: 'canvas{display:none!important}body{background:lime!important}',
  })
  const canceled = await capturing
  assert.equal(canceled.canceled, true)
  assert.match(canceled.error, /abort|cancel/i)
  const afterCancellation = await following
  assert.deepEqual(await pixel(afterCancellation, 199, 119), [0, 255, 0, 255])
  assert.deepEqual(await pixel(afterCancellation, 5, 5), [0, 255, 0, 255])
  assert.equal(await page.evaluate(() => document.querySelectorAll('div').length), 1)
  await page.setContent(
    '<style>html,body{margin:0}body{height:1000px;background:blue}#target{position:absolute;left:20px;top:500px;width:40px;height:30px;background:red}</style><div id="target"></div>',
  )
  const target = page.locator('#target')
  const element = await target.screenshot({ scale: 'css', path: `${directory}/locator.png` })
  assert.deepEqual(await dimensions(element), { width: 40, height: 30, format: 'png' })
  assert.deepEqual(await pixel(element, 20, 15), [255, 0, 0, 255])
  await page.evaluate(() => window.scrollTo(0, 0))
  const fullPageMask = await page.screenshot({
    scale: 'css',
    fullPage: true,
    mask: [target],
    path: `${directory}/masked-fullpage.png`,
  })
  assert.deepEqual(await pixel(fullPageMask, 25, 505), [255, 0, 255, 255])
  const successiveFullPageStyles = await Promise.all([
    page.screenshot({
      fullPage: true,
      scale: 'css',
      style: '#target{background:lime!important}',
    }),
    page.screenshot({
      fullPage: true,
      scale: 'css',
      style: '#target{background:cyan!important}',
    }),
  ])
  assert.deepEqual(await pixel(successiveFullPageStyles[0], 25, 505), [0, 255, 0, 255])
  assert.deepEqual(await pixel(successiveFullPageStyles[1], 25, 505), [0, 255, 255, 255])
  const styledElement = await target.screenshot({
    scale: 'css',
    style: '#target{width:20px!important;background:lime!important}',
  })
  assert.deepEqual(await dimensions(styledElement), { width: 20, height: 30, format: 'png' })
  assert.deepEqual(await pixel(styledElement, 10, 15), [0, 255, 0, 255])
  const handle = await target.elementHandle()
  assert(handle)
  assert.deepEqual(await dimensions(await handle.screenshot({ scale: 'css' })), {
    width: 40,
    height: 30,
    format: 'png',
  })
  await handle.dispose()
  await page.setContent(
    '<style>html,body{margin:0;background:blue}#target{position:absolute;left:20px;top:10px;width:40px;height:30px;background:red}</style><div id="target"></div>',
  )
  const masked = await page.screenshot({
    scale: 'css',
    mask: [page.locator('#target'), page.locator('#absent')],
    caret: 'initial',
    path: `${directory}/masked.png`,
  })
  assert.deepEqual(await pixel(masked, 25, 15), [255, 0, 255, 255])
  assert.deepEqual(await pixel(masked, 10, 10), [0, 0, 255, 255])
  const maskCustomColor = await page.screenshot({
    scale: 'css',
    mask: [page.locator('#target')],
    maskColor: '#00ff00',
  })
  assert.deepEqual(await pixel(maskCustomColor, 25, 15), [0, 255, 0, 255])
  assert.deepEqual(await pixel(await page.screenshot({ scale: 'css' }), 25, 15), [255, 0, 0, 255])
  await assert.rejects(
    page.screenshot({ mask: [page.locator('#target')], maskColor: 'invalid-color' }),
    /mask/i,
  )
  assert.equal(await page.evaluate(() => document.querySelectorAll('div').length), 1)
  await page.setContent(
    '<style>html,body{margin:0;background:blue}iframe{position:absolute;left:20px;top:20px;border:0;width:80px;height:40px;transform:scale(2);transform-origin:0 0}</style><iframe srcdoc="<style>html,body{margin:0;background:white}#target{position:absolute;left:10px;top:5px;width:20px;height:15px;background:yellow}</style><div id=target></div>"></iframe>',
  )
  const framedTarget = page.frameLocator('iframe').locator('#target')
  const framed = await framedTarget.screenshot({
    scale: 'css',
    path: `${directory}/locator-transformed-frame.png`,
  })
  assert.deepEqual(await dimensions(framed), { width: 40, height: 30, format: 'png' })
  assert.deepEqual(await pixel(framed, 20, 15), [255, 255, 0, 255])
  const framedMask = await page.screenshot({ scale: 'css', mask: [framedTarget] })
  assert.deepEqual(await pixel(framedMask, 50, 40), [255, 0, 255, 255])
  assert.deepEqual(await pixel(await page.screenshot({ scale: 'css' }), 50, 40), [255, 255, 0, 255])
  await page.setContent(
    '<style>html,body{margin:0}#target{width:40px;height:20px;background:red}@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}</style><div id="target"></div>',
  )
  let beforeScreenshotCalls = 0
  const lateAnimation = await page.screenshot({
    scale: 'css',
    animations: 'disabled',
    beforeScreenshot: async () => {
      beforeScreenshotCalls++
      const phase = z
        .object({ artifacts: z.number(), pausedActions: z.number() })
        .parse(await source.runAutomation('diagnostics'))
      assert.equal(phase.artifacts, 1)
      assert.equal(phase.pausedActions, 1)
      await page.locator('#target').evaluate((element) => {
        element.style.animation = 'spin 1s linear infinite'
      })
    },
  })
  assert.equal(beforeScreenshotCalls, 1)
  assert.deepEqual(await pixel(lateAnimation, 20, 10), [255, 0, 0, 255])
  page.setDefaultTimeout(500)
  let hungHookEntered = false
  await assert.rejects(
    page.screenshot({
      beforeScreenshot: () => {
        hungHookEntered = true
        return new Promise(() => {
          // The native deadline must cancel a genuine pending native pause.
        })
      },
    }),
    /Timeout 500ms exceeded/,
  )
  assert.equal(hungHookEntered, true)
  page.setDefaultTimeout(30_000)
  const afterHook = z
    .object({ artifacts: z.number(), pausedActions: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(afterHook.artifacts, 0)
  assert.equal(afterHook.pausedActions, 0)
  await page.setContent(
    '<style>div{caret-color:black!important}</style><div id="editable" contenteditable style="caret-color:#123456!important"></div>',
  )
  const editable = page.locator('#editable')
  await editable.fill('Native caret proof')
  const originalCaret = await editable.evaluate((element) => [
    element.style.getPropertyValue('caret-color'),
    element.style.getPropertyPriority('caret-color'),
  ])
  const noCaret = await editable.screenshot({ scale: 'css' })
  for (let index = 0; index < 5; ++index) {
    await page.waitForTimeout(150)
    assert.deepEqual(await editable.screenshot({ scale: 'css' }), noCaret)
  }
  assert.deepEqual(
    await editable.evaluate((element) => [
      element.style.getPropertyValue('caret-color'),
      element.style.getPropertyPriority('caret-color'),
    ]),
    originalCaret,
  )
  await page.setContent(
    '<style>@page{size:A5}h1{break-before:page}</style><h1>Native first page</h1><h1>Native second page</h1>',
  )
  const pdf = await page.pdf({
    format: 'A4',
    printBackground: true,
    tagged: true,
    outline: true,
    displayHeaderFooter: true,
    headerTemplate: '<div style="font-size:10px">Native header proof</div>',
    footerTemplate:
      '<div style="font-size:10px">Native footer proof <span class="pageNumber"></span></div>',
    margin: { top: '1cm', bottom: '1cm' },
    path: `${directory}/document.pdf`,
  })
  assert.deepEqual(await readFile(`${directory}/document.pdf`), pdf)
  const execute = promisify(execFile)
  const metadata = await execute('pdfinfo', [`${directory}/document.pdf`])
  assert.match(metadata.stdout, /Pages:\s+2\b/)
  assert.match(metadata.stdout, /Tagged:\s+yes/)
  assert.match(metadata.stdout, /Page size:\s+595(?:\.\d+)? x 842(?:\.\d+)? pts/)
  const text = await execute('pdftotext', [`${directory}/document.pdf`, '-'])
  assert.match(text.stdout, /Native first page/)
  assert.match(text.stdout, /Native second page/)
  assert.match(text.stdout, /Native header proof/)
  assert.match(text.stdout, /Native footer proof/)
  await page.pdf({ preferCSSPageSize: true, pageRanges: '2', path: `${directory}/selected.pdf` })
  const selected = await execute('pdfinfo', [`${directory}/selected.pdf`])
  assert.match(selected.stdout, /Pages:\s+1\b/)
  const selectedSize = /Page size:\s+(\d+(?:\.\d+)?) x (\d+(?:\.\d+)?) pts/.exec(selected.stdout)
  assert(selectedSize, 'PDF must report physical page dimensions')
  // Chromium quantizes paged layout to CSS pixels before PDF output. Validate
  // ISO A5 dimensions within one CSS pixel, rather than an integer-only regex.
  assert(Math.abs(Number(selectedSize[1]) - (148 * 72) / 25.4) <= 0.75)
  assert(Math.abs(Number(selectedSize[2]) - (210 * 72) / 25.4) <= 0.75)
  await assert.rejects(page.pdf({ pageRanges: '400-500' }), /page|range/i)
  const state = z
    .object({
      otherOperations: z.number(),
      artifacts: z.number(),
      artifactPhysicalCapture: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(state.otherOperations, 0)
  assert.equal(state.artifacts, 0)
  assert.equal(state.artifactPhysicalCapture, 0)
  for (const path of provenancePaths)
    assert.equal(
      createHash('sha256')
        .update(await readFile(path))
        .digest('hex'),
      provenance[path],
      'Artifact evidence requires unchanged native executable and SDK',
    )
  await writeFile(
    `${directory}/report.json`,
    `${JSON.stringify(
      {
        cefVersion: source.cefVersion,
        provenance,
        viewportCSSAndDevicePixels: 'passed',
        fullPageAndClipPixels: 'passed',
        jpegAndWebP: 'passed',
        concurrentStyleIsolation: 'passed',
        transparencyRestoration: 'passed',
        finiteAndInfiniteAnimationHandling: 'passed',
        openShadowCSSAnimationPixelsAndRestoration: 'passed',
        mediaOverridesAndPartialReset: 'passed',
        realPhysicalCaptureCancellationAndRecovery: 'passed',
        locatorAndElementHandleScreenshotGeometry: 'passed',
        locatorStyleLayoutAndRestoration: 'passed',
        actualMaskPixelsCustomColorAndRestoration: 'passed',
        fullPageMaskOutsideViewportAndPhysicalCancellationCleanup: 'passed',
        successiveFullPageLeaseStyleIsolation: 'passed',
        transformedSameProcessFrameLocatorAndMasks: 'passed',
        nativeBeforeScreenshotPauseLateAnimationAndDeadline: 'passed',
        importantCaretPixelsAndExactInlineRestoration: 'passed',
        pdfPagesAndText: 'passed',
        pdfCSSPageSizeAndRanges: 'passed',
        pdfTemplatesAndTagging: 'passed',
        registries: state,
      },
      null,
      2,
    )}\n`,
  )
  console.log(
    'Native screenshot pixels, formats, scaling, full-page bounds, override cleanup and PDF layout verified',
  )
} catch (error) {
  await writeFile(`${directory}/failure-stderr.txt`, diagnostics)
  await writeFile(
    `${directory}/failure.json`,
    `${JSON.stringify(
      {
        error: String(error),
        cefVersion: source.cefVersion,
        provenance,
      },
      null,
      2,
    )}\n`,
  )
  throw error
} finally {
  await source.close()
}
