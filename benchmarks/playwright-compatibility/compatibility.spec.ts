import { execFileSync } from 'node:child_process'
import { readFile, writeFile, stat } from 'node:fs/promises'
import { createServer, type Server, type IncomingMessage, type ServerResponse } from 'node:http'
import { createServer as createHttpsServer } from 'node:https'
import { connect } from 'node:net'
import { setTimeout as delay } from 'node:timers/promises'

import { test as base, expect, chromium, type Page } from '@playwright/test'
import * as z from 'zod'

import { launchEngine, type SuiteCutEngine } from '../../src/engine.js'

declare global {
  interface Window {
    suitecutWorker: Worker
  }
}
function required<T>(value: T | null | undefined): T {
  if (value == null) throw new Error('Required test value is missing')
  return value
}
const HarSchema = z.object({
  log: z.object({
    entries: z.array(
      z.object({
        response: z.object({
          status: z.number(),
          content: z.object({ text: z.string().optional() }),
        }),
      }),
    ),
  }),
})
const ProbeSchema = z.object({
  streams: z.array(z.object({ codec_name: z.string(), width: z.number(), height: z.number() })),
  format: z.object({ duration: z.string() }),
})

const test = base.extend<object, { engine: SuiteCutEngine | undefined }>({
  engine: [
    async ({ browserName }, use) => {
      if (browserName !== 'chromium') throw new Error('This compatibility matrix targets Chromium')
      if (process.env.COMPAT_BACKEND === 'chromium') return use(undefined)
      const engine = await launchEngine({
        executablePath: required(process.env.SUITECUT_NATIVE_EXECUTABLE),
        width: 640,
        height: 360,
        framesPerSecond: 30,
      })
      engine.source.onFailure?.((error) =>
        process.stderr.write(`Native failure: ${error.message}\n`),
      )
      try {
        await use(engine)
      } finally {
        await engine.close()
      }
    },
    { scope: 'worker' },
  ],
  browser: [
    async ({ engine }, use) => {
      if (engine) return use(engine.browser)
      const browser = await chromium.launch({ headless: true, args: ['--site-per-process'] })
      try {
        await use(browser)
      } finally {
        await browser.close()
      }
    },
    { scope: 'worker' },
  ],
})

let server: Server,
  secure: Server,
  proxy: Server,
  origin: string,
  cross: string,
  tlsOrigin: string,
  proxyUrl: string
let proxyRequests: { url: string; authenticated: boolean }[] = []
let originAuthHeaders: (string | undefined)[] = []
const basic = 'Basic ' + Buffer.from('user:password').toString('base64')
const proxyBasic = 'Basic ' + Buffer.from('proxy:secret').toString('base64')
const pendingDownloads = new Set<ServerResponse>()
const html = `<!doctype html><button onclick="this.textContent='clicked'">Click</button><input aria-label="Name"><canvas width="640" height="250"></canvas><script>let n=0;function draw(){const c=document.querySelector('canvas').getContext('2d');c.fillStyle=n++%2?'red':'blue';c.fillRect(0,0,640,250);requestAnimationFrame(draw)}draw()</script>`
const sw = `self.addEventListener('install',e=>e.waitUntil(self.skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));self.addEventListener('fetch',e=>{if(new URL(e.request.url).pathname==='/sw-data')e.respondWith(new Response('from-worker'))});self.marker=42;`
function handle(req: IncomingMessage, res: ServerResponse) {
  const path = new URL(req.url ?? '/', 'http://localhost').pathname
  if (path === '/slow-download') {
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Disposition': 'attachment; filename="slow.bin"',
      'Content-Length': String(1024 * 1024),
    })
    pendingDownloads.add(res)
    res.on('close', () => pendingDownloads.delete(res))
    res.write(Buffer.alloc(1024, 7))
    return
  }
  if (path === '/cookie-echo') {
    res.setHeader('Content-Type', 'text/plain')
    res.end(req.headers.cookie ?? '')
    return
  }
  if (path === '/initial-language') {
    res.writeHead(200, { 'Content-Type': 'text/html' })
    res.end('<script>window.initialLanguage = navigator.language</script>')
    return
  }
  if (path === '/auth') originAuthHeaders.push(req.headers.authorization)
  if (path === '/auth' && req.headers.authorization !== basic) {
    res.writeHead(401, { 'WWW-Authenticate': 'Basic realm="suitecut-local"' })
    res.end('unauthorized')
    return
  }
  if (path === '/sw.js') {
    res.writeHead(200, { 'Content-Type': 'application/javascript', 'Cache-Control': 'no-store' })
    res.end(sw)
    return
  }
  if (path === '/worker.js') {
    res.writeHead(200, { 'Content-Type': 'application/javascript' })
    res.end('self.marker=41;onmessage=e=>postMessage(e.data+1)')
    return
  }
  if (path === '/redirect') {
    res.writeHead(302, { Location: '/json' })
    res.end()
    return
  }
  if (path === '/json') {
    res.setHeader('Content-Type', 'application/json')
    res.end('{"value":42}')
    return
  }
  if (path === '/post') {
    let body = ''
    req.on('data', (c) => (body += c))
    req.on('end', () => {
      res.setHeader('Content-Type', 'application/json')
      res.end(JSON.stringify({ body }))
    })
    return
  }
  res.setHeader('Content-Type', 'text/html')
  res.end(path === '/auth' ? '<h1>Authenticated</h1>' : html)
}
async function listen(s: Server) {
  await new Promise<void>((r) => s.listen(0, '0.0.0.0', r))
  const a = s.address()
  if (!a || typeof a === 'string') throw Error('Missing address')
  return a.port
}
test.beforeAll(async () => {
  server = createServer(handle)
  const port = await listen(server)
  origin = `http://127.0.0.1:${port}`
  cross = `http://localhost:${port}`
  secure = createHttpsServer(
    {
      key: await readFile(new URL('./fixtures/key.pem', import.meta.url)),
      cert: await readFile(new URL('./fixtures/cert.pem', import.meta.url)),
    },
    handle,
  )
  tlsOrigin = `https://127.0.0.1:${await listen(secure)}`
  proxy = createServer((req, res) => {
    const authenticated = req.headers['proxy-authorization'] === proxyBasic
    proxyRequests.push({ url: req.url ?? '/', authenticated })
    if (req.url?.includes('auth-proxy.test') && !authenticated) {
      res.writeHead(407, { 'Proxy-Authenticate': 'Basic realm="local-proxy"' })
      res.end()
      return
    }
    const target = new URL(req.url ?? '/')
    const forwarded = server.address()
    if (!forwarded || typeof forwarded === 'string') throw Error('Missing server')
    void import('node:http').then(({ request }) => {
      const headers = { ...req.headers }
      delete headers['proxy-authorization']
      const upstream = request(
        {
          hostname: '127.0.0.1',
          port: forwarded.port,
          path: target.pathname + target.search,
          method: req.method,
          headers,
        },
        (r) => {
          res.writeHead(r.statusCode ?? 502, r.headers)
          r.pipe(res)
        },
      )
      upstream.on('error', (e) => {
        res.writeHead(502)
        res.end(String(e))
      })
      req.pipe(upstream)
    })
  })
  proxy.on('connect', (req, socket, head) => {
    const authenticated = req.headers['proxy-authorization'] === proxyBasic
    proxyRequests.push({ url: req.url ?? '/', authenticated })
    if (req.url?.startsWith('auth-proxy.test') && !authenticated) {
      socket.end(
        'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="local-proxy"\r\nContent-Length: 0\r\n\r\n',
      )
      return
    }
    const a = secure.address()
    if (!a || typeof a === 'string') {
      socket.destroy()
      return
    }
    const upstream = connect(a.port, '127.0.0.1', () => {
      socket.write('HTTP/1.1 200 Connection Established\r\n\r\n')
      if (head.length) upstream.write(head)
      upstream.pipe(socket)
      socket.pipe(upstream)
    })
    upstream.on('error', () => socket.destroy())
    socket.on('error', () => upstream.destroy())
    socket.on('close', () => upstream.destroy())
  })
  proxyUrl = `http://127.0.0.1:${await listen(proxy)}`
})
test.afterAll(async () => {
  for (const s of [proxy, secure, server]) {
    s?.closeAllConnections()
    await new Promise<void>((r) => s?.close(() => r()))
  }
})

async function register(page: Page) {
  await page.goto(origin)
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/sw.js')
    await navigator.serviceWorker.ready
    if (!navigator.serviceWorker.controller)
      await new Promise<void>((r) =>
        navigator.serviceWorker.addEventListener('controllerchange', () => r(), { once: true }),
      )
  })
}
test('worker: dedicated Worker evaluation and messaging', async ({ page }) => {
  await page.goto(origin)
  const event = page.waitForEvent('worker')
  await page.evaluate(() => {
    const w = new Worker('/worker.js')
    window.suitecutWorker = w
  })
  const worker = await event
  expect(await worker.evaluate(() => (self as typeof self & { marker: number }).marker)).toBe(41)
  expect(
    await page.evaluate(
      () =>
        new Promise<number>((r) => {
          window.suitecutWorker.onmessage = (e: MessageEvent<number>) => r(e.data)
          window.suitecutWorker.postMessage(8)
        }),
    ),
  ).toBe(9)
})
test('worker: service worker website control and fetch', async ({ page }) => {
  await register(page)
  expect(await page.evaluate(() => fetch('/sw-data').then((r) => r.text()))).toBe('from-worker')
})
test('worker: context serviceworker event, enumeration and evaluation', async ({
  page,
  context,
}) => {
  const event = context.waitForEvent('serviceworker', { timeout: 5000 })
  void event.catch(() => undefined)
  await register(page)
  const worker = await event
  expect(context.serviceWorkers()).toContain(worker)
  expect(await worker.evaluate(() => (self as typeof self & { marker: number }).marker)).toBe(42)
})
test('worker: service worker console and close event on context disposal', async ({
  page,
  context,
}) => {
  const event = context.waitForEvent('serviceworker', { timeout: 5000 })
  void event.catch(() => undefined)
  await register(page)
  const worker = await event
  const consoleEvent = context.waitForEvent('console', { timeout: 5000 })
  void consoleEvent.catch(() => undefined)
  await worker.evaluate(() => console.log('suitecut-worker-console'))
  expect((await consoleEvent).text()).toBe('suitecut-worker-console')
  const closed = worker.waitForEvent('close', { timeout: 5000 })
  void closed.catch(() => undefined)
  await context.close()
  await closed
})
test('worker: multiple pages expose one service worker identity', async ({ page, context }) => {
  const event = context.waitForEvent('serviceworker', { timeout: 5000 })
  void event.catch(() => undefined)
  await register(page)
  const worker = await event
  const second = await context.newPage()
  await second.goto(origin)
  await second.evaluate(() => navigator.serviceWorker.ready)
  await delay(200)
  expect(context.serviceWorkers()).toHaveLength(1)
  expect(await worker.evaluate(() => (self as typeof self & { marker: number }).marker)).toBe(42)
  await second.close()
})
test('worker: worker remains automatable after its first page closes', async ({
  page,
  context,
}) => {
  const event = context.waitForEvent('serviceworker', { timeout: 5000 })
  void event.catch(() => undefined)
  await register(page)
  const worker = await event
  const second = await context.newPage()
  await second.goto(origin)
  await second.evaluate(() => navigator.serviceWorker.ready)
  await page.close()
  await expect.poll(() => context.serviceWorkers().length).toBe(1)
  expect(context.serviceWorkers()[0]).toBe(worker)
  expect(
    await required(context.serviceWorkers()[0]).evaluate(
      () => (self as typeof self & { marker: number }).marker,
    ),
  ).toBe(42)
  await second.close()
})
test('worker: repeated worker contexts release browser target registrations', async ({
  browser,
}) => {
  test.setTimeout(60000)
  const root = await browser.newBrowserCDPSession()
  try {
    for (let i = 0; i < 80; i++) {
      const c = await browser.newContext()
      try {
        const p = await c.newPage()
        const event = c.waitForEvent('serviceworker', { timeout: 5000 })
        void event.catch(() => undefined)
        await register(p)
        expect(
          await (await event).evaluate(() => (self as typeof self & { marker: number }).marker),
        ).toBe(42)
      } finally {
        await c.close()
      }
    }
    await expect
      .poll(
        async () =>
          (await root.send('Target.getTargets')).targetInfos.filter(
            (t) => t.type === 'service_worker',
          ).length,
      )
      .toBe(0)
  } finally {
    await root.detach()
  }
})
test('native: worker CDP aliases detach when their context closes', async ({ browser, engine }) => {
  test.skip(!engine, 'This checks the native shim alias lifecycle')
  test.setTimeout(60000)
  const root = await browser.newBrowserCDPSession(),
    detached = new Set<string>()
  root.on('Target.detachedFromTarget', (event) => detached.add(event.sessionId))
  try {
    for (let i = 0; i < 80; i++) {
      const c = await browser.newContext()
      let extra: string | undefined
      try {
        const p = await c.newPage()
        const event = c.waitForEvent('serviceworker', { timeout: 5000 })
        void event.catch(() => undefined)
        await register(p)
        await event
        const target = required(
          (await root.send('Target.getTargets')).targetInfos.find(
            (t) => t.type === 'service_worker',
          ),
        )
        extra = (
          await root.send('Target.attachToTarget', { targetId: target.targetId, flatten: true })
        ).sessionId
      } finally {
        await c.close()
      }
      await expect.poll(() => detached.has(required(extra))).toBe(true)
    }
  } finally {
    await root.detach()
  }
})
test('worker: serviceWorkers block prevents registration', async ({ browser }) => {
  const c = await browser.newContext({ serviceWorkers: 'block' })
  try {
    const p = await c.newPage()
    await p.goto(origin)
    await p.evaluate(() => navigator.serviceWorker.register('/sw.js').catch(() => null))
    expect(
      await p.evaluate(() => navigator.serviceWorker.getRegistrations().then((r) => r.length)),
    ).toBe(0)
  } finally {
    await c.close()
  }
})
test('oopif: cross-site frame is a separate CDP target and remains interactive', async ({
  page,
  context,
}) => {
  await page.goto(origin)
  const session = await context.newCDPSession(page)
  const attachments: { targetInfo: { type: string } }[] = []
  session.on('Target.attachedToTarget', (e) => attachments.push(e))
  await session.send('Target.setAutoAttach', {
    autoAttach: true,
    waitForDebuggerOnStart: false,
    flatten: true,
  })
  await page.evaluate((url) => {
    const f = document.createElement('iframe')
    f.src = url
    document.body.append(f)
  }, cross)
  await page.frameLocator('iframe').getByRole('button').click()
  await expect(page.frameLocator('iframe').getByRole('button')).toHaveText('clicked')
  await expect.poll(() => attachments.some((e) => e.targetInfo.type === 'iframe')).toBe(true)
  await expect.poll(() => page.frames().map((f) => f.url())).toContain(cross + '/')
  const frame = required(page.frames().find((f) => f.url().startsWith(cross)))
  expect(await frame.evaluate(() => location.origin)).toBe(cross)
  await session.detach()
})
test('oopif: cross-site navigation, detach and network routing', async ({ page, context }) => {
  await context.route('**/json', (r) => r.fulfill({ json: { value: 'routed' } }))
  await page.goto(origin)
  await page.evaluate((url) => {
    const f = document.createElement('iframe')
    f.src = url
    document.body.append(f)
  }, cross)
  const inner = page.frameLocator('iframe')
  await inner.getByLabel('Name').fill('cross-site')
  await expect(inner.getByLabel('Name')).toHaveValue('cross-site')
  const f = required(page.frames().find((f) => f.url().startsWith(cross)))
  expect(await f.evaluate(() => fetch('/json').then((r) => r.json()))).toEqual({ value: 'routed' })
  await page
    .locator('iframe')
    .evaluate((f, url) => ((f as HTMLIFrameElement).src = url), origin + '/next')
  await expect.poll(() => page.frames().map((f) => f.url())).toContain(origin + '/next')
  await inner.getByRole('button').click()
  await expect(inner.getByRole('button')).toHaveText('clicked')
  const detached = page.waitForEvent('framedetached')
  await page.locator('iframe').evaluate((f) => f.remove())
  await detached
  expect(page.frames()).toHaveLength(1)
})
test('viewport: odd and tiny browser dimensions support screenshots and Playwright video', async ({
  browser,
}, info) => {
  const c = await browser.newContext({
    viewport: { width: 1904, height: 609 },
    recordVideo: { dir: info.outputPath('odd-videos'), size: { width: 640, height: 204 } },
  })
  try {
    const p = await c.newPage()
    await p.goto(origin)
    expect(await p.evaluate(() => ({ width: innerWidth, height: innerHeight }))).toEqual({
      width: 1904,
      height: 609,
    })
    const png = await p.screenshot()
    expect(png.readUInt32BE(16)).toBe(1904)
    expect(png.readUInt32BE(20)).toBe(609)
    await p.getByRole('button').click()
    await expect(p.getByRole('button')).toHaveText('clicked')
    await p.setViewportSize({ width: 1, height: 1 })
    const tiny = await p.screenshot()
    expect(tiny.readUInt32BE(16)).toBe(1)
    expect(tiny.readUInt32BE(20)).toBe(1)
  } finally {
    await c.close()
  }
})
test('locale: popups inherit context language before their first script without cross-context leakage', async ({
  browser,
}) => {
  const french = await browser.newContext({ locale: 'fr-FR' })
  const german = await browser.newContext({ locale: 'de-DE' })
  try {
    for (const [context, expected] of [
      [french, 'fr-FR'],
      [german, 'de-DE'],
      [french, 'fr-FR'],
    ] as const) {
      const page = await context.newPage()
      await page.goto(origin + '/initial-language')
      expect(await page.evaluate('window.initialLanguage')).toBe(expected)
      const [popup] = await Promise.all([
        page.waitForEvent('popup'),
        page.evaluate((url) => {
          window.open(url)
        }, origin + '/initial-language'),
      ])
      await popup.waitForLoadState('domcontentloaded')
      expect(await popup.evaluate('window.initialLanguage')).toBe(expected)
      await page.close()
      await popup.close()
    }
  } finally {
    await french.close()
    await german.close()
  }
})

test('viewport: real browser version metadata preserves 4K windowless dimensions', async ({
  browser,
}) => {
  const context = await browser.newContext({ viewport: { width: 3840, height: 2160 } })
  try {
    const page = await context.newPage()
    await page.goto(origin)
    expect(await page.evaluate(() => ({ width: innerWidth, height: innerHeight }))).toEqual({
      width: 3840,
      height: 2160,
    })
    const cdp = await context.newCDPSession(page)
    try {
      const version = await cdp.send('Browser.getVersion')
      expect(version.userAgent).toBe(await page.evaluate(() => navigator.userAgent))
      expect(version.jsVersion).not.toBe('')
      const { bounds } = await cdp.send('Browser.getWindowForTarget')
      expect(bounds.width).toBe(3840)
      expect(bounds.height).toBe(2160)
    } finally {
      await cdp.detach()
    }
  } finally {
    await context.close()
  }
})

test('state: IndexedDB auth export to file and restore into a fresh context', async ({
  page,
  context,
  browser,
}, info) => {
  await page.goto(origin)
  await page.evaluate(async () => {
    localStorage.setItem('session', 'local-token')
    document.cookie = 'auth=cookie-token; SameSite=Lax; path=/'
    const db = await new Promise<IDBDatabase>((r, j) => {
      const q = indexedDB.open('auth-db', 3)
      q.onupgradeneeded = () => {
        const s = q.result.createObjectStore('tokens', { keyPath: 'id' })
        s.createIndex('byScope', 'scope', { unique: true })
      }
      q.onsuccess = () => r(q.result)
      q.onerror = () => j(new Error(q.error?.message ?? 'IndexedDB failure'))
    })
    await new Promise<void>((r, j) => {
      const t = db.transaction('tokens', 'readwrite')
      t.objectStore('tokens').put({
        id: 'user',
        scope: 'login',
        token: 'idb-token',
        bytes: new Uint8Array([0, 1, 254, 255]),
        created: new Date('2026-01-01'),
      })
      t.oncomplete = () => r()
      t.onerror = () => j(new Error(t.error?.message ?? 'IndexedDB failure'))
    })
    db.close()
  })
  const path = info.outputPath('state.json')
  const state = await context.storageState({ path, indexedDB: true })
  expect(state.origins[0]).toMatchObject({ indexedDB: [{ version: 3 }] })
  await context.close()
  const restored = await browser.newContext({ storageState: path })
  try {
    const p = await restored.newPage()
    await p.goto(origin)
    expect(
      await p.evaluate(() => ({ cookie: document.cookie, local: localStorage.getItem('session') })),
    ).toEqual({ cookie: 'auth=cookie-token', local: 'local-token' })
    expect(
      await p.evaluate(
        () =>
          new Promise((r, j) => {
            const q = indexedDB.open('auth-db')
            q.onsuccess = () => {
              const db = q.result,
                t = db.transaction('tokens'),
                get = t.objectStore('tokens').index('byScope').get('login')
              get.onsuccess = () => {
                const v = get.result as { token: string; bytes: Uint8Array; created: Date }
                r({ token: v.token, bytes: Array.from(v.bytes), created: v.created.toISOString() })
                db.close()
              }
              get.onerror = () => j(new Error(get.error?.message ?? 'IndexedDB failure'))
            }
          }),
      ),
    ).toEqual({ token: 'idb-token', bytes: [0, 1, 254, 255], created: '2026-01-01T00:00:00.000Z' })
    expect(await restored.storageState({ indexedDB: true })).toEqual(state)
  } finally {
    await restored.close()
  }
  const isolated = await browser.newContext()
  try {
    const p = await isolated.newPage()
    await p.goto(origin)
    expect(
      await p.evaluate(() => ({ cookie: document.cookie, local: localStorage.getItem('session') })),
    ).toEqual({ cookie: '', local: null })
  } finally {
    await isolated.close()
  }
})
test('state: lone UTF-16 values survive arguments, console, state export and import', async ({
  page,
  context,
  browser,
}) => {
  const values = ['\udafe', '\udc00', '\\udafe', '😀']
  await page.goto(origin)
  await page.evaluate((values) => {
    values.forEach((value, i) => localStorage.setItem(`unicode-${i}`, value))
  }, values)
  const consoleEvent = page.waitForEvent('console')
  await page.evaluate((value) => console.log(value), values[0])
  expect((await consoleEvent).text()).toBe(values[0])
  const state = await context.storageState()
  expect(state.origins[0]?.localStorage.map((item) => item.value).sort()).toEqual(
    [...values].sort(),
  )
  const restored = await browser.newContext({ storageState: state })
  try {
    const next = await restored.newPage()
    await next.goto(origin)
    expect(
      await next.evaluate(() =>
        Array.from({ length: 4 }, (_, i) => localStorage.getItem(`unicode-${i}`)),
      ),
    ).toEqual(values)
  } finally {
    await restored.close()
  }
})

test('state: empty IndexedDB export and restore', async ({ page, context, browser }) => {
  await page.goto(origin)
  await page.evaluate(
    () =>
      new Promise<void>((r, j) => {
        const q = indexedDB.open('empty-db')
        q.onsuccess = () => {
          q.result.close()
          r()
        }
        q.onerror = () => j(new Error(q.error?.message ?? 'IndexedDB failure'))
      }),
  )
  const state = await context.storageState({ indexedDB: true })
  const c = await browser.newContext({ storageState: state })
  try {
    expect(await c.storageState({ indexedDB: true })).toEqual(state)
  } finally {
    await c.close()
  }
})
for (const scheme of ['http', 'https']) {
  test(`auth: ${scheme} origin HTTP credentials`, async ({ browser }) => {
    const c = await browser.newContext({
      httpCredentials: { username: 'user', password: 'password' },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      expect(
        required(await p.goto((scheme === 'http' ? origin : tlsOrigin) + '/auth')).status(),
      ).toBe(200)
      await expect(p.getByRole('heading')).toHaveText('Authenticated')
    } finally {
      await c.close()
    }
  })
  test(`proxy: ${scheme} through local proxy plus origin auth`, async ({ browser }) => {
    proxyRequests = []
    const c = await browser.newContext({
      proxy: { server: proxyUrl, bypass: '<-loopback>' },
      httpCredentials: { username: 'user', password: 'password' },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      expect(required(await p.goto(`${scheme}://proxy.test/auth`)).status()).toBe(200)
      await expect(p.getByRole('heading')).toHaveText('Authenticated')
      expect(proxyRequests.length).toBeGreaterThan(0)
    } finally {
      await c.close()
    }
  })
  test(`proxy: ${scheme} authenticated proxy plus origin auth`, async ({ browser }) => {
    proxyRequests = []
    const c = await browser.newContext({
      proxy: { server: proxyUrl, username: 'proxy', password: 'secret', bypass: '<-loopback>' },
      httpCredentials: { username: 'user', password: 'password' },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      expect(required(await p.goto(`${scheme}://auth-proxy.test/auth`)).status()).toBe(200)
      await expect(p.getByRole('heading')).toHaveText('Authenticated')
      expect(proxyRequests.some((r) => r.authenticated)).toBe(true)
    } finally {
      await c.close()
    }
  })
  test(`proxy: ${scheme} separate credentials respect a matching origin`, async ({ browser }) => {
    originAuthHeaders = []
    const target = `${scheme}://auth-proxy.test`
    const c = await browser.newContext({
      proxy: { server: proxyUrl, username: 'proxy', password: 'secret', bypass: '<-loopback>' },
      httpCredentials: { username: 'user', password: 'password', origin: target },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      expect(required(await p.goto(target + '/auth')).status()).toBe(200)
      expect(originAuthHeaders).toContain(basic)
      expect(originAuthHeaders).not.toContain(proxyBasic)
    } finally {
      await c.close()
    }
  })
  for (const scoped of [false, true]) {
    test(`proxy: ${scheme} does not send ${scoped ? 'out-of-origin' : 'proxy'} credentials to the website`, async ({
      browser,
    }) => {
      originAuthHeaders = []
      proxyRequests = []
      const c = await browser.newContext({
        proxy: { server: proxyUrl, username: 'proxy', password: 'secret', bypass: '<-loopback>' },
        ...(scoped
          ? {
              httpCredentials: {
                username: 'user',
                password: 'password',
                origin: 'https://unrelated.test',
              },
            }
          : {}),
        ignoreHTTPSErrors: true,
      })
      try {
        const p = await c.newPage()
        const status = await p.goto(`${scheme}://auth-proxy.test/auth`).then(
          (r) => required(r).status(),
          (error) => {
            expect(String(error)).toContain('ERR_INVALID_AUTH_CREDENTIALS')
            return 401
          },
        )
        expect(status).toBe(401)
        expect(proxyRequests.some((r) => r.authenticated)).toBe(true)
        expect(originAuthHeaders.length).toBeGreaterThan(0)
        expect(originAuthHeaders.every((header) => header === undefined)).toBe(true)
      } finally {
        await c.close()
      }
    })
  }
  test(`proxy: ${scheme} incorrect proxy credentials are denied without reaching the website`, async ({
    browser,
  }) => {
    proxyRequests = []
    originAuthHeaders = []
    const c = await browser.newContext({
      proxy: { server: proxyUrl, username: 'proxy', password: 'wrong', bypass: '<-loopback>' },
      httpCredentials: { username: 'user', password: 'password' },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      // A 407 can commit an error document without a load event in Chromium.
      // This checks authentication denial; default-load navigation completion
      // is a separate boundary preserved in the diagnostic run.
      const status = await p
        .goto(`${scheme}://auth-proxy.test/auth`, { timeout: 5000, waitUntil: 'commit' })
        .then(
          (response) => required(response).status(),
          (error) => {
            expect(String(error)).toMatch(/ERR_/)
            return 407
          },
        )
      expect(status).toBe(407)
      expect(proxyRequests.length).toBeGreaterThan(0)
      expect(proxyRequests.length).toBeLessThan(12)
      expect(originAuthHeaders).toEqual([])
    } finally {
      await c.close()
    }
  })
}
test('auth: wrong credentials fail and a fresh context does not inherit them', async ({
  browser,
}) => {
  for (const creds of [{ username: 'user', password: 'wrong' }, undefined]) {
    const c = await browser.newContext(creds ? { httpCredentials: creds } : {})
    try {
      const p = await c.newPage()
      const outcome = await p.goto(origin + '/auth').then(
        (r) => required(r).status(),
        (e) => {
          expect(String(e)).toContain('ERR_INVALID_AUTH_CREDENTIALS')
          return 401
        },
      )
      expect(outcome).toBe(401)
      expect(await p.getByRole('heading', { name: 'Authenticated' }).count()).toBe(0)
    } finally {
      await c.close()
    }
  }
})
test('proxy: bypass uses the real origin without contacting the proxy', async ({ browser }) => {
  proxyRequests = []
  const c = await browser.newContext({ proxy: { server: proxyUrl, bypass: '127.0.0.1' } })
  try {
    const p = await c.newPage()
    await p.goto(origin)
    expect(proxyRequests.filter((r) => r.url.includes(new URL(origin).host))).toHaveLength(0)
  } finally {
    await c.close()
  }
})
for (const scheme of ['http', 'https'])
  test(`proxy: ${scheme} authenticated proxy without origin auth`, async ({ browser }) => {
    proxyRequests = []
    const c = await browser.newContext({
      proxy: { server: proxyUrl, username: 'proxy', password: 'secret', bypass: '<-loopback>' },
      ignoreHTTPSErrors: true,
    })
    try {
      const p = await c.newPage()
      await p.goto(`${scheme}://auth-proxy.test/`)
      await expect(p.getByRole('button')).toHaveText('Click')
      expect(proxyRequests.some((r) => r.authenticated)).toBe(true)
    } finally {
      await c.close()
    }
  })
for (const extension of ['har', 'har.zip'])
  test(`har: ${extension} captures and replays bodies, POST and redirects`, async ({
    browser,
  }, info) => {
    const path = info.outputPath(`capture.${extension}`)
    const c = await browser.newContext({
      recordHar: { path, content: extension === 'har' ? 'embed' : 'attach' },
      serviceWorkers: 'block',
    })
    try {
      const p = await c.newPage()
      await p.goto(origin)
      expect(await p.evaluate(() => fetch('/redirect').then((r) => r.json()))).toEqual({
        value: 42,
      })
      expect(
        await p.evaluate(() =>
          fetch('/post', { method: 'POST', body: 'hello' }).then((r) => r.json()),
        ),
      ).toEqual({ body: 'hello' })
    } finally {
      await c.close()
    }
    expect((await stat(path)).size).toBeGreaterThan(100)
    if (extension === 'har') {
      const data = HarSchema.parse(JSON.parse(await readFile(path, 'utf8')))
      expect(data.log.entries.some((e) => e.response.status === 302)).toBe(true)
      expect(data.log.entries.some((e) => e.response.content.text?.includes('42'))).toBe(true)
    }
    const replay = await browser.newContext({ serviceWorkers: 'block' })
    try {
      await replay.routeFromHAR(path, { notFound: 'abort' })
      const p = await replay.newPage()
      await p.goto(origin)
      expect(await p.evaluate(() => fetch('/redirect').then((r) => r.json()))).toEqual({
        value: 42,
      })
      expect(
        await p.evaluate(() =>
          fetch('/post', { method: 'POST', body: 'hello' }).then((r) => r.json()),
        ),
      ).toEqual({ body: 'hello' })
      await expect(p.goto(origin + '/not-in-har')).rejects.toThrow()
    } finally {
      await replay.close()
    }
  })
test('video: Playwright recordVideo creates a decodable WebM after context close', async ({
  browser,
}, info) => {
  const c = await browser.newContext({
    viewport: { width: 640, height: 360 },
    recordVideo: { dir: info.outputPath('videos'), size: { width: 320, height: 180 } },
  })
  let video: ReturnType<Page['video']>
  try {
    const p = await c.newPage()
    await p.goto(origin)
    video = p.video()
    expect(video).not.toBeNull()
    await delay(1200)
  } finally {
    await c.close()
  }
  const path = await required(video).path()
  const data = ProbeSchema.parse(
    JSON.parse(
      execFileSync(
        'ffprobe',
        ['-v', 'error', '-show_streams', '-show_format', '-of', 'json', path],
        {
          encoding: 'utf8',
        },
      ),
    ),
  )
  expect(data.streams[0]).toMatchObject({ codec_name: 'vp8', width: 320, height: 180 })
  expect(Number(data.format.duration)).toBeGreaterThan(0.5)
  execFileSync('ffmpeg', ['-v', 'error', '-i', path, '-f', 'null', '-'], { timeout: 15000 })
  await writeFile(info.outputPath('ffprobe.json'), JSON.stringify(data, null, 2))
})

test('downloads: repeated cancellation releases requests and capacity', async ({ page }) => {
  test.setTimeout(60000)
  await page.goto(origin)
  await page.setContent(`<a href="${origin}/slow-download">download</a>`)
  // Exceed the native concurrent-download cap over time. Finished entries
  // must be reclaimed, and cancellation must close the server connection.
  for (let i = 0; i < 70; i++) {
    const [download] = await Promise.all([page.waitForEvent('download'), page.click('a')])
    await download.cancel()
    expect(await download.failure()).toBe('canceled')
    await expect.poll(() => pendingDownloads.size).toBe(0)
  }
})

test('downloads: denied download reports failure and creates no saved file', async ({
  browser,
}, info) => {
  const context = await browser.newContext({ acceptDownloads: false })
  try {
    const page = await context.newPage()
    await page.goto(origin)
    await page.setContent(`<a href="${origin}/slow-download">download</a>`)
    const [download] = await Promise.all([page.waitForEvent('download'), page.click('a')])
    expect(await download.failure()).toContain('acceptDownloads')
    const destination = info.outputPath('denied.bin')
    await expect(download.saveAs(destination)).rejects.toThrow('acceptDownloads')
    await expect(stat(destination)).rejects.toMatchObject({ code: 'ENOENT' })
    await expect.poll(() => pendingDownloads.size).toBe(0)
  } finally {
    await context.close()
  }
})

test('cdp: session close event precedes detach completion', async ({ page }) => {
  const session = await page.context().newCDPSession(page)
  const events: string[] = []
  session.on('close', () => events.push('closed'))
  await session.detach()
  events.push('detached')
  expect(events).toEqual(['closed', 'detached'])
  await expect(session.send('Runtime.evaluate', { expression: '1 + 1' })).rejects.toThrow()
})

test('permissions: local network grant resets and stays within its context', async ({
  browser,
}) => {
  const allowed = await browser.newContext()
  const isolated = await browser.newContext()
  try {
    const page = await allowed.newPage()
    const other = await isolated.newPage()
    await page.goto(origin)
    await other.goto(origin)
    const state = (p: Page) =>
      p.evaluate(
        async () =>
          (await navigator.permissions.query({ name: 'loopback-network' as PermissionName })).state,
      )
    expect(await state(page)).toBe('prompt')
    await allowed.grantPermissions(['local-network-access'], { origin })
    expect(await state(page)).toBe('granted')
    expect(await state(other)).toBe('prompt')
    await allowed.clearPermissions()
    expect(await state(page)).toBe('prompt')
    await allowed.grantPermissions([], { origin })
    expect(await state(page)).toBe('denied')
  } finally {
    await allowed.close()
    await isolated.close()
  }
})

test('cdp: explicit page sessions keep debugger domains independent', async ({ page }) => {
  const session = await page.context().newCDPSession(page)
  try {
    await session.send('Debugger.enable')
    await page.coverage.startJSCoverage()
    await page.coverage.stopJSCoverage()
    const parsed = new Promise<void>((resolve) => {
      session.on('Debugger.scriptParsed', (event) => {
        if (event.url === 'suitecut-independent.js') resolve()
      })
    })
    await Promise.all([parsed, page.evaluate('void 0;\n//# sourceURL=suitecut-independent.js')])
    const result = await session.send('Runtime.evaluate', { expression: '21 * 2' })
    expect(result.result.value).toBe(42)
  } finally {
    await session.detach()
  }
})

for (const pagesFirst of [false, true]) {
  test(`cookies: partitioned cookies stay isolated with pagesFirst=${pagesFirst}`, async ({
    browser,
  }) => {
    const context = await browser.newContext({ ignoreHTTPSErrors: true })
    const other = await browser.newContext({ ignoreHTTPSErrors: true })
    try {
      if (pagesFirst) await context.newPage()
      await context.addCookies([
        {
          name: 'chip',
          value: 'one',
          domain: '127.0.0.1',
          path: '/',
          secure: true,
          sameSite: 'None',
          partitionKey: 'https://localhost',
        },
        {
          name: 'chip',
          value: 'two',
          domain: '127.0.0.1',
          path: '/',
          secure: true,
          sameSite: 'None',
          partitionKey: 'https://example.com',
        },
        { name: 'plain', value: 'three', url: tlsOrigin },
      ])
      expect((await context.cookies()).filter((cookie) => cookie.name === 'chip')).toEqual(
        expect.arrayContaining([
          expect.objectContaining({ value: 'one', partitionKey: 'https://localhost' }),
          expect.objectContaining({ value: 'two', partitionKey: 'https://example.com' }),
        ]),
      )
      expect(await other.cookies()).toEqual([])
      expect(context.pages()).toHaveLength(pagesFirst ? 1 : 0)
      const state = await context.storageState()
      await other.addCookies(state.cookies)
      expect(await other.cookies()).toEqual(await context.cookies())
      await context.clearCookies({ name: 'chip' })
      expect(await context.cookies()).toEqual([expect.objectContaining({ name: 'plain' })])
      expect(await other.cookies()).toHaveLength(3)
      await context.clearCookies()
      expect(await context.cookies()).toEqual([])
    } finally {
      await context.close()
      await other.close()
    }
  })
}

for (const deviceScaleFactor of [1, 2]) {
  test(`screenshots: oversized elements preserve pixels and viewport at DPR ${deviceScaleFactor}`, async ({
    browser,
  }, info) => {
    const context = await browser.newContext({
      viewport: { width: 500, height: 500 },
      deviceScaleFactor,
    })
    try {
      const page = await context.newPage()
      await page.setContent(
        '<style>::-webkit-scrollbar{display:none}.box{border:1px solid blue;width:600px;height:600px;margin-left:50px}</style><div class="box"></div><div class="box"></div>',
      )
      const images = await Promise.all(
        (await page.$$('.box')).map((element) => element.screenshot()),
      )
      const { default: sharp } = await import('sharp')
      for (const [index, image] of images.entries()) {
        const { data, info: pixels } = await sharp(image)
          .ensureAlpha()
          .raw()
          .toBuffer({ resolveWithObject: true })
        expect(pixels.width).toBe(602 * deviceScaleFactor)
        expect(pixels.height).toBe(602 * deviceScaleFactor)
        for (let x = 0; x < pixels.width; x++) {
          expect([...data.subarray(x * 4, x * 4 + 4)]).toEqual([0, 0, 255, 255])
          const bottom = ((pixels.height - 1) * pixels.width + x) * 4
          expect([...data.subarray(bottom, bottom + 4)]).toEqual([0, 0, 255, 255])
        }
        await writeFile(info.outputPath(`oversized-${index}.png`), image)
      }
      expect(await page.evaluate(() => [innerWidth, innerHeight, devicePixelRatio])).toEqual([
        500,
        500,
        deviceScaleFactor,
      ])
    } finally {
      await context.close()
    }
  })
}

for (const concurrent of [false, true]) {
  test(`screenshots: independent sessions preserve the viewport with concurrent=${concurrent}`, async ({
    page,
    engine,
  }, info) => {
    test.skip(
      concurrent && !engine,
      'Native screenshot serialization guarantee; stock Chromium races independent capture sessions',
    )
    await page.setViewportSize({ width: 500, height: 500 })
    await page.setContent(
      '<style>body{margin:0}::-webkit-scrollbar{display:none}</style><div style="width:100%;height:10000px;background:blue"></div>',
    )
    const sessions = await Promise.all([
      page.context().newCDPSession(page),
      page.context().newCDPSession(page),
    ])
    try {
      const capture = (index: number) =>
        required(sessions[index]).send('Page.captureScreenshot', {
          format: 'png',
          captureBeyondViewport: true,
          clip: { x: 0, y: 0, width: 500, height: index ? 10000 : 602, scale: 1 },
        })
      const captures = concurrent
        ? await Promise.all([capture(0), capture(1)])
        : [await capture(0), await capture(1)]
      const { default: sharp } = await import('sharp')
      for (const [index, result] of captures.entries()) {
        const image = Buffer.from(result.data, 'base64')
        const metadata = await sharp(image).metadata()
        expect(metadata.width).toBe(500)
        expect(metadata.height).toBe(index ? 10000 : 602)
        await writeFile(info.outputPath(`parallel-${index}.png`), image)
      }
      expect(await page.evaluate(() => [innerWidth, innerHeight])).toEqual([500, 500])
    } finally {
      await Promise.all(sessions.map((session) => session.detach()))
    }
  })
}

test('native: screenshot bounds reject excessive pixel allocation', async ({ page, engine }) => {
  test.skip(!engine, 'Native bounded capture policy')
  const session = await page.context().newCDPSession(page)
  try {
    await expect(
      session.send('Page.captureScreenshot', {
        clip: { x: 0, y: 0, width: 16384, height: 16384, scale: 1 },
      }),
    ).rejects.toThrow('pixel')
    expect(await page.evaluate(() => 21 * 2)).toBe(42)
    expect(await page.evaluate(() => [innerWidth, innerHeight])).toEqual([640, 360])
  } finally {
    await session.detach()
  }
})

test('cookies: partition keys determine which cookie reaches a cross-site frame', async ({
  browser,
}) => {
  const context = await browser.newContext({ ignoreHTTPSErrors: true })
  try {
    await context.addCookies([
      {
        name: 'chip',
        value: 'one',
        domain: '127.0.0.1',
        path: '/',
        secure: true,
        sameSite: 'None',
        partitionKey: 'https://localhost',
      },
      {
        name: 'chip',
        value: 'two',
        domain: '127.0.0.1',
        path: '/',
        secure: true,
        sameSite: 'None',
        partitionKey: 'https://example.com',
      },
    ])
    const page = await context.newPage()
    await page.goto(tlsOrigin.replace('127.0.0.1', 'localhost'))
    await page.setContent(`<iframe src="${tlsOrigin}/cookie-echo"></iframe>`)
    await expect(page.frameLocator('iframe').locator('body')).toHaveText('chip=one')
  } finally {
    await context.close()
  }
})

test('native: repeated empty cookie contexts reclaim private control targets', async ({
  browser,
  engine,
}) => {
  test.skip(!engine, 'Native context-agent lifecycle')
  test.setTimeout(60000)
  const session = await browser.newBrowserCDPSession()
  try {
    const before = (await session.send('Target.getTargets')).targetInfos
      .map((target) => target.targetId)
      .sort()
    for (let i = 0; i < 70; i++) {
      const context = await browser.newContext()
      await context.addCookies([{ name: 'cycle', value: String(i), url: origin }])
      expect(context.pages()).toEqual([])
      expect(await context.cookies()).toEqual([
        expect.objectContaining({ name: 'cycle', value: String(i) }),
      ])
      await context.close()
    }
    expect(
      (await session.send('Target.getTargets')).targetInfos.map((target) => target.targetId).sort(),
    ).toEqual(before)
  } finally {
    await session.detach()
  }
})

test('screenshots: JPEG uses a white matte and preserves PNG transparency', async ({ page }) => {
  await page.goto(origin)
  await page.setContent('<body></body>')
  const jpeg = await page.screenshot({ type: 'jpeg', omitBackground: true })
  const { default: sharp } = await import('sharp')
  const matte = await sharp(jpeg).removeAlpha().raw().toBuffer()
  expect(matte.every((value) => value === 255)).toBe(true)
  const png = await page.screenshot({ omitBackground: true })
  const alpha = await sharp(png).ensureAlpha().raw().toBuffer()
  expect(alpha.every((value, index) => index % 4 !== 3 || value === 0)).toBe(true)
})
