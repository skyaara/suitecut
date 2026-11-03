// SPDX-License-Identifier: MIT
// A long agent session across real sites, the check that found every page
// death the local resilience cases in tests/cef now reproduce. Each step works
// like an AI agent: read the page as an AI snapshot, pick an element by ref,
// act on it with real input, and read the result. Read-only: no sign-in,
// checkout, forms with personal data or cookie consent beyond declining.
// Every step runs through the live viewer, which a simulated person takes over
// now and then. It needs the internet, so it is opt-in:
//
//   SUITECUT_NATIVE_EXECUTABLE=/path/to/suitecut-browser npm run soak:real-sites
//
// SOAK_ACTIONS (1200), SOAK_PER_SITE (40), SOAK_SITES (a regex over the site
// URLs) and SOAK_BUDGET_S (5400) shape the run. It writes soak.jsonl and
// soak-summary.json to SOAK_OUTPUT (.suitecut/soak) and exits 1 when a page or
// the browser died.
import { execSync } from 'node:child_process'
import { appendFileSync, mkdirSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import process from 'node:process'

import {
  createBrowserViewer,
  launchBrowser,
  type NativeBrowserController,
  type NativePage,
} from 'suitecut/browser'

// Whatever a page, the viewer or a command hands back, before it is checked.
type UntrustedInput = object | string | number | boolean | bigint | symbol | null | undefined | void

const output = resolve(process.env.SOAK_OUTPUT ?? '.suitecut/soak')
mkdirSync(output, { recursive: true })
const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE ?? ''
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to a built suitecut-browser')
const target = Number(process.env.SOAK_ACTIONS ?? 1200)
const perSite = Number(process.env.SOAK_PER_SITE ?? 40)
const log = join(output, 'soak.jsonl')
writeFileSync(log, '')
const sites: { url: string; query?: string }[] = [
  { url: 'https://en.wikipedia.org/wiki/Bicycle', query: 'tandem bicycle' },
  { url: 'https://news.ycombinator.com/' },
  { url: 'https://developer.mozilla.org/en-US/docs/Web/HTML', query: 'flexbox' },
  { url: 'https://www.ebay.com/', query: 'mechanical keyboard' },
  { url: 'https://github.com/explore' },
  { url: 'https://stackoverflow.com/questions', query: 'javascript promise' },
  { url: 'https://www.bbc.com/news' },
  { url: 'https://www.theguardian.com/international' },
  { url: 'https://www.npr.org/' },
  { url: 'https://www.allrecipes.com/', query: 'banana bread' },
  { url: 'https://www.imdb.com/', query: 'the matrix' },
  { url: 'https://www.python.org/', query: 'asyncio' },
  { url: 'https://nodejs.org/en' },
  { url: 'https://archive.org/', query: 'jazz' },
  { url: 'https://www.ikea.com/us/en/', query: 'desk lamp' },
  { url: 'https://www.etsy.com/', query: 'ceramic mug' },
  { url: 'https://www.bestbuy.com/', query: 'headphones' },
  { url: 'https://www.target.com/', query: 'water bottle' },
  { url: 'https://www.apple.com/' },
  { url: 'https://www.craigslist.org/about/sites' },
  { url: 'https://lobste.rs/' },
  { url: 'https://www.reddit.com/r/programming/' },
  { url: 'https://www.weather.gov/' },
  { url: 'https://www.openstreetmap.org/', query: 'Berlin' },
  { url: 'https://www.goodreads.com/', query: 'dune' },
  { url: 'https://www.nasa.gov/' },
  { url: 'https://www.cnn.com/' },
  { url: 'https://www.walmart.com/', query: 'notebook' },
  { url: 'https://www.zillow.com/' },
  { url: 'https://www.booking.com/' },
]
if (process.env.SOAK_SITES) {
  const only = new RegExp(process.env.SOAK_SITES, 'u')
  sites.splice(0, sites.length, ...sites.filter((site) => only.test(site.url)))
}
// Never act on these: sign-in, purchase, personal data or consent to tracking.
const forbidden =
  /sign ?in|log ?in|sign ?up|register|account|cart|basket|checkout|buy|purchase|order|subscribe|donate|pay|accept|agree|allow all|consent|join|create|download|install|apply|contact|email|password|delete|report|share|print|logout|vote|upvote|flag|hide|reply|comment|submit|post/iu
const decline =
  /reject all|decline|necessary only|only necessary|essential only|refuse|deny|no,? thanks|not now|close|dismiss/iu
interface Ref {
  role: string
  name: string
  ref: string
}
const refs = (snapshot: string): Ref[] =>
  [...snapshot.matchAll(/- (\w+)(?: "((?:[^"\\]|\\.)*)")?[^\n]*\[ref=([^\]]+)\]/gu)].map((m) => ({
    role: m[1] ?? '',
    name: (m[2] ?? '').replace(/\\"/gu, '"'),
    ref: m[3] ?? '',
  }))
const host = (url: string) => {
  try {
    return new URL(url).hostname
      .replace(/^www\./u, '')
      .split('.')
      .slice(-2)
      .join('.')
  } catch {
    return ''
  }
}
const pick = <T>(items: T[]): T | undefined => items[Math.floor(Math.random() * items.length)]
const engineError = (message: string) =>
  /native|renderer|crash|protocol|closed|capacity|exceeds|limit|invalid|unexpected|subscriber|terminated|disconnected/iu.test(
    message,
  ) && !/strict mode violation/iu.test(message)

let browser!: NativeBrowserController,
  page!: NativePage,
  viewer!: Awaited<ReturnType<typeof createBrowserViewer>>,
  link: URL,
  token: string,
  clientId: string
let browserDeaths = 0,
  pageDeaths = 0,
  samples = 0,
  closing = false
let heartbeat: ReturnType<typeof setInterval> | undefined
// Worst event-loop lag in this process since the last read: a blocked loop
// fires command timers before the reply it already received is read.
let lag = 0,
  tick = performance.now()
setInterval(() => {
  const now = performance.now()
  lag = Math.max(lag, now - tick - 100)
  tick = now
}, 100).unref()
const takeLag = () => {
  const value = Math.round(lag)
  lag = 0
  return value
}
async function start() {
  browser = await launchBrowser({
    backend: 'cef',
    executablePath,
    headless: true,
    pixelFormat: 'bgra',
  })
  const first = browser.defaultContext().pages()[0]
  if (!first) throw new Error('The browser opened without a page')
  page = first
  viewer = await createBrowserViewer({ browser, page, network: true, resolutionMode: 'auto' })
  link = new URL(viewer.url)
  token = decodeURIComponent(link.hash.slice(1))
  clientId = ((await viewerCall('/session', {})) as { clientId: string }).clientId
  // A connected viewer page heartbeats; without it the client session expires.
  clearInterval(heartbeat)
  heartbeat = setInterval(() => {
    void viewerCall('/state')
      .then((state) =>
        viewerCall('/action', {
          clientId,
          revision: (state as { revision: number }).revision,
          type: 'heartbeat',
        }),
      )
      .catch(() => undefined)
  }, 5000)
  page.on('crash', () => issue(host(page.url()), 'engine', 'page crash event'))
  page.on('close', () => {
    const page_ = String(
      (page.source.pageSignal?.reason as Error | undefined)?.message ?? 'unknown',
    )
    const process_ = browser.source.processSignal?.aborted
      ? String((browser.source.processSignal.reason as Error)?.message)
      : 'running'
    if (closing) return
    ++pageDeaths
    issue(host(page.url()), 'engine', `agent page closed: ${page_} (process ${process_})`)
    record({
      site: host(page.url()),
      name: 'page-closed',
      error: page_.slice(0, 600),
      process: process_.slice(0, 300),
    })
  })
  page.on('dialog', (dialog) => void dialog.dismiss().catch(() => undefined))
}
class BrowserDied extends Error {}
const viewerCall = async (path: string, body?: object) => {
  const response = await fetch(new URL(path, link.origin), {
    method: body ? 'POST' : 'GET',
    headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  })
  const json = (await response.json()) as Record<string, UntrustedInput>
  if (!response.ok)
    throw new Error(`viewer ${path} ${response.status}: ${JSON.stringify(json.error)}`)
  return json
}
const human = async (type: string) =>
  viewerCall('/action', { clientId, revision: (await viewerCall('/state')).revision, type })

let actions = 0,
  snapshots = 0,
  nextHandover = 30
const counts: Record<string, number> = {}
const issues: Record<string, { count: number; sites: Set<string>; sample: string }> = {}
const latencies: Record<string, number[]> = {}
const t0 = Date.now()
const record = (entry: Record<string, UntrustedInput>) =>
  appendFileSync(log, JSON.stringify({ t: ((Date.now() - t0) / 1000).toFixed(1), ...entry }) + '\n')
const issue = (site: string, kind: string, message: string) => {
  const key = `${kind}: ${message
    .replace(/\s+/gu, ' ')
    .replace(/r[0-9a-f]{32}_\d+_\d+/gu, '<ref>')
    .replace(/https?:\S+/gu, '<url>')
    .slice(0, 160)}`
  const entry = (issues[key] ??= { count: 0, sites: new Set(), sample: message.slice(0, 600) })
  ++entry.count
  entry.sites.add(site)
}
await start()
// tsx outlives run.sh's alarm, so the soak keeps its own budget.
setTimeout(
  () => {
    writeFileSync(join(output, 'soak-summary.json'), JSON.stringify(summary(), null, 2))
    console.log('SOAK BUDGET EXCEEDED', actions, 'actions')
    process.exit(2)
  },
  Number(process.env.SOAK_BUDGET_S ?? 5400) * 1000,
).unref()

const step = async (
  site: string,
  name: string,
  run: () => Promise<UntrustedInput>,
  timeout = 20000,
) => {
  const started = Date.now()
  try {
    await viewer.runAgent(() =>
      Promise.race([
        run(),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error(`step ${name} hung > ${timeout} ms`)), timeout),
        ),
      ]),
    )
    ;(latencies[name] ??= []).push(Date.now() - started)
    counts[name] = (counts[name] ?? 0) + 1
    if (name !== 'snapshot') ++actions
    record({
      site,
      name,
      ms: Date.now() - started,
      url: page.url().slice(0, 120),
      loopLagMs: takeLag(),
    })
    return true
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    // When the host stops answering at all, keep a stack sample of it.
    if (message.includes('browser command timed out') && samples < 3) {
      ++samples
      try {
        const pid = execSync(`pgrep -f 'MacOS/suitecut-browser --suitecut' | head -1`)
          .toString()
          .trim()
        execSync(
          `sample ${pid} 2 -file ${join(output, `hang-${samples}.txt`)} >/dev/null 2>&1 || true`,
        )
        record({ site, name: 'host-sampled', file: `hang-${samples}.txt` })
      } catch {
        // Sampling is best effort.
      }
    }
    // Only actions a live browser actually ran count toward the target.
    if (page.isClosed() || !browser.isConnected()) {
      issue(site, 'engine', `browser or agent page died: ${name}: ${message}`)
      throw new BrowserDied(message)
    }
    if (name !== 'snapshot') ++actions
    issue(site, engineError(message) ? 'engine' : 'site', `${name}: ${message}`)
    record({
      site,
      name,
      ms: Date.now() - started,
      error: message.slice(0, 400),
      loopLagMs: takeLag(),
    })
    if (page.source.state === 'failed') {
      await viewer.runAgent(() => page.reload({ timeout: 20000 })).catch(() => undefined)
      issue(site, 'engine', 'recovered crashed page with reload')
    }
    return false
  }
}

async function visit(site: { url: string; query?: string }) {
  const name = host(site.url)
  let searched = false
  if (
    !(await step(
      name,
      'goto',
      () => page.goto(site.url, { waitUntil: 'domcontentloaded', timeout: 30000 }),
      35000,
    ))
  )
    return
  for (let i = 0; i < perSite && actions < target; ++i) {
    let snapshot = ''
    const ok = await step(name, 'snapshot', async () => {
      snapshot = await page.ariaSnapshot({ mode: 'ai', timeout: 10000 })
      ++snapshots
    })
    if (!ok) {
      await step(name, 'scroll', () => page.mouse.wheel(0, 600))
      continue
    }
    const all = refs(snapshot)
    if (!all.length) issue(name, 'site', 'empty AI snapshot')
    const dismiss = all.find(
      (r) => r.role === 'button' && decline.test(r.name) && !forbidden.test(r.name),
    )
    if (dismiss && i < 3) {
      await step(name, 'click', () => page.getByRef(dismiss.ref).click({ timeout: 5000 }))
      continue
    }
    const field = all.find(
      (r) =>
        ['searchbox', 'combobox', 'textbox'].includes(r.role) &&
        /search|find|query|look/iu.test(r.name),
    )
    const links = all.filter(
      (r) => r.role === 'link' && r.name.length > 2 && !forbidden.test(r.name),
    )
    const buttons = all.filter(
      (r) =>
        ['button', 'tab', 'menuitem'].includes(r.role) &&
        r.name.length > 1 &&
        !forbidden.test(r.name),
    )
    const roll = Math.random()
    if (field && site.query && !searched && roll < 0.6) {
      searched = true
      await step(name, 'type', async () => {
        const box = page.getByRef(field.ref)
        await box.click({ timeout: 5000 })
        await box.fill('', { timeout: 5000 })
        await page.keyboard.type(site.query ?? '', { delay: 15 })
        await page.keyboard.press('Enter')
        await page.waitForLoadState('domcontentloaded', { timeout: 15000 }).catch(() => undefined)
      })
    } else if (roll < 0.45 && links.length) {
      const choice = pick(links)
      if (!choice) continue
      await step(name, 'click', async () => {
        const target = page.getByRef(choice.ref)
        const href = await target.getAttribute('href', { timeout: 3000 }).catch(() => null)
        if (href && host(new URL(href, page.url()).href) !== host(site.url)) {
          await target.hover({ timeout: 5000 })
          return
        }
        await target.click({ timeout: 5000 })
        await page.waitForLoadState('domcontentloaded', { timeout: 15000 }).catch(() => undefined)
      })
    } else if (roll < 0.6 && buttons.length) {
      const choice = pick(buttons)
      if (!choice) continue
      await step(name, 'click', () => page.getByRef(choice.ref).click({ timeout: 5000 }))
      await step(name, 'press', () => page.keyboard.press('Escape'))
    } else if (roll < 0.75) {
      await step(name, 'scroll', () => page.mouse.wheel(0, 400 + Math.random() * 1200))
    } else if (roll < 0.83 && links.length) {
      const choice = pick(links)
      if (!choice) continue
      await step(name, 'hover', () => page.getByRef(choice.ref).hover({ timeout: 5000 }))
    } else if (roll < 0.9) {
      await step(name, 'back', () => page.goBack({ waitUntil: 'domcontentloaded', timeout: 15000 }))
    } else if (roll < 0.95) {
      await step(name, 'read', () =>
        Promise.all([page.title(), page.evaluate(() => document.body?.innerText.length ?? 0)]),
      )
    } else {
      await step(name, 'screenshot', () => page.screenshot({ timeout: 10000 }))
    }
    if (host(page.url()) !== host(site.url) && page.url() !== 'about:blank')
      await step(
        name,
        'goto',
        () => page.goto(site.url, { waitUntil: 'domcontentloaded', timeout: 30000 }),
        35000,
      )
    // A person takes over now and then, scrolls, and hands back.
    if (actions >= nextHandover) {
      nextHandover += 60
      const started = Date.now()
      try {
        await human('takeover')
        await viewerCall('/action', {
          clientId,
          revision: (await viewerCall('/state')).revision,
          type: 'wheel',
          deltaX: 0,
          deltaY: 500,
        })
        await human('release')
        record({ site: name, name: 'handover', ms: Date.now() - started })
        counts.handover = (counts.handover ?? 0) + 1
      } catch (error) {
        issue(name, 'engine', `handover: ${error instanceof Error ? error.message : String(error)}`)
        await human('release').catch(() => undefined)
        // What kept the engine busy: every nonzero pending counter.
        const busy = await viewer
          .runAgent(() => page.source.runAutomation('diagnostics', { timeoutMs: 5000 }))
          .then((value) =>
            Object.entries(value as Record<string, UntrustedInput>).filter(
              ([key, v]) =>
                /pending|queued|physical|active|inflight|waiters|work|retr|submitted|callbacks/iu.test(
                  key,
                ) &&
                typeof v === 'number' &&
                v > 0,
            ),
          )
          .catch((e: Error) => e.message)
        record({ site: name, name: 'handover-busy', busy })
      }
    }
  }
  // Anomalies the page survived that used to close it.
  const degraded = await viewer
    .runAgent(() => page.source.runAutomation('diagnostics', { timeoutMs: 5000 }))
    .then((value) => {
      const result = value as {
        degraded?: Record<string, number>
        engineDegraded?: Record<string, number>
      }
      return { ...result.degraded, ...result.engineDegraded }
    })
    .catch(() => ({}))
  if (Object.keys(degraded).length) record({ site: name, name: 'degraded', degraded })
}

const memory = () => process.memoryUsage().rss
const summary = () => {
  const p = (values: number[], q: number) =>
    [...values].sort((a, b) => a - b)[Math.floor((values.length - 1) * q)] ?? 0
  return {
    minutes: ((Date.now() - t0) / 60000).toFixed(1),
    actions,
    snapshots,
    counts,
    latency: Object.fromEntries(
      Object.entries(latencies).map(([k, v]) => [
        k,
        { p50: p(v, 0.5), p95: p(v, 0.95), max: p(v, 1) },
      ]),
    ),
    hostRSSMB: Math.round(memory() / 1048576),
    browserDeaths,
    pageDeaths,
    issues: Object.entries(issues)
      .sort((a, b) => b[1].count - a[1].count)
      .map(([key, value]) => ({
        key,
        count: value.count,
        sites: [...value.sites].join(','),
        sample: value.sample,
      })),
  }
}
try {
  for (let round = 0; actions < target; ++round)
    for (const site of sites) {
      if (actions >= target) break
      try {
        await visit(site)
      } catch (error) {
        if (!(error instanceof BrowserDied)) throw error
        ++browserDeaths
        record({ site: host(site.url), name: 'browser-died', error: error.message.slice(0, 400) })
        closing = true
        await viewer.close().catch(() => undefined)
        await browser.close().catch(() => undefined)
        closing = false
        await start()
      }
      console.log(
        `${((Date.now() - t0) / 60000).toFixed(1)} min: ${actions} actions, ${snapshots} snapshots, ${Object.keys(issues).length} issue kinds after ${host(site.url)}`,
      )
      writeFileSync(join(output, 'soak-summary.json'), JSON.stringify(summary(), null, 2))
    }
} finally {
  writeFileSync(join(output, 'soak-summary.json'), JSON.stringify(summary(), null, 2))
  console.log(
    'SOAK DONE',
    actions,
    'actions,',
    pageDeaths,
    'page deaths,',
    browserDeaths,
    'browser deaths',
  )
  clearInterval(heartbeat)
  closing = true
  await viewer.close().catch(() => undefined)
  await browser.close()
}
if (pageDeaths || browserDeaths) process.exitCode = 1
