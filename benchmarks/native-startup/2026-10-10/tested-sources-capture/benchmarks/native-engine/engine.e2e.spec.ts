import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

import { test, expect } from '../../src/engine-test.js'

let server: ReturnType<typeof createServer>, url: string
const artifactDirectory = resolve(
  process.env.SUITECUT_ENGINE_E2E_DIRECTORY ?? 'benchmarks/native-engine/2026-10-09',
)
const html = `<!doctype html><style>body{margin:0;height:2000px}canvas{position:fixed;left:0;top:120px}</style>
<button onclick="document.querySelector('output').textContent='clicked'">Click</button><input placeholder="Name"><output></output>
<select aria-label="Choice"><option value="a">Alpha</option><option value="b">Beta</option></select><input type="file" aria-label="Upload">
<a href="/download" download>Download</a><button onclick="window.open('/popup')">Popup</button><button onclick="alert('native dialog')">Dialog</button>
<iframe title="Inner" srcdoc="<button>Inner button</button>"></iframe><canvas width="640" height="180"></canvas>
<script>const c=document.querySelector('canvas').getContext('2d');let n=0;function frame(){c.fillStyle='rgb('+(n++%200)+',50,90)';c.fillRect(0,0,640,180);requestAnimationFrame(frame)}frame();const a=new AudioContext();const o=a.createOscillator();const g=a.createGain();g.gain.value=.15;o.connect(g).connect(a.destination);o.start()</script>`
test.beforeAll(async () => {
  server = createServer((req, res) => {
    if (req.url === '/silent') {
      res.setHeader('content-type', 'text/html')
      res.end(
        `<h1>Delayed audio</h1><button onclick="const a=new AudioContext({sampleRate:48000});const o=a.createOscillator();const g=a.createGain();g.gain.value=.08;o.connect(g).connect(a.destination);o.start();a.resume();window.audio=a">Start audio</button>`,
      )
      return
    }
    if (req.url === '/download') {
      res.setHeader('content-disposition', 'attachment; filename="native.txt"')
      res.end('native download')
      return
    }
    if (req.url === '/data') {
      res.setHeader('content-type', 'application/json')
      res.end('{"value":"server"}')
      return
    }
    res.setHeader('content-type', 'text/html')
    res.end(html)
  })
  await new Promise<void>((r) => server.listen(0, '127.0.0.1', r))
  const address = server.address()
  if (!address || typeof address === 'string') throw new Error('Missing address')
  url = `http://127.0.0.1:${address.port}`
})
test.afterAll(async () => {
  server.closeAllConnections()
  await new Promise<void>((r) => server.close(() => r()))
})
test('real Playwright locators, auto-wait, input, scrolling, iframe and screenshot', async ({
  page,
}) => {
  await page.goto(url)
  await page.getByRole('button', { name: 'Click', exact: true }).click()
  await expect(page.locator('output')).toHaveText('clicked')
  await page.getByPlaceholder('Name').fill('CEF + Playwright')
  await expect(page.getByPlaceholder('Name')).toHaveValue('CEF + Playwright')
  await page.getByRole('combobox', { name: 'Choice' }).selectOption('b')
  await expect(page.getByRole('combobox')).toHaveValue('b')
  await page.frameLocator('iframe').getByRole('button').click()
  await page.mouse.move(600, 300)
  await page.mouse.wheel(0, 500)
  await expect.poll(() => page.evaluate(() => scrollY)).toBeGreaterThan(100)
  await page.screenshot({ path: resolve(artifactDirectory, 'playwright.png') })
})
test('native audio readiness waits for delayed sound before returning a recording', async ({
  page,
  suitecutEngine,
}) => {
  await page.goto(`${url}/silent`)
  let settled = false
  const recording = suitecutEngine.record(page, {
    destination: resolve(artifactDirectory, 'audio-readiness.mkv'),
    audioCodec: 'pcm_s16le',
    audioStartup: 'wait-for-signal',
  })
  void recording.then(
    () => {
      settled = true
    },
    () => {
      settled = true
    },
  )
  await delay(150)
  expect(settled).toBe(false)
  await page.getByRole('button', { name: 'Start audio' }).click()
  const output = await recording
  expect((await output.status()).ready).toBe(true)
  await delay(250)
  const final = await output.stop()
  expect(final.error).toBe('')
  expect(final.audioSamples).toBeGreaterThan(0)
})
test('audio readiness times out without sound and leaves the engine usable', async ({
  page,
  suitecutEngine,
}) => {
  await page.goto(`${url}/silent`)
  await expect(
    suitecutEngine.record(page, {
      destination: resolve(artifactDirectory, 'audio-timeout.mkv'),
      audioStartup: 'wait-for-signal',
      audioStartupTimeoutMs: 150,
    }),
  ).rejects.toThrow('Native audio readiness timed out')
  const output = await suitecutEngine.record(page, {
    destination: resolve(artifactDirectory, 'silent-immediate.mkv'),
    audioCodec: 'pcm_s16le',
  })
  await delay(100)
  expect((await output.stop()).error).toBe('')
})
test('network interception, init scripts and storage isolation', async ({ browser }) => {
  const a = await browser.newContext(),
    b = await browser.newContext()
  try {
    await a.addInitScript(() => {
      localStorage.setItem('engine', 'a')
    })
    await a.addCookies([{ name: 'before-page', value: 'isolated', url }])
    expect((await a.cookies(url)).map((c) => c.name)).toContain('before-page')
    expect(await b.cookies(url)).toEqual([])
    const pa = await a.newPage(),
      pb = await b.newPage()
    await pa.route('**/data', (route) => route.fulfill({ json: { value: 'intercepted' } }))
    await pa.goto(url)
    await pb.goto(url)
    expect(await pa.evaluate(() => fetch('/data').then((r) => r.json()))).toEqual({
      value: 'intercepted',
    })
    expect(await pb.evaluate(() => localStorage.getItem('engine'))).toBeNull()
    await a.addCookies([{ name: 'engine', value: 'a', url }])
    expect((await a.cookies(url)).map((c) => c.value)).toContain('a')
    expect(await b.cookies(url)).toEqual([])
    await a.clearCookies()
    expect(await a.cookies(url)).toEqual([])
  } finally {
    await a.close()
    await b.close()
  }
})
test('popup, dialog, file upload, download and public CDP sessions', async ({ page, context }) => {
  await page.goto(url)
  const popup = context.waitForEvent('page')
  await page.getByRole('button', { name: 'Popup' }).click()
  const child = await popup
  await child.waitForLoadState()
  expect(child.url()).toContain('/popup')
  await child.close()
  page.once('dialog', async (dialog) => {
    expect(dialog.message()).toBe('native dialog')
    await dialog.accept()
  })
  await page.getByRole('button', { name: 'Dialog' }).click()
  await page.getByLabel('Upload').setInputFiles({
    name: 'input.txt',
    mimeType: 'text/plain',
    buffer: Buffer.from('native upload'),
  })
  expect(
    await page.getByLabel('Upload').evaluate((input: HTMLInputElement) => input.files?.[0]?.name),
  ).toBe('input.txt')
  const download = page.waitForEvent('download')
  await page.getByRole('link', { name: 'Download' }).click()
  const file = await download
  const path = await file.path()
  expect(file.suggestedFilename()).toBe('native.txt')
  if (!path) throw new Error('Missing download path')
  expect(await readFile(path, 'utf8')).toBe('native download')
  const session = await context.newCDPSession(page)
  expect(
    (await session.send('Runtime.evaluate', { expression: '6*7', returnByValue: true })).result
      .value,
  ).toBe(42)
  await session.detach()
  await expect(session.send('Runtime.evaluate', { expression: '1' })).rejects.toThrow()
})
test('native recording survives blocked Node, pause and page switching', async ({
  suitecutEngine,
  page,
  context,
}) => {
  await page.goto(url)
  await page.waitForTimeout(1000)
  const output = await suitecutEngine.record(page, {
    destination: resolve(artifactDirectory, 'playwright-recording.mp4'),
    width: 640,
    height: 360,
    fps: 30,
  })
  try {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 4000)
    const captured = await output.status()
    expect(captured.videoFrames).toBeGreaterThan(100)
    expect(captured.droppedAudioSamples).toBe(0)
    await output.pause(true)
    const before = await output.status()
    await page.waitForTimeout(500)
    const paused = await output.status()
    expect(paused.videoFrames).toBe(before.videoFrames)
    await output.pause(false)
    const next = await context.newPage()
    await next.goto(url)
    await suitecutEngine.select(output, next)
    await next.waitForTimeout(500)
    await suitecutEngine.select(output, page)
    await next.close()
  } finally {
    const final = await output.stop()
    expect(final.queuedVideoBytes).toBe(0)
    expect(final.queuedAudioBytes).toBe(0)
    expect(await output.stop()).toEqual(final)
  }
})
test('repeated context/page shutdown keeps the engine usable', async ({ browser, page }) => {
  for (let i = 0; i < 10; i++) {
    const context = await browser.newContext()
    const other = await context.newPage()
    await other.goto(url)
    await context.close()
  }
  await page.goto(url)
  await expect(page.getByRole('button', { name: 'Click', exact: true })).toBeVisible()
})

test('permission and geolocation setup before page creation', async ({ browser }) => {
  const context = await browser.newContext({
    geolocation: { latitude: 12.34, longitude: 56.78 },
    permissions: ['geolocation'],
  })
  try {
    const page = await context.newPage()
    await page.goto(url)
    expect(
      await page.evaluate(
        async () => (await navigator.permissions.query({ name: 'geolocation' })).state,
      ),
    ).toBe('granted')
    const location = await page.evaluate(
      () =>
        new Promise<{ latitude: number; longitude: number }>((resolve, reject) => {
          navigator.geolocation.getCurrentPosition(
            (position) =>
              resolve({ latitude: position.coords.latitude, longitude: position.coords.longitude }),
            (error) => reject(new Error(error.message)),
            { timeout: 5000 },
          )
        }),
    )
    expect(location).toEqual({ latitude: 12.34, longitude: 56.78 })
    await context.clearPermissions()
    expect(
      await page.evaluate(
        async () => (await navigator.permissions.query({ name: 'geolocation' })).state,
      ),
    ).toBe('prompt')
  } finally {
    await context.close()
  }
})
