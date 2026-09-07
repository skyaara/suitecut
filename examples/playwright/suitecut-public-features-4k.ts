import { mkdir } from 'node:fs/promises'

import { type Locator } from 'playwright'

import { defineSuiteCut } from 'suitecut'
import { renderSuiteCut } from 'suitecut/render'

const manifestPath = '.suitecut/suitecut-public-features-4k.json'
const recordingDirectory = '.suitecut/suitecut-public-features-4k'
const videoPath = '.suitecut/videos/suitecut-public-features-4k.mp4'
const docsUrl = 'https://suitecut.aakashreddy.com/docs'
const mdnUrl = 'https://developer.mozilla.org/en-US/docs/Web/API/Document'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: {
    colorScheme: 'dark',
    locale: 'en-US',
    reducedMotion: 'no-preference',
  },
  capture: {
    viewport: { width: 1920, height: 1080 },
    size: { width: 1920, height: 1080 },
    framesPerSecond: 60,
    quality: 95,
    narrationTailMs: 180,
  },
  output: {
    directory: recordingDirectory,
    manifestPath,
    pathKind: 'manifest-relative',
  },
})

const narration = {
  provider: 'kokoro' as const,
  voice: 'af_heart',
  speed: 1.08,
}

await mkdir('.suitecut/videos', { recursive: true })

const result = await record('SuiteCut public feature tour in 4K', async ({ page, suitecut }) => {
  page.setDefaultTimeout(30_000)
  page.setDefaultNavigationTimeout(60_000)

  const narrate = async (text: string): Promise<void> => {
    await suitecut.narrate(text, narration)
  }

  const guide = async (locator: Locator, label: string, borderColor = '#8CB4FF'): Promise<void> => {
    await suitecut.highlight(locator, {
      mode: 'outline',
      geometry: 'content',
      label,
      borderColor,
      borderWidthPx: 3,
      borderRadiusPx: 10,
      paddingPx: 12,
      durationMs: 720,
      enter: { type: 'fade-scale', durationMs: 120, easing: 'ease-out' },
      exit: { type: 'fade', durationMs: 100, easing: 'ease-in' },
    })
  }

  const focus = async (locator: Locator, scale = 1.2, holdMs = 620): Promise<void> => {
    await suitecut.zoom(locator, {
      scale,
      geometry: 'content',
      paddingPx: 36,
      holdMs,
      enter: { type: 'scale', durationMs: 180, easing: 'ease-out' },
      exit: { type: 'scale', durationMs: 150, easing: 'ease-in-out' },
    })
  }

  await page.goto(docsUrl, { waitUntil: 'commit' })
  const gettingStarted = page.getByRole('heading', { level: 1, name: 'Getting started' })
  await gettingStarted.waitFor({ state: 'visible' })
  suitecut.selectPage(page)

  await narrate(
    'SuiteCut records an ordinary Playwright flow first, then renders the saved run into a finished video.',
  )
  await guide(gettingStarted, 'Live SuiteCut documentation')
  await focus(gettingStarted, 1.16, 520)

  const workflow = page.locator('.docs-article > p').first()
  await guide(workflow, 'Record first, render separately', '#4ADE80')
  await focus(workflow, 1.17, 560)

  const renderSize = page.getByRole('heading', {
    name: 'Render size and frame rate',
    exact: true,
  })
  await suitecut.scrollTo(renderSize, { behavior: 'smooth', block: 'center', settleMs: 320 })
  await narrate(
    'This pass records a nineteen-twenty by ten-eighty browser at sixty frames per second, then renders a four-K delivery file.',
  )
  await guide(renderSize, 'HD source, 4K delivery')
  await focus(renderSize, 1.2, 620)

  const apiLink = page.getByRole('link', { name: 'API and CLI', exact: true }).first()
  await suitecut.scrollTop({ behavior: 'smooth', settleMs: 320 })
  await guide(apiLink, 'Open the API reference')
  await Promise.all([
    page.waitForURL(/\/docs\/api\/?$/u, { waitUntil: 'commit' }),
    suitecut.click(apiLink, { moveDurationMs: 220, settleMs: 260 }),
  ])
  const apiHeading = page.getByRole('heading', { level: 1, name: 'API and CLI' })
  await apiHeading.waitFor({ state: 'visible' })
  suitecut.selectPage(page)

  const fixtureMethods = page.getByRole('heading', { name: 'Fixture methods', exact: true })
  await suitecut.scrollTo(fixtureMethods, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 300,
  })
  await narrate(
    'The fixture controls narration, captions, highlights, zoom, pointer movement, typing, scrolling, holds, and review checkpoints.',
  )
  await guide(fixtureMethods, 'One fixture controls the recording')

  const typeMethod = page.getByRole('heading', { name: /type\(locator/u })
  await suitecut.scrollTo(typeMethod, { behavior: 'smooth', block: 'center', settleMs: 260 })
  await guide(typeMethod, 'Real keyboard events', '#FBBF24')
  await focus(typeMethod, 1.23, 620)
  await suitecut.checkpoint('SuiteCut fixture API')

  await page.goto(mdnUrl, { waitUntil: 'commit' })
  const documentHeading = page.getByRole('heading', { level: 1, name: 'Document' }).first()
  await documentHeading.waitFor({ state: 'visible' })
  suitecut.selectPage(page)

  await narrate('Now the same controls are running against MDN, a real public website.')
  await guide(documentHeading, 'Live MDN Document reference')
  await focus(documentHeading, 1.18, 520)

  const baseline = page.locator('.baseline-indicator')
  await guide(baseline, 'Cross-browser baseline', '#4ADE80')
  await focus(baseline, 1.18, 420)

  const themeButton = page.getByRole('button', { name: 'Switch color theme' })
  await suitecut.hover(themeButton, { moveDurationMs: 180, settleMs: 80 })
  await suitecut.click(themeButton, { moveDurationMs: 100, settleMs: 100 })
  const lightTheme = page.getByRole('button', { name: 'Light', exact: true })
  await guide(lightTheme, 'Click a live control')
  await suitecut.click(lightTheme, { moveDurationMs: 110, settleMs: 220 })
  await suitecut.click(themeButton, { moveDurationMs: 120, settleMs: 80 })
  await suitecut.click(page.getByRole('button', { name: 'Dark', exact: true }), {
    moveDurationMs: 100,
    settleMs: 220,
  })

  await page.keyboard.press('/')
  const siteSearch = page.locator('input[aria-label="Search"]')
  await siteSearch.waitFor({ state: 'visible' })
  await guide(siteSearch, 'Typewriter input')
  await suitecut.click(siteSearch, { moveDurationMs: 140, settleMs: 60 })
  await suitecut.type(siteSearch, 'View Transition API', { delayMs: 62, settleMs: 360 })
  await focus(siteSearch, 1.18, 460)
  await suitecut.checkpoint('MDN search query typed')
  await page.keyboard.press('Escape')
  await page.keyboard.press('Escape')
  await siteSearch.waitFor({ state: 'hidden' })

  const filter = page.locator('input[placeholder="Filter"]')
  await suitecut.scrollTo(filter, { behavior: 'smooth', block: 'center', settleMs: 320 })
  await guide(filter, 'Filter a long API index')
  await suitecut.click(filter, { moveDurationMs: 140, settleMs: 60 })
  await suitecut.type(filter, 'fullscreen', { delayMs: 72, settleMs: 300 })

  const fullscreenProperty = page
    .getByRole('link', { name: 'Document.fullscreenElement', exact: true })
    .first()
  await fullscreenProperty.waitFor({ state: 'visible' })
  await suitecut.scrollTo(fullscreenProperty, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 300,
  })
  await guide(fullscreenProperty, 'Filtered result', '#FBBF24')
  await focus(fullscreenProperty, 1.22, 520)
  await narrate(
    'SuiteCut sends real browser actions. The page responds while the cursor, camera, caption, and audio stay on one timeline.',
  )

  await page.goto(mdnUrl, { waitUntil: 'domcontentloaded' })
  await page
    .getByRole('heading', { level: 1, name: 'Document' })
    .filter({ visible: true })
    .first()
    .waitFor({ state: 'visible' })
  suitecut.selectPage(page)

  const methodsHeading = page.getByRole('heading', { name: 'Instance methods', exact: true })
  await suitecut.scrollTo(methodsHeading, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 320,
  })
  await guide(methodsHeading, 'Native smooth scrolling')

  const querySelectorLink = page
    .getByRole('link', { name: 'Document.querySelector()', exact: true })
    .first()
  await suitecut.scrollTo(querySelectorLink, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 280,
  })
  await suitecut.hover(querySelectorLink, { moveDurationMs: 170, settleMs: 90 })
  await focus(querySelectorLink, 1.22, 420)
  await guide(querySelectorLink, 'Pointer movement on a live link', '#C084FC')
  await suitecut.checkpoint('MDN instance methods')

  const compatibilityHeading = page.getByRole('heading', {
    name: 'Browser compatibility',
    exact: true,
  })
  await suitecut.scrollTo(compatibilityHeading, {
    behavior: 'smooth',
    block: 'start',
    settleMs: 380,
  })
  await guide(compatibilityHeading, 'Compatibility data')
  await focus(compatibilityHeading, 1.2, 480)

  const supportControl = page.locator('mdn-compat-table button').first()
  await supportControl.waitFor({ state: 'visible' })
  await suitecut.scrollTo(supportControl, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 280,
  })
  await suitecut.hover(supportControl, { moveDurationMs: 160, settleMs: 80 })
  await suitecut.click(supportControl, { moveDurationMs: 100, settleMs: 240 })
  await guide(supportControl, 'Interactive support details', '#4ADE80')

  await narrate(
    'The browser run is saved as a validated manifest. The renderer can then produce another size or quality without repeating the flow.',
  )
  await suitecut.checkpoint('SuiteCut public feature tour complete')
  await suitecut.scrollTop({ behavior: 'smooth', settleMs: 340 })
  await suitecut.hold(720)
})

const report = await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: videoPath,
  selection: { testId: result.testId, retry: 0 },
  config: {
    output: {
      container: 'mp4',
      videoCodec: 'libx264',
      audioCodec: 'aac',
      pixelFormat: 'yuv420p',
      colorRange: 'auto',
      width: 3840,
      height: 2160,
      framesPerSecond: 60,
      quality: 'high',
    },
    narrationEnabled: true,
    resultHoldMs: 0,
    backgroundColor: '#111827',
    failureMode: 'strict',
  },
})

console.log(`Manifest: ${result.manifestPath}`)
console.log(`Video: ${videoPath}`)
console.log(`Duration: ${String(report.presentationDurationMs)} ms`)
