import { mkdir } from 'node:fs/promises'

import { type Locator } from 'playwright'

import { defineSuiteCut } from 'suitecut'
import { renderSuiteCut } from 'suitecut/render'

const manifestPath = '.suitecut/mdn-interactions-4k.json'
const recordingDirectory = '.suitecut/mdn-interactions-4k'
const videoPath = '.suitecut/videos/mdn-interactions-4k.mp4'
const documentUrl = 'https://developer.mozilla.org/en-US/docs/Web/API/Document'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: {
    colorScheme: 'dark',
    locale: 'en-US',
    reducedMotion: 'no-preference',
  },
  capture: {
    viewport: { width: 3840, height: 2160 },
    size: { width: 3840, height: 2160 },
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

const result = await record(
  'Fast MDN interaction tour in native 4K',
  async ({ page, suitecut }) => {
    page.setDefaultTimeout(30_000)
    page.setDefaultNavigationTimeout(60_000)

    const preparePage = (): void => {
      suitecut.selectPage(page)
    }

    const fastZoom = async (locator: Locator, scale = 1.25, holdMs = 180): Promise<void> => {
      await suitecut.zoom(locator, {
        scale,
        paddingPx: 28,
        holdMs,
        enter: { type: 'scale', durationMs: 120, easing: 'ease-out' },
        exit: { type: 'scale', durationMs: 100, easing: 'ease-in-out' },
      })
    }

    const fastHighlight = async (
      locator: Locator,
      label: string,
      borderColor = '#8CB4FF',
    ): Promise<void> => {
      await suitecut.highlight(locator, {
        mode: 'outline',
        label,
        borderColor,
        borderWidthPx: 3,
        paddingPx: 10,
        durationMs: 420,
        enter: { type: 'scale', durationMs: 80, easing: 'ease-out' },
        exit: { type: 'scale', durationMs: 70, easing: 'ease-in' },
      })
    }

    const cdp = await page.context().newCDPSession(page)
    await cdp.send('Emulation.setDeviceMetricsOverride', {
      width: 2560,
      height: 1440,
      deviceScaleFactor: 1.5,
      mobile: false,
      screenWidth: 2560,
      screenHeight: 1440,
    })

    await page.goto(documentUrl, { waitUntil: 'domcontentloaded' })
    await page.waitForTimeout(1_500)
    preparePage()

    await suitecut.narrate("A fast SuiteCut pass through MDN's live Document reference.", narration)

    const documentHeading = page.getByRole('heading', { level: 1, name: 'Document' })
    await documentHeading.waitFor({ state: 'visible' })
    await fastHighlight(documentHeading, 'Live MDN API reference')
    await fastZoom(documentHeading)

    const baseline = page.locator('.baseline-indicator')
    await fastZoom(baseline, 1.2, 140)
    await fastHighlight(baseline, 'Cross-browser baseline', '#4ADE80')

    const themeButton = page.getByRole('button', { name: 'Switch color theme' })
    await suitecut.hover(themeButton, { moveDurationMs: 150, settleMs: 60 })
    await suitecut.click(themeButton, { moveDurationMs: 80, settleMs: 80 })
    const lightTheme = page.getByRole('button', { name: 'Light', exact: true })
    await fastHighlight(lightTheme, 'Light theme')
    await suitecut.click(lightTheme, { moveDurationMs: 90, settleMs: 180 })
    await suitecut.click(themeButton, { moveDurationMs: 100, settleMs: 70 })
    await suitecut.click(page.getByRole('button', { name: 'Dark', exact: true }), {
      moveDurationMs: 90,
      settleMs: 180,
    })

    await page.keyboard.press('/')
    const siteSearch = page.locator('input[aria-label="Search"]')
    await siteSearch.waitFor({ state: 'visible' })
    await fastHighlight(siteSearch, 'MDN site search')
    await suitecut.click(siteSearch, { moveDurationMs: 120, settleMs: 40 })
    await suitecut.type(siteSearch, 'View Transition API', { delayMs: 55, settleMs: 350 })
    await fastZoom(siteSearch, 1.18, 160)
    await suitecut.checkpoint('MDN search query typed')
    await page.keyboard.press('Escape')
    await page.keyboard.press('Escape')
    await siteSearch.waitFor({ state: 'hidden' })

    const filter = page.locator('input[placeholder="Filter"]')
    await suitecut.scrollTop({ behavior: 'smooth', settleMs: 260 })
    await fastHighlight(filter, 'Filter the API index')
    await suitecut.click(filter, { moveDurationMs: 140, settleMs: 40 })
    await suitecut.type(filter, 'fullscreen', { delayMs: 72, settleMs: 260 })

    const fullscreenProperty = page
      .getByRole('link', { name: 'Document.fullscreenElement', exact: true })
      .first()
    await fullscreenProperty.waitFor({ state: 'visible' })
    await suitecut.scrollTo(fullscreenProperty, {
      behavior: 'smooth',
      block: 'center',
      settleMs: 320,
    })
    await fastZoom(fullscreenProperty)
    await fastHighlight(fullscreenProperty, 'Filtered property result', '#FBBF24')
    await suitecut.narrate(
      'Typewriter input filters a long API index while every camera move returns to the full frame.',
      narration,
    )
    await filter.fill('')
    await page.waitForTimeout(180)

    const methodsHeading = page
      .getByRole('heading', { name: 'Instance methods', exact: true })
      .first()
    await suitecut.scrollTo(methodsHeading, {
      behavior: 'smooth',
      block: 'center',
      settleMs: 320,
    })
    await fastZoom(methodsHeading)
    await fastHighlight(methodsHeading, 'Large-page anchor navigation')

    const querySelectorLink = page
      .getByRole('link', { name: 'Document.querySelector()', exact: true })
      .first()
    await suitecut.scrollTo(querySelectorLink, {
      behavior: 'smooth',
      block: 'center',
      settleMs: 260,
    })
    await suitecut.hover(querySelectorLink, { moveDurationMs: 150, settleMs: 70 })
    await fastZoom(querySelectorLink, 1.25, 140)
    await Promise.all([
      page.waitForURL(/\/Document\/querySelector\/?$/u, { waitUntil: 'domcontentloaded' }),
      suitecut.click(querySelectorLink, {
        moveDurationMs: 100,
        settleMs: 220,
        animationTimeoutMs: 1_200,
      }),
    ])
    await page.waitForTimeout(500)
    preparePage()

    const querySelectorHeading = page.getByRole('heading', {
      level: 1,
      name: /Document: querySelector\(\) method/u,
    })
    await querySelectorHeading.waitFor({ state: 'visible' })
    await fastHighlight(querySelectorHeading, 'Real page navigation', '#C084FC')
    await fastZoom(querySelectorHeading)

    const syntaxHeading = page.getByRole('heading', { name: 'Syntax', exact: true })
    await suitecut.scrollTo(syntaxHeading, {
      behavior: 'smooth',
      block: 'center',
      settleMs: 260,
    })
    await fastZoom(page.locator('pre').first(), 1.22, 170)
    await suitecut.checkpoint('MDN querySelector syntax')

    await page.goto(documentUrl, { waitUntil: 'domcontentloaded' })
    await page.waitForTimeout(700)
    preparePage()

    const compatibilityHeading = page.getByRole('heading', {
      name: 'Browser compatibility',
      exact: true,
    })
    await suitecut.scrollTo(compatibilityHeading, {
      behavior: 'smooth',
      block: 'start',
      settleMs: 420,
    })
    await fastZoom(compatibilityHeading, 1.23, 160)
    await fastHighlight(compatibilityHeading, 'Compatibility data')

    const chromeSupport = page.locator('mdn-compat-table button').first()
    await chromeSupport.waitFor({ state: 'visible' })
    await suitecut.scrollTo(chromeSupport, {
      behavior: 'smooth',
      block: 'center',
      settleMs: 260,
    })
    await suitecut.hover(chromeSupport, { moveDurationMs: 140, settleMs: 60 })
    await fastZoom(chromeSupport, 1.25, 130)
    await suitecut.click(chromeSupport, { moveDurationMs: 90, settleMs: 220 })
    await fastHighlight(chromeSupport, 'Interactive support details', '#4ADE80')

    await suitecut.narrate(
      'Rapid zooms, returns, highlights, scrolling, typing, and navigation stay on one native 4K timeline.',
      narration,
    )
    await suitecut.checkpoint('MDN compatibility interaction complete')
    await suitecut.scrollTop({ behavior: 'smooth', settleMs: 320 })
    await fastZoom(page.getByRole('link', { name: 'MDN', exact: true }).first(), 1.18, 130)
    await suitecut.hold(500)
  },
)

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
