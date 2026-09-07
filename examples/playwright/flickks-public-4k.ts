import { mkdir } from 'node:fs/promises'

import { defineSuiteCut } from 'suitecut'
import { renderSuiteCut } from 'suitecut/render'

const manifestPath = '.suitecut/flickks-public-4k.json'
const recordingDirectory = '.suitecut/flickks-public-4k'
const videoPath = '.suitecut/videos/flickks-public-4k.mp4'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: {
    colorScheme: 'light',
    locale: 'en-US',
    reducedMotion: 'no-preference',
  },
  capture: {
    viewport: { width: 1920, height: 1080 },
    size: { width: 1920, height: 1080 },
    framesPerSecond: 60,
    quality: 100,
    narrationTailMs: 320,
  },
  output: {
    directory: recordingDirectory,
    manifestPath,
    pathKind: 'manifest-relative',
  },
})

const narration = {
  provider: 'macos-say' as const,
  voice: 'Samantha',
  speed: 0.98,
}

await mkdir('.suitecut/videos', { recursive: true })

const result = await record('Flickks public website tour in 4K', async ({ page, suitecut }) => {
  page.setDefaultTimeout(30_000)
  page.setDefaultNavigationTimeout(45_000)

  const open = async (url: string, ready: ReturnType<typeof page.locator>) => {
    await page.goto(url, { waitUntil: 'commit', timeout: 60_000 })
    suitecut.selectPage(page)
    await ready.waitFor({ state: 'visible' })
    await suitecut.hold(600)
  }

  await open(
    'https://www.flickks.com/',
    page.getByRole('heading', {
      level: 1,
      name: /One home for your business/,
    }),
  )

  await suitecut.narrate(
    'This is the full public Flickks website, recorded live in a nineteen twenty by ten eighty browser viewport and delivered in four K.',
    narration,
  )

  const hero = page.getByRole('heading', {
    level: 1,
    name: /One home for your business/,
  })
  await suitecut.zoom(hero, { scale: 1.08, paddingPx: 72, holdMs: 900 })

  const clientSites = page.getByRole('heading', {
    name: 'Give every client a purpose-driven custom site.',
  })
  await suitecut.scrollTo(clientSites, { settleMs: 900 })
  await suitecut.narrate(
    'The home page connects the public portfolio, private client sites, full-resolution delivery, roles, support, and plans in one product story.',
    narration,
  )
  await suitecut.highlight(clientSites, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 18,
    durationMs: 900,
  })

  const delivery = page.getByRole('heading', { name: /Full size.*Untouched/ })
  await suitecut.scrollTo(delivery, { settleMs: 850 })
  await suitecut.highlight(delivery, {
    mode: 'spotlight',
    borderColor: '#f4df44',
    borderWidthPx: 3,
    paddingPx: 20,
    backdropOpacity: 0.18,
    durationMs: 950,
  })
  await suitecut.checkpoint('Flickks home page')

  await open(
    'https://www.flickks.com/templates',
    page.getByRole('searchbox', {
      name: 'Describe the photography website style you want',
    }),
  )
  await suitecut.narrate(
    'Templates opens the full photography-first catalog. Search by style, then filter by structure, layout, navigation, media, density, and motion.',
    narration,
  )

  const search = page.getByRole('searchbox', {
    name: 'Describe the photography website style you want',
  })
  await suitecut.click(search, { moveDurationMs: 480, settleMs: 120 })
  await suitecut.type(search, 'editorial portfolio', {
    delayMs: 95,
    settleMs: 650,
  })
  await suitecut.highlight(search, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 10,
    durationMs: 800,
  })
  await search.fill('')

  const masonry = page.getByRole('button', { name: /^Masonry/ })
  await suitecut.click(masonry, { moveDurationMs: 420, settleMs: 750 })
  const templateHeading = page.locator('main article').first().getByRole('heading').first()
  await suitecut.scrollTo(templateHeading, { settleMs: 900 })
  await suitecut.zoom(templateHeading, {
    scale: 1.12,
    paddingPx: 64,
    holdMs: 900,
  })
  await suitecut.checkpoint('Flickks templates page')

  await open(
    'https://www.flickks.com/showcase',
    page.getByRole('heading', { level: 1, name: 'See every plan in practice.' }),
  )
  await suitecut.narrate(
    'Showcase compares complete live-site examples across Free, Individual, Pro, and Studio, so each plan can be judged in context.',
    narration,
  )
  const plans = page.locator('#plans')
  await suitecut.scrollTo(plans, { settleMs: 900 })
  const studioCard = page.locator('#plans button').filter({ hasText: 'Studio' }).first()
  await suitecut.hover(studioCard, { moveDurationMs: 520, settleMs: 450 })
  await suitecut.highlight(studioCard, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 12,
    durationMs: 850,
  })
  await suitecut.checkpoint('Flickks showcase page')

  await open(
    'https://www.flickks.com/pricing',
    page.getByRole('heading', { level: 1, name: 'Flickks pricing plans' }),
  )
  await suitecut.narrate(
    'Pricing keeps the free plan beside the paid options, with storage, album, client-site, collaboration, and domain limits spelled out.',
    narration,
  )
  const comparePlans = page.getByRole('heading', { name: 'Compare every plan.' })
  await suitecut.scrollTo(comparePlans, { settleMs: 900 })
  await suitecut.highlight(comparePlans, {
    mode: 'spotlight',
    borderColor: '#f4df44',
    borderWidthPx: 3,
    paddingPx: 18,
    backdropOpacity: 0.16,
    durationMs: 900,
  })
  const pricingFaq = page.getByRole('heading', {
    name: 'Website, album, and delivery FAQs',
  })
  await suitecut.scrollTo(pricingFaq, { settleMs: 850 })
  await suitecut.checkpoint('Flickks pricing page')

  await open(
    'https://www.flickks.com/about',
    page.getByRole('heading', { name: /Three rules.*One standard of care/ }),
  )
  await suitecut.narrate(
    'About is deliberately personal. The work stays first, every handoff should feel human, and uploaded originals remain protected.',
    narration,
  )
  const principles = page.getByRole('heading', {
    name: /Three rules.*One standard of care/,
  })
  await suitecut.zoom(principles, { scale: 1.12, paddingPx: 72, holdMs: 850 })
  const human = page.getByRole('heading', {
    name: 'There is a human on the other side.',
  })
  await suitecut.scrollTo(human, { settleMs: 900 })
  await suitecut.highlight(human, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 18,
    durationMs: 900,
  })
  await suitecut.checkpoint('Flickks about page')

  await open(
    'https://www.flickks.com/contact',
    page.getByRole('heading', { level: 1, name: /Tell us what.*you need/ }),
  )
  await suitecut.narrate(
    'Contact gives visitors a direct route to the team and prepares a normal email without forcing a support account.',
    narration,
  )
  const prepareEmail = page.getByRole('button', { name: 'Prepare email' })
  await suitecut.hover(prepareEmail, { moveDurationMs: 520, settleMs: 450 })
  await suitecut.highlight(prepareEmail, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 12,
    durationMs: 850,
  })
  await suitecut.checkpoint('Flickks contact page')

  await open(
    'https://www.flickks.com/privacy',
    page.getByRole('heading', { level: 1, name: 'Privacy Policy' }),
  )
  await suitecut.narrate(
    'The public legal pages are part of the tour too. Privacy explains collection, sharing, retention, security, rights, and contact details.',
    narration,
  )
  const privacySecurity = page.getByRole('heading', { name: 'Security', exact: true })
  await suitecut.scrollTo(privacySecurity, { settleMs: 950 })
  await suitecut.highlight(privacySecurity, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 16,
    durationMs: 800,
  })
  await suitecut.checkpoint('Flickks privacy page')

  await open(
    'https://www.flickks.com/terms',
    page.getByRole('heading', { level: 1, name: 'Terms of Service' }),
  )
  await suitecut.narrate(
    'Terms covers accounts, publishing, passwords, storage, billing, acceptable use, ownership, availability, and termination.',
    narration,
  )
  const acceptableUse = page.getByRole('heading', { name: 'Acceptable use', exact: true })
  await suitecut.scrollTo(acceptableUse, { settleMs: 950 })
  await suitecut.highlight(acceptableUse, {
    mode: 'outline',
    borderColor: '#111111',
    borderWidthPx: 3,
    paddingPx: 16,
    durationMs: 800,
  })
  await suitecut.checkpoint('Flickks terms page')

  await open(
    'https://blog.flickks.com/',
    page.getByRole('heading', {
      level: 1,
      name: 'Useful notes for working photographers.',
    }),
  )
  await suitecut.narrate(
    'The public tour ends at the Flickks blog, with field notes on portfolios, delivery, templates, community, and the engineering behind the product.',
    narration,
  )
  const journal = page.getByRole('heading', { name: 'More from Flickks.' })
  await suitecut.scrollTo(journal, { settleMs: 950 })
  const engineering = page.getByRole('link', {
    name: 'From upload to unlock, how Flickks runs on Cloudflare',
    exact: true,
  })
  await suitecut.scrollTo(engineering, { settleMs: 900 })
  await suitecut.highlight(engineering, {
    mode: 'spotlight',
    borderColor: '#f4df44',
    borderWidthPx: 3,
    paddingPx: 16,
    backdropOpacity: 0.16,
    durationMs: 950,
  })
  await suitecut.checkpoint('Flickks blog')
  await suitecut.scrollTop({ settleMs: 900 })
  await suitecut.narrate(
    'Nine public destinations, one continuous Flickks story. Show the work, share it with clients, and deliver the originals.',
    narration,
  )
  await suitecut.hold(1_400)
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
