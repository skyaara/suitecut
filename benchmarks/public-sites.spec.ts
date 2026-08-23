import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

import { type Locator, type Page } from '@playwright/test'

import { expect, test } from 'suitecut'

type SiteName = 'flickks' | 'llmgames' | 'spek'

const site = readSite(process.env.BENCHMARK_SITE)
const moduleStartedNs = process.hrtime.bigint()

function readSite(value: string | undefined): SiteName {
  if (value === 'flickks' || value === 'llmgames' || value === 'spek') return value
  throw new Error('BENCHMARK_SITE must be flickks, llmgames, or spek')
}

function elapsedMs(startedNs: bigint): number {
  return Number(process.hrtime.bigint() - startedNs) / 1_000_000
}

async function preparePage(page: Page): Promise<void> {
  await page.waitForLoadState('domcontentloaded')
  await page.evaluate(async () => {
    await document.fonts.ready
    const images = Array.from(document.images).filter((image) => {
      const rect = image.getBoundingClientRect()
      return rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.top < innerHeight
    })
    await Promise.all(images.map((image) => image.decode().catch(() => undefined)))
    await new Promise<void>((done) =>
      requestAnimationFrame(() => requestAnimationFrame(() => done())),
    )
  })
}

async function getNavigation(
  page: Page,
  name: string,
): Promise<Record<string, number | string> | undefined> {
  return page.evaluate((label) => {
    const entry = performance.getEntriesByType('navigation')[0] as
      PerformanceNavigationTiming | undefined
    if (entry === undefined) return undefined
    return {
      decodedBodySize: entry.decodedBodySize,
      domContentLoadedMs: entry.domContentLoadedEventEnd,
      loadMs: entry.loadEventEnd,
      name: label,
      responseEndMs: entry.responseEnd,
      responseStartMs: entry.responseStart,
      transferSize: entry.transferSize,
    }
  }, name)
}

test.use({
  trace: 'off',
  viewport: { width: 1600, height: 900 },
  suitecutCapture: {
    viewport: { width: 1600, height: 900 },
    size: { width: 1600, height: 900 },
    framesPerSecond: 60,
    quality: 90,
    narrationTailMs: 250,
  },
})

test(site, async ({ page, suitecut }) => {
  const steps: { durationMs: number; name: string; startedAt: string; url: string }[] = []
  const navigations: Record<string, number | string>[] = []
  const flowStartedNs = process.hrtime.bigint()

  const measure = async <T>(name: string, action: () => Promise<T>): Promise<T> => {
    const startedAt = new Date().toISOString()
    const startedNs = process.hrtime.bigint()
    try {
      return await action()
    } finally {
      steps.push({ durationMs: elapsedMs(startedNs), name, startedAt, url: page.url() })
    }
  }

  const narrate = (name: string, text: string, caption: string): Promise<void> =>
    measure(name, () =>
      suitecut.narrate(text, { provider: 'kokoro', voice: 'af_heart', speed: 1.05, caption }),
    )

  const visit = async (name: string, url: string): Promise<void> => {
    await measure(name, async () => {
      await page.goto(url, { waitUntil: 'domcontentloaded' })
      await preparePage(page)
    })
    const timing = await getNavigation(page, name)
    if (timing !== undefined) navigations.push(timing)
  }

  const scroll = (name: string, locator: Locator): Promise<void> =>
    measure(name, () =>
      suitecut.scrollTo(locator, { behavior: 'smooth', block: 'center', settleMs: 700 }),
    )

  const navigate = async (name: string, locator: Locator, url: RegExp): Promise<void> => {
    await measure(name, async () => {
      await Promise.all([
        page.waitForURL(url),
        suitecut.click(locator, { moveDurationMs: 420, animationTimeoutMs: 2_000, settleMs: 450 }),
      ])
      await preparePage(page)
    })
    const timing = await getNavigation(page, name)
    if (timing !== undefined) navigations.push(timing)
  }

  const highlight = (name: string, locator: Locator, color: string): Promise<void> =>
    measure(name, () =>
      suitecut.highlight(locator, { borderColor: color, borderWidthPx: 4, durationMs: 850 }),
    )

  if (site === 'flickks') {
    await visit('navigate.home', 'https://www.flickks.com/')
    const hero = page.getByRole('heading', { level: 1, name: /One home for your business/ })
    await measure('assert.hero', () => expect(hero).toBeVisible())
    await narrate(
      'narrate.hero',
      'Flickks gives photographers one public website, a branded site for every client, and original file delivery in the same product.',
      'One website. Branded client sites. Original delivery.',
    )
    await highlight('highlight.hero', hero, '#ff6b45')
    await measure('checkpoint.hero', () => suitecut.checkpoint('Flickks home'))
    const templates = page.getByRole('heading', { level: 2, name: /Choose the look/ })
    await scroll('scroll.templates', templates)
    await narrate(
      'narrate.templates',
      "Templates change the visual system without moving the photographer's pages, albums, or navigation.",
      'Change the look. Keep the pages and albums.',
    )
    await highlight('highlight.templates', templates, '#3a68ff')
    const showcase = page.getByRole('link', { name: 'Showcase', exact: true }).first()
    await navigate('navigate.showcase', showcase, /\/showcase\/?$/)
    const destination = page.getByRole('heading', { level: 1, name: 'See how the work can live.' })
    await measure('assert.showcase', () => expect(destination).toBeVisible())
    await narrate(
      'narrate.showcase',
      'The showcase opens complete live photography sites, so a visitor can inspect real pages, albums, media, and responsive navigation.',
      'Complete live sites, ready to inspect.',
    )
    await measure('checkpoint.showcase', () => suitecut.checkpoint('Flickks showcase'))
  } else if (site === 'llmgames') {
    await visit('navigate.home', 'https://www.llmgames.online/')
    const hero = page.getByRole('heading', {
      level: 1,
      name: 'Multiplayer games for you and your agents.',
    })
    await measure('assert.hero', () => expect(hero).toBeVisible())
    await narrate(
      'narrate.hero',
      'LLM Games puts humans and external AI agents at the same server-authoritative table. The server owns rules, turns, timers, randomness, and shared state.',
      'Humans and agents share one authoritative game room.',
    )
    await highlight('highlight.hero', hero, '#f0c75e')
    await measure('checkpoint.hero', () => suitecut.checkpoint('LLM Games home'))
    const connect = page.getByRole('heading', { level: 2, name: 'Connect your agent' })
    await scroll('scroll.connect-agent', connect)
    await narrate(
      'narrate.connect-agent',
      'An agent connects through the remote MCP endpoint and OAuth. The game never needs the model provider API key.',
      'Remote MCP and OAuth. No model API key.',
    )
    await highlight(
      'highlight.command',
      page.locator('code').filter({ hasText: 'codex mcp add llmgames' }),
      '#62d6a8',
    )
    const library = page.getByRole('heading', { level: 2, name: 'Pick a table.' })
    await scroll('scroll.game-library', library)
    await narrate(
      'narrate.game-library',
      'The same room system runs group card tables and head-to-head games. Human and agent seats use the same legal actions.',
      'One room system across every game.',
    )
    await highlight('highlight.game-library', library, '#8aa4ff')
    const setup = page
      .getByRole('navigation', { name: 'Main navigation' })
      .getByRole('link', { name: 'Connect AI' })
    await navigate('navigate.setup', setup, /\/setup\/?$/)
    await measure('assert.setup', () => expect(page.locator('h1')).toBeVisible())
    await narrate(
      'narrate.setup',
      'The setup page gives each supported agent host a copyable command and the authentication steps needed to claim a seat.',
      'Copy the command, authenticate, and claim a seat.',
    )
    await measure('checkpoint.setup', () => suitecut.checkpoint('LLM Games setup'))
  } else {
    await visit('navigate.home', 'https://spek.aakashreddy.com/')
    const hero = page.getByRole('heading', { level: 1, name: /Let the words.*take the room/ })
    await measure('assert.hero', () => expect(hero).toBeVisible())
    await narrate(
      'narrate.hero',
      'Spek turns documents and pasted text into a focused, sentence-by-sentence listening experience in the browser.',
      'A focused reader for documents and pasted text.',
    )
    await highlight('highlight.hero', hero, '#d5ff70')
    await measure('checkpoint.hero', () => suitecut.checkpoint('Spek home'))
    const cleanup = page.getByRole('heading', {
      level: 2,
      name: 'It keeps what belongs in the voice.',
    })
    await scroll('scroll.cleanup', cleanup)
    await narrate(
      'narrate.cleanup',
      'Before playback, Spek finds the real beginning, removes repeated page clutter, and pauses when a visual needs the reader.',
      'Find the beginning. Remove clutter. Pause for visuals.',
    )
    await highlight('highlight.cleanup', cleanup, '#9d8cff')
    await navigate(
      'navigate.features',
      page.getByRole('link', { name: 'Features', exact: true }).first(),
      /\/features\/?$/,
    )
    await measure('assert.features', () => expect(page.locator('h1')).toBeVisible())
    await narrate(
      'narrate.features',
      'The feature guide explains extraction, local speech, visible sentence tracking, and controls that stay out of the way until needed.',
      'Extraction, local speech, and sentence tracking.',
    )
    await measure('checkpoint.features', () => suitecut.checkpoint('Spek features'))
    await navigate(
      'navigate.reader',
      page.getByRole('link', { name: 'Open Spek', exact: true }).first(),
      /\/read(?:\?.*)?$/,
    )
    await measure('assert.reader', () => expect(page.locator('main')).toBeVisible())
    await narrate(
      'narrate.reader',
      'The reader accepts a file or pasted text. Documents and speech stay on the device, with no account or library to maintain.',
      'Files and speech stay on this device.',
    )
    await measure('checkpoint.reader', () => suitecut.checkpoint('Spek reader'))
  }

  await measure('hold.final', () => suitecut.hold(900))
  const resultPath = resolve('benchmarks', 'results', 'steps', `suitecut-${site}.json`)
  await mkdir(resolve(resultPath, '..'), { recursive: true })
  await writeFile(
    resultPath,
    `${JSON.stringify(
      {
        flowDurationMs: elapsedMs(flowStartedNs),
        moduleToFlowStartMs: elapsedMs(moduleStartedNs) - elapsedMs(flowStartedNs),
        navigations,
        site,
        steps,
        version: 'suitecut',
      },
      null,
      2,
    )}\n`,
    'utf8',
  )
})
