import { expect, test } from '../../dist/test.js'

test.use({
  suitecutCapture: {
    viewport: { width: 1920, height: 1080 },
    size: { width: 1920, height: 1080 },
    framesPerSecond: 60,
    quality: 95,
    narrationTailMs: 180,
  },
})

const narration = {
  provider: 'kokoro' as const,
  voice: 'af_heart',
  speed: 1.08,
}

test('explains the SuiteCut workflow on the docs site', async ({ page, suitecut }) => {
  test.setTimeout(1_800_000)

  const expectHdComposition = async (): Promise<void> => {
    await expect
      .poll(() =>
        page.evaluate(() => ({
          scale: getComputedStyle(document.documentElement).zoom,
          width: document.querySelector('.site-header')?.getBoundingClientRect().width,
        })),
      )
      .toEqual({ scale: '1', width: 960 })
  }

  const explain = async (text: string): Promise<void> => {
    await suitecut.narrate(text, narration)
  }

  const guide = async (target: ReturnType<typeof page.locator>, label: string): Promise<void> => {
    await suitecut.highlight(target, {
      mode: 'outline',
      geometry: 'content',
      label,
      borderColor: '#f5f5f5',
      borderWidthPx: 3,
      borderRadiusPx: 8,
      paddingPx: 10,
      durationMs: 760,
      enter: { type: 'fade', durationMs: 120, easing: 'ease-out' },
      exit: { type: 'fade', durationMs: 120, easing: 'ease-in' },
    })
  }

  const focus = async (
    target: ReturnType<typeof page.locator>,
    scale = 1.2,
    holdMs = 780,
  ): Promise<void> => {
    await suitecut.zoom(target, {
      scale,
      geometry: 'content',
      paddingPx: 36,
      holdMs,
      enter: { type: 'scale', durationMs: 220, easing: 'ease-out' },
      exit: { type: 'scale', durationMs: 180, easing: 'ease-in-out' },
    })
  }

  await page.goto('./', { waitUntil: 'domcontentloaded' })
  await expectHdComposition()
  suitecut.selectPage(page)

  const homeIntroduction = page.locator('.home-reference').first()
  await explain(
    'SuiteCut records a real Playwright flow, then renders the saved run into a finished video.',
  )
  await guide(homeIntroduction.locator('h1'), 'SuiteCut API reference')
  await focus(homeIntroduction.locator('h1'), 1.16, 640)

  const docsLink = page.getByRole('link', { name: 'Docs', exact: true })
  await guide(docsLink, 'Start with the docs')
  await suitecut.click(docsLink, { moveDurationMs: 260, settleMs: 240 })
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Getting started')
  await expectHdComposition()

  const gettingStartedIntro = page.locator('.docs-article > p').first()
  await explain(
    'Start with the Playwright recorder. Add the test fixture when the same flow also needs assertions, retries, and workers.',
  )
  await guide(gettingStartedIntro, 'Record first, render separately')
  await focus(gettingStartedIntro, 1.18, 720)

  const apiNav = page.getByRole('link', { name: 'API and CLI', exact: true })
  await guide(apiNav, 'Open the runtime controls')
  await suitecut.click(apiNav, { moveDurationMs: 240, settleMs: 260 })
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('API and CLI')
  await expectHdComposition()

  const fixtureMethods = page.getByRole('heading', { name: 'Fixture methods', exact: true })
  await suitecut.scrollTo(fixtureMethods, { behavior: 'smooth', block: 'center', settleMs: 280 })
  await explain(
    'The fixture adds narration, highlights, zooms, pointer actions, typewriter input, scrolling, holds, and checkpoints to normal Playwright.',
  )
  await guide(fixtureMethods, 'One fixture, complete recording controls')

  const narrateMethod = page.getByRole('heading', { name: /narrate\(text/u })
  await suitecut.scrollTo(narrateMethod, { behavior: 'smooth', block: 'center', settleMs: 260 })
  await explain(
    'Narrate creates local speech, displays a caption, and measures the audio so the timeline stays synchronized.',
  )
  await focus(narrateMethod, 1.22, 820)

  const highlightMethod = page.getByRole('heading', { name: /highlight\(locator/u })
  await suitecut.scrollTo(highlightMethod, { behavior: 'smooth', block: 'center', settleMs: 260 })
  await explain(
    'Highlight marks the exact element being discussed. Zoom frames that same element, then returns to the page context.',
  )
  await guide(highlightMethod, 'Direct attention')
  await focus(highlightMethod, 1.23, 820)

  const zoomMethod = page.getByRole('heading', { name: /zoom\(locator/u })
  await suitecut.scrollTo(zoomMethod, { behavior: 'smooth', block: 'center', settleMs: 220 })
  await focus(zoomMethod, 1.23, 720)

  const typeMethod = page.getByRole('heading', { name: /type\(locator/u })
  await suitecut.scrollTo(typeMethod, { behavior: 'smooth', block: 'center', settleMs: 260 })
  await explain(
    'Hover, click, scroll, and type perform real browser actions. The recording shows the application responding, character by character.',
  )
  await guide(typeMethod, 'Real typewriter input')
  await focus(typeMethod, 1.23, 820)

  const checkpointMethod = page.getByRole('heading', { name: /checkpoint\(label/u })
  await suitecut.scrollTo(checkpointMethod, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 280,
  })
  await explain(
    'Checkpoint saves a named video clip from the page screencast, while the manifest keeps the timing, geometry, media, and test identity together.',
  )
  await guide(checkpointMethod, 'Reviewable checkpoints')
  await focus(checkpointMethod, 1.2, 760)

  const rendererNav = page.getByRole('link', { name: 'Renderer', exact: true })
  await guide(rendererNav, 'Render the saved run')
  await suitecut.click(rendererNav, { moveDurationMs: 250, settleMs: 260 })
  const rendererHeading = page.getByRole('heading', { name: 'Renderer', exact: true })
  await expect(rendererHeading).toBeVisible()
  await suitecut.scrollTo(rendererHeading, {
    behavior: 'smooth',
    block: 'center',
    settleMs: 260,
  })
  await explain(
    'Rendering stays separate. One saved recording can produce another size or quality without rerunning the browser flow.',
  )
  await focus(rendererHeading, 1.2, 760)

  const kokoroNav = page.getByRole('link', { name: 'Kokoro speech', exact: true })
  await guide(kokoroNav, 'Local speech')
  await suitecut.click(kokoroNav, { moveDurationMs: 250, settleMs: 260 })
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Kokoro speech')
  await expectHdComposition()

  const kokoroIntro = page.locator('.docs-article > p').first()
  await explain('Kokoro speech runs locally, with no hosted API and no key.')
  await guide(kokoroIntro, 'Local by default')
  await focus(kokoroIntro, 1.2, 760)

  await explain(
    'That is the SuiteCut loop: write a Playwright flow, record it once, and render a clear product walkthrough.',
  )
  await suitecut.checkpoint('SuiteCut feature tour complete')
  await suitecut.hold(520)
})
