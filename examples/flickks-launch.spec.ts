import { expect, test } from 'suitecut/test'

const FLICKKS_URL = 'https://www.flickks.com'

test.use({
  trace: 'off',
  viewport: { width: 1600, height: 900 },
  suitecutCapture: {
    size: { width: 1600, height: 900 },
    framesPerSecond: 60,
    quality: 95,
    narrationTailMs: 320,
  },
})

const narration = {
  provider: 'macos-say' as const,
  voice: 'Samantha',
  speed: 0.96,
}

test.beforeEach(async ({ page }, testInfo) => {
  testInfo.setTimeout(180_000)
  await page.setViewportSize({ width: 1600, height: 900 })
  await page.goto(FLICKKS_URL, { waitUntil: 'domcontentloaded' })
  await expect(page.locator('h1')).toBeVisible()
})

test('Flickks launch film', async ({ page, suitecut }) => {
  test.setTimeout(180_000)
  const hero = page.locator('h1')
  await expect(hero).toBeVisible()
  await suitecut.hold(1_250)
  await suitecut.narrate(
    'Your public website, private client sites, and original file delivery. Flickks brings the whole photography business into one home.',
    narration,
  )

  const exploreTemplates = page.getByRole('button', { name: 'Explore templates' })
  await suitecut.hover(exploreTemplates, { moveDurationMs: 520, settleMs: 420 })
  await Promise.all([
    page.waitForURL(/\/templates\/?$/),
    suitecut.click(exploreTemplates, {
      moveDurationMs: 160,
      animationTimeoutMs: 2_000,
      settleMs: 650,
    }),
  ])

  const templatePrompt = page.getByRole('searchbox', {
    name: 'Describe the photography website style you want',
  })
  await expect(templatePrompt).toBeVisible()
  await suitecut.narrate(
    'Start with a photography-first design. Search by mood, or shape the collection by layout, navigation, media, and density.',
    narration,
  )

  const masonry = page.getByRole('button', { name: 'Masonry' })
  await suitecut.click(masonry, { moveDurationMs: 460, settleMs: 850 })

  const firstTemplate = page.locator('main article').first().getByRole('heading').first()
  await expect(firstTemplate).toBeVisible()
  await suitecut.scrollTo(firstTemplate, { settleMs: 1_050 })
  await suitecut.highlight(firstTemplate, {
    durationMs: 900,
    mode: 'outline',
    paddingPx: 14,
    borderColor: '#111111',
    borderWidthPx: 3,
    borderRadiusPx: 4,
    enter: { type: 'fade-scale', durationMs: 180, easing: 'ease-out' },
    exit: { type: 'fade', durationMs: 160, easing: 'ease-in' },
  })

  const showcaseLink = page
    .getByRole('navigation', { name: 'Main' })
    .getByRole('link', { name: 'Showcase' })
  await Promise.all([
    page.waitForURL(/\/showcase\/?$/),
    suitecut.click(showcaseLink, {
      moveDurationMs: 560,
      animationTimeoutMs: 2_000,
      settleMs: 650,
    }),
  ])
  const showcaseHero = page.getByRole('heading', { name: 'See how the work can live.' })
  await expect(showcaseHero).toBeVisible()
  await suitecut.narrate(
    'Every template is a complete live site, ready for photo and video, and responsive from desktop to mobile.',
    narration,
  )

  const browseMore = page.getByRole('link', { name: /Browse 28 more sites/ })
  await suitecut.click(browseMore, {
    moveDurationMs: 480,
    animationTimeoutMs: 2_000,
    settleMs: 1_000,
  })
  const lightwell = page.getByRole('heading', { name: 'Lightwell' })
  await expect(lightwell).toBeVisible()
  await suitecut.hover(lightwell, { moveDurationMs: 520, settleMs: 900 })

  const homeLink = page
    .getByRole('navigation', { name: 'Main' })
    .getByRole('link', { name: 'Home', exact: true })
  await Promise.all([
    page.waitForURL((url) => url.pathname === '/'),
    suitecut.click(homeLink, {
      moveDurationMs: 620,
      animationTimeoutMs: 2_000,
      settleMs: 700,
    }),
  ])

  const clientSites = page.getByRole('heading', {
    name: 'Give every client a purpose-driven custom site.',
  })
  await suitecut.scrollTo(clientSites, { settleMs: 1_200 })
  await suitecut.narrate(
    'Then give every client a purpose-built destination. Build in private, choose what goes live, and send one polished link.',
    narration,
  )

  const workflow = page.getByRole('heading', {
    name: /Manage each project.*private draft to archive/,
  })
  await suitecut.scrollTo(workflow, { settleMs: 1_150 })
  await suitecut.highlight(workflow, {
    durationMs: 950,
    mode: 'spotlight',
    paddingPx: 18,
    borderColor: '#f4df44',
    borderWidthPx: 3,
    borderRadiusPx: 6,
    backdropColor: '#000000',
    backdropOpacity: 0.2,
    enter: { type: 'fade', durationMs: 180, easing: 'ease-out' },
    exit: { type: 'fade', durationMs: 180, easing: 'ease-in' },
  })

  const originals = page.getByRole('heading', { name: /Full size.*Untouched/ })
  await suitecut.scrollTo(originals, { settleMs: 1_200 })
  await suitecut.narrate(
    'Clients browse the work and download the original files. No account to create. No resized copy standing between them and the final image.',
    narration,
  )

  const studio = page.getByRole('heading', {
    name: /Show clients what you do.*before the first call/,
  })
  await suitecut.scrollTo(studio, { settleMs: 1_250 })
  await suitecut.narrate(
    'From the first impression to the final handoff, Flickks keeps the experience unmistakably yours.',
    narration,
  )

  const finalCta = page.getByRole('heading', {
    name: 'Your website and client delivery belong together.',
  })
  await suitecut.scrollTo(finalCta, { settleMs: 1_300 })
  const buildSite = finalCta
    .locator('xpath=ancestor::section[1]')
    .getByRole('button', { name: 'Build your Flickks site' })
  await suitecut.hover(buildSite, { moveDurationMs: 620, settleMs: 500 })
  await suitecut.narrate('Show it. Share it. Deliver it. All with Flickks.', narration)
  await suitecut.hold(1_600)
})
