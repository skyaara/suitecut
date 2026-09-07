import { mkdir } from 'node:fs/promises'

import { defineSuiteCut } from 'suitecut'
import { renderSuiteCut } from 'suitecut/render'

const manifestPath = '.suitecut/wikipedia-typewriter-4k.json'
const recordingDirectory = '.suitecut/wikipedia-typewriter-4k'
const videoPath = '.suitecut/videos/wikipedia-typewriter-4k.mp4'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: {
    colorScheme: 'light',
    locale: 'en-US',
    reducedMotion: 'no-preference',
  },
  capture: {
    viewport: { width: 3840, height: 2160 },
    size: { width: 3840, height: 2160 },
    framesPerSecond: 60,
    quality: 95,
    narrationTailMs: 300,
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
  speed: 1.02,
}

await mkdir('.suitecut/videos', { recursive: true })

const result = await record('Wikipedia typewriter API tour in 4K', async ({ page, suitecut }) => {
  page.setDefaultTimeout(30_000)
  page.setDefaultNavigationTimeout(45_000)

  await page.goto('https://www.wikipedia.org/', { waitUntil: 'domcontentloaded' })
  await page.evaluate(() => {
    document.documentElement.style.zoom = '1.65'
  })
  suitecut.selectPage(page)

  await suitecut.narrate(
    'This is a native 4K SuiteCut recording of a real public website.',
    narration,
  )

  const search = page.getByRole('searchbox', { name: 'Search Wikipedia' })
  await search.waitFor({ state: 'visible' })
  await suitecut.highlight(search, {
    mode: 'spotlight',
    label: 'SuiteCut typewriter input',
    borderColor: '#3366cc',
    fillColor: '#3366cc',
    fillOpacity: 0.08,
    backdropOpacity: 0.22,
    durationMs: 1_150,
  })
  await suitecut.click(search, { moveDurationMs: 520, settleMs: 120 })
  await suitecut.type(search, 'Playwright (software)', {
    delayMs: 240,
    settleMs: 450,
  })
  await suitecut.checkpoint('Wikipedia search typed')

  const searchButton = page.getByRole('button', { name: 'Search', exact: true })
  await suitecut.hover(searchButton, { moveDurationMs: 480, settleMs: 220 })
  await suitecut.narrate(
    'The type API sends real keyboard events, one character at a time, then leaves the field ready to submit.',
    narration,
  )

  await Promise.all([
    page.waitForURL(/en\.wikipedia\.org\/wiki\/Playwright_\(software\)/u),
    suitecut.click(searchButton, {
      moveDurationMs: 220,
      settleMs: 500,
      animationTimeoutMs: 2_000,
    }),
  ])

  await page.evaluate(() => {
    document.documentElement.style.zoom = '1.65'
  })
  suitecut.selectPage(page)
  const articleHeading = page.getByRole('heading', {
    level: 1,
    name: 'Playwright (software)',
  })
  await articleHeading.waitFor({ state: 'visible' })
  await suitecut.zoom(articleHeading, {
    scale: 1.2,
    paddingPx: 36,
    holdMs: 1_050,
  })
  await suitecut.narrate(
    'Click, zoom, narration, highlights, and checkpoints stay on the same recorded timeline.',
    narration,
  )

  const usageHeading = page.getByRole('heading', {
    name: 'Usage and examples',
    exact: true,
  })
  await suitecut.scrollTo(usageHeading, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 500,
  })
  await suitecut.highlight(usageHeading, {
    mode: 'outline',
    label: 'Native browser scrolling',
    borderColor: '#202122',
    borderWidthPx: 4,
    paddingPx: 18,
    durationMs: 1_100,
  })

  const exampleLink = page.getByRole('link', { name: 'https://example.com', exact: true }).first()
  await suitecut.hover(exampleLink, { moveDurationMs: 560, settleMs: 300 })
  await suitecut.checkpoint('Wikipedia usage section')
  await suitecut.hold(900)

  await suitecut.scrollTop({ behavior: 'smooth', settleMs: 500 })
  await suitecut.narrate(
    'The flow ends at the top of the article, with every SuiteCut recording control exercised on the live page.',
    narration,
  )
  await suitecut.checkpoint('Wikipedia article complete')
  await suitecut.hold(1_100)
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
    backgroundColor: '#ffffff',
    failureMode: 'strict',
  },
})

console.log(`Manifest: ${result.manifestPath}`)
console.log(`Video: ${videoPath}`)
console.log(`Duration: ${String(report.presentationDurationMs)} ms`)
