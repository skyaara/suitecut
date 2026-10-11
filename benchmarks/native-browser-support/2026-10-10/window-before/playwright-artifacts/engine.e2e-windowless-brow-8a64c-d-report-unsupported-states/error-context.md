# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> windowless browser bounds resize the selected page and report unsupported states
- Location: benchmarks/native-engine/engine.e2e.spec.ts:69:1

# Error details

```
Error: expect(received).not.toBe(expected) // Object.is equality

Expected: not 1
```

# Test source

```ts
  1   | import { spawn } from 'node:child_process'
  2   | import { once } from 'node:events'
  3   | import { readFile, writeFile } from 'node:fs/promises'
  4   | import { createServer } from 'node:http'
  5   | import { createServer as socketServer } from 'node:net'
  6   | import { resolve } from 'node:path'
  7   | import { setTimeout as delay } from 'node:timers/promises'
  8   | 
  9   | import { test, expect } from '../../src/engine-test.js'
  10  | import { launchEngine } from '../../src/engine.js'
  11  | 
  12  | let server: ReturnType<typeof createServer>, url: string
  13  | const artifactDirectory = resolve(
  14  |   process.env.SUITECUT_ENGINE_E2E_DIRECTORY ?? 'benchmarks/native-engine/2026-10-09',
  15  | )
  16  | const html = `<!doctype html><style>body{margin:0;height:2000px}canvas{position:fixed;left:0;top:120px}</style>
  17  | <button onclick="document.querySelector('output').textContent='clicked'">Click</button><input placeholder="Name"><output></output>
  18  | <select aria-label="Choice"><option value="a">Alpha</option><option value="b">Beta</option></select><input type="file" aria-label="Upload">
  19  | <a href="/download" download>Download</a><button onclick="window.open('/popup')">Popup</button><button onclick="alert('native dialog')">Dialog</button>
  20  | <iframe title="Inner" srcdoc="<button>Inner button</button>"></iframe><canvas width="640" height="180"></canvas>
  21  | <script>const c=document.querySelector('canvas').getContext('2d');let n=0;function frame(){c.fillStyle='rgb('+(n++%200)+',50,90)';c.fillRect(0,0,640,180);requestAnimationFrame(frame)}frame();const a=new AudioContext();const o=a.createOscillator();const g=a.createGain();g.gain.value=.15;o.connect(g).connect(a.destination);o.start()</script>`
  22  | test.beforeAll(async () => {
  23  |   server = createServer((req, res) => {
  24  |     if (req.url === '/silent') {
  25  |       res.setHeader('content-type', 'text/html')
  26  |       res.end(
  27  |         `<h1>Delayed audio</h1><button onclick="const a=new AudioContext({sampleRate:48000});const o=a.createOscillator();const g=a.createGain();g.gain.value=.08;o.connect(g).connect(a.destination);o.start();a.resume();window.audio=a">Start audio</button>`,
  28  |       )
  29  |       return
  30  |     }
  31  |     if (req.url === '/download') {
  32  |       res.setHeader('content-disposition', 'attachment; filename="native.txt"')
  33  |       res.end('native download')
  34  |       return
  35  |     }
  36  |     if (req.url === '/data') {
  37  |       res.setHeader('content-type', 'application/json')
  38  |       res.end('{"value":"server"}')
  39  |       return
  40  |     }
  41  |     res.setHeader('content-type', 'text/html')
  42  |     res.end(html)
  43  |   })
  44  |   await new Promise<void>((r) => server.listen(0, '127.0.0.1', r))
  45  |   const address = server.address()
  46  |   if (!address || typeof address === 'string') throw new Error('Missing address')
  47  |   url = `http://127.0.0.1:${address.port}`
  48  | })
  49  | test.afterAll(async () => {
  50  |   server.closeAllConnections()
  51  |   await new Promise<void>((r) => server.close(() => r()))
  52  | })
  53  | test('real Playwright locators, auto-wait, input, scrolling, iframe and screenshot', async ({
  54  |   page,
  55  | }) => {
  56  |   await page.goto(url)
  57  |   await page.getByRole('button', { name: 'Click', exact: true }).click()
  58  |   await expect(page.locator('output')).toHaveText('clicked')
  59  |   await page.getByPlaceholder('Name').fill('CEF + Playwright')
  60  |   await expect(page.getByPlaceholder('Name')).toHaveValue('CEF + Playwright')
  61  |   await page.getByRole('combobox', { name: 'Choice' }).selectOption('b')
  62  |   await expect(page.getByRole('combobox')).toHaveValue('b')
  63  |   await page.frameLocator('iframe').getByRole('button').click()
  64  |   await page.mouse.move(600, 300)
  65  |   await page.mouse.wheel(0, 500)
  66  |   await expect.poll(() => page.evaluate(() => scrollY)).toBeGreaterThan(100)
  67  |   await page.screenshot({ path: resolve(artifactDirectory, 'playwright.png') })
  68  | })
  69  | test('windowless browser bounds resize the selected page and report unsupported states', async ({
  70  |   browser,
  71  | }) => {
  72  |   const context = await browser.newContext({ viewport: null })
  73  |   try {
  74  |     const first = await context.newPage(),
  75  |       second = await context.newPage()
  76  |     await first.goto(url)
  77  |     await second.goto(url)
  78  |     const a = await context.newCDPSession(first),
  79  |       b = await context.newCDPSession(second)
  80  |     const firstWindow = await a.send('Browser.getWindowForTarget')
  81  |     const secondWindow = await b.send('Browser.getWindowForTarget')
> 82  |     expect(firstWindow.windowId).not.toBe(secondWindow.windowId)
      |                                      ^ Error: expect(received).not.toBe(expected) // Object.is equality
  83  |     await a.send('Browser.setWindowBounds', {
  84  |       windowId: firstWindow.windowId,
  85  |       bounds: { width: 800, height: 450 },
  86  |     })
  87  |     await expect
  88  |       .poll(() => first.evaluate(() => ({ width: innerWidth, height: innerHeight })))
  89  |       .toEqual({ width: 800, height: 450 })
  90  |     expect(await second.evaluate(() => ({ width: innerWidth, height: innerHeight }))).toEqual({
  91  |       width: 640,
  92  |       height: 360,
  93  |     })
  94  |     expect(
  95  |       (await a.send('Browser.getWindowBounds', { windowId: firstWindow.windowId })).bounds,
  96  |     ).toMatchObject({ width: 800, height: 450, windowState: 'normal' })
  97  |     expect((await first.screenshot()).readUInt32BE(16)).toBe(800)
  98  |     await expect(
  99  |       a.send('Browser.setWindowBounds', {
  100 |         windowId: firstWindow.windowId,
  101 |         bounds: { windowState: 'maximized' },
  102 |       }),
  103 |     ).rejects.toThrow('Windowless CEF')
  104 |     await expect(
  105 |       a.send('Browser.setWindowBounds', {
  106 |         windowId: firstWindow.windowId,
  107 |         bounds: { width: 99999 },
  108 |       }),
  109 |     ).rejects.toThrow('capture dimensions')
  110 |     await expect(
  111 |       a.send('Browser.getWindowForTarget', { targetId: 'missing-target' }),
  112 |     ).rejects.toThrow('Unknown CEF window')
  113 |   } finally {
  114 |     await context.close()
  115 |   }
  116 | })
  117 | test('native engine records silent startup and accepts later sound', async ({
  118 |   page,
  119 |   suitecutEngine,
  120 | }) => {
  121 |   await page.goto(`${url}/silent`)
  122 |   const output = await suitecutEngine.record(page, {
  123 |     destination: resolve(artifactDirectory, 'audio-readiness.mkv'),
  124 |     audioCodec: 'pcm_s16le',
  125 |   })
  126 |   expect((await output.status()).ready).toBe(true)
  127 |   await delay(250)
  128 |   await page.getByRole('button', { name: 'Start audio' }).click()
  129 |   await delay(250)
  130 |   const final = await output.stop()
  131 |   expect(final.error).toBe('')
  132 |   expect(final.audioSamples).toBeGreaterThan(24000)
  133 | })
  134 | test('streaming initializes silent audio and accepts later browser sound', async ({
  135 |   page,
  136 |   suitecutEngine,
  137 | }) => {
  138 |   await page.goto(`${url}/silent`)
  139 |   const probe = socketServer()
  140 |   await new Promise<void>((ready) => probe.listen(0, '127.0.0.1', ready))
  141 |   const address = probe.address()
  142 |   if (!address || typeof address === 'string') throw new Error('Missing RTMP port')
  143 |   await new Promise<void>((ready) => probe.close(() => ready()))
  144 |   const destination = `rtmp://127.0.0.1:${address.port}/live/readiness`
  145 |   const receiver = spawn(
  146 |     'ffmpeg',
  147 |     [
  148 |       '-v',
  149 |       'error',
  150 |       '-y',
  151 |       '-listen',
  152 |       '1',
  153 |       '-i',
  154 |       destination,
  155 |       '-c',
  156 |       'copy',
  157 |       resolve(artifactDirectory, 'delayed-stream.flv'),
  158 |     ],
  159 |     { stdio: ['ignore', 'ignore', 'pipe'] },
  160 |   )
  161 |   let stderr = ''
  162 |   receiver.stderr.on('data', (chunk: Buffer) => {
  163 |     stderr += chunk.toString()
  164 |   })
  165 |   const closed = once(receiver, 'close')
  166 |   void closed.catch(() => undefined)
  167 |   try {
  168 |     const portHex = address.port.toString(16).toUpperCase().padStart(4, '0')
  169 |     await expect
  170 |       .poll(async () => {
  171 |         const sockets = await readFile('/proc/net/tcp', 'utf8')
  172 |         return sockets.split('\n').some((line) => {
  173 |           const fields = line.trim().split(/\s+/)
  174 |           return fields[1]?.endsWith(`:${portHex}`) && fields[3] === '0A'
  175 |         })
  176 |       })
  177 |       .toBe(true)
  178 |     const output = await suitecutEngine.stream(page, { destination })
  179 |     await delay(250)
  180 |     await page.getByRole('button', { name: 'Start audio' }).click()
  181 |     try {
  182 |       expect((await output.status()).ready).toBe(true)
```