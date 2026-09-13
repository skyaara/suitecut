import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

import sharpFactory from 'sharp'

import { expect, test } from 'suitecut/test'

import { createRecordingSession } from '../src/suitecut.js'

test.use({
  suitecutCapture: {
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 2,
    quality: 100,
    framesPerSecond: 30,
  },
})

test('rejects a device scale that was not set when the context was created', async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 1,
  })
  const page = await context.newPage()
  await expect(
    createRecordingSession({
      captureOptions: { deviceScaleFactor: 2 },
      output: {
        pathFor: (name) => test.info().outputPath(name),
        attach: () => Promise.resolve(),
      },
      page,
    }),
  ).rejects.toThrow(/must be applied when the Playwright browser context is created/u)
  await context.close()
})

test('supersamples without changing page layout', async ({ context, page, suitecut }, testInfo) => {
  test.setTimeout(180_000)
  const artifactDirectory = resolve('.suitecut/device-scale-artifacts')
  await mkdir(artifactDirectory, { recursive: true })
  await page.setContent(`
    <style>
      * { box-sizing: border-box; }
      html, body { margin: 0; }
      body { color: #172033; background: #f8fafc; font-family: system-ui, sans-serif; }
      #viewport { width: 100vw; height: 100vh; border: 1px solid #172033; display: grid; grid-template-rows: 72px 1fr 64px; }
      header, footer { position: relative; display: flex; align-items: center; padding: 0 40px; background: #ffffff; }
      header { position: fixed; inset: 0 0 auto; height: 72px; border-bottom: 1px solid #64748b; z-index: 1; }
      main { grid-row: 2; display: grid; place-items: center; }
      footer { grid-row: 3; border-top: 1px solid #64748b; }
      .fine { max-width: 760px; font: 300 17px/1.45 Georgia, serif; letter-spacing: .015em; }
      button { position: absolute; left: 1420px; top: 460px; width: 220px; height: 72px; border: 1px solid #4338ca; background: #eef2ff; color: #312e81; }
      #breakpoint { color: rgb(16 120 72); }
      @media (min-width: 2500px) { #breakpoint { color: rgb(220 38 38); } }
    </style>
    <div id="viewport">
      <header>Fixed header</header>
      <main>
        <section class="fine">
          <svg width="96" height="96" viewBox="0 0 96 96" aria-label="Vector artwork">
            <circle cx="48" cy="48" r="38.5" fill="none" stroke="#4338ca" stroke-width="1" />
            <path d="M22 50L40 68L75 29" fill="none" stroke="#0f766e" stroke-width="1" />
          </svg>
          <p id="breakpoint">Thin vector text and one-pixel rules stay on the 1920px breakpoint.</p>
        </section>
        <button type="button">Inspect alignment</button>
      </main>
      <footer>Fixed footer</footer>
    </div>
  `)

  const state = await page.evaluate(() => ({
    devicePixelRatio: window.devicePixelRatio,
    innerHeight: window.innerHeight,
    innerWidth: window.innerWidth,
    rootStyleTransform: document.documentElement.style.transform,
    rootStyleZoom: document.documentElement.style.zoom,
  }))
  expect(state).toEqual({
    devicePixelRatio: 2,
    innerHeight: 1080,
    innerWidth: 1920,
    rootStyleTransform: '',
    rootStyleZoom: '',
  })
  await expect(page.locator('#breakpoint')).toHaveCSS('color', 'rgb(16, 120, 72)')
  await expect(page.locator('#viewport')).toHaveCSS('width', '1920px')
  await expect(page.locator('#viewport')).toHaveCSS('height', '1080px')

  const viewportBox = await page.locator('#viewport').boundingBox()
  const headerBox = await page.locator('header').boundingBox()
  const footerBox = await page.locator('footer').boundingBox()
  expect(viewportBox).toEqual({ x: 0, y: 0, width: 1920, height: 1080 })
  expect(headerBox?.y).toBe(0)
  expect(footerBox && footerBox.y + footerBox.height).toBeGreaterThanOrEqual(1079)
  expect(footerBox && footerBox.y + footerBox.height).toBeLessThanOrEqual(1080)

  const physical = await page.screenshot({ animations: 'allow', caret: 'initial', scale: 'device' })
  expect(await sharpFactory(physical).metadata()).toMatchObject({ width: 3840, height: 2160 })
  const oneX = await page.screenshot({ animations: 'allow', caret: 'initial', scale: 'css' })
  const downscaled = await sharpFactory(physical)
    .resize(1920, 1080, { fit: 'fill', kernel: 'lanczos3' })
    .png()
    .toBuffer()
  const physicalPath = resolve(artifactDirectory, 'device-scale-physical-2x.png')
  const oneXPath = resolve(artifactDirectory, 'device-scale-native-1x.png')
  const downscaledPath = resolve(artifactDirectory, 'device-scale-2x-downscaled.png')
  await Promise.all([
    writeFile(physicalPath, physical),
    writeFile(oneXPath, oneX),
    writeFile(downscaledPath, downscaled),
  ])
  await testInfo.attach('device-scale-physical-2x.png', {
    path: physicalPath,
    contentType: 'image/png',
  })
  await testInfo.attach('device-scale-native-1x.png', {
    path: oneXPath,
    contentType: 'image/png',
  })
  await testInfo.attach('device-scale-2x-downscaled.png', {
    path: downscaledPath,
    contentType: 'image/png',
  })
  const [oneXRaw, downscaledRaw] = await Promise.all([
    sharpFactory(oneX).removeAlpha().raw().toBuffer(),
    sharpFactory(downscaled).removeAlpha().raw().toBuffer(),
  ])
  let absoluteDifference = 0
  for (let index = 0; index < oneXRaw.length; index += 1) {
    absoluteDifference += Math.abs((oneXRaw[index] ?? 0) - (downscaledRaw[index] ?? 0))
  }
  const meanAbsoluteDifference = absoluteDifference / oneXRaw.length
  expect(meanAbsoluteDifference).toBeGreaterThan(0)
  const comparisonPath = resolve(artifactDirectory, 'device-scale-comparison.json')
  await writeFile(comparisonPath, JSON.stringify({ meanAbsoluteDifference }, undefined, 2))
  await testInfo.attach('device-scale-comparison.json', {
    path: comparisonPath,
    contentType: 'application/json',
  })

  const button = page.getByRole('button', { name: 'Inspect alignment' })
  const buttonBox = await button.boundingBox()
  expect(buttonBox).not.toBeNull()
  const highlight = suitecut.highlight(button, {
    durationMs: 600,
    enter: { type: 'none' },
    exit: { type: 'none' },
  })
  const highlightLayer = page.locator('[data-suitecut-highlight]')
  await highlightLayer.waitFor({ state: 'visible' })
  expect(await highlightLayer.boundingBox()).toEqual({
    x: (buttonBox?.x ?? 0) - 8,
    y: (buttonBox?.y ?? 0) - 8,
    width: (buttonBox?.width ?? 0) + 16,
    height: (buttonBox?.height ?? 0) + 16,
  })
  await highlight
  await suitecut.hover(button, { moveDurationMs: 0, settleMs: 0, waitForAnimations: false })
  const cursorPoint = await page.locator('[data-suitecut-cursor]').evaluate((cursor) => ({
    x: Number((cursor as HTMLElement).dataset.x),
    y: Number((cursor as HTMLElement).dataset.y),
  }))
  expect(cursorPoint).toEqual({
    x: (buttonBox?.x ?? 0) + (buttonBox?.width ?? 0) / 2,
    y: (buttonBox?.y ?? 0) + (buttonBox?.height ?? 0) / 2,
  })

  const secondary = await context.newPage()
  await expect.poll(() => secondary.evaluate(() => window.innerWidth)).toBe(1920)
  expect(await secondary.evaluate(() => window.devicePixelRatio)).toBe(2)
  await secondary.close()

  const [popup] = await Promise.all([
    page.waitForEvent('popup'),
    page.evaluate(() => window.open('about:blank')),
  ])
  await expect.poll(() => popup.evaluate(() => window.innerWidth)).toBe(1920)
  expect(await popup.evaluate(() => window.devicePixelRatio)).toBe(2)
  await popup.close()
  await suitecut.hold(250)
})
