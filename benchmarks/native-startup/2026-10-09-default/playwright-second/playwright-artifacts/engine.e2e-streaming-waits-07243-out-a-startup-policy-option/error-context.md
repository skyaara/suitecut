# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> streaming waits for delayed native sound without a startup-policy option
- Location: benchmarks/native-engine/engine.e2e.spec.ts:96:1

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: false
Received: true
```

# Test source

```ts
  42  |   })
  43  |   await new Promise<void>((r) => server.listen(0, '127.0.0.1', r))
  44  |   const address = server.address()
  45  |   if (!address || typeof address === 'string') throw new Error('Missing address')
  46  |   url = `http://127.0.0.1:${address.port}`
  47  | })
  48  | test.afterAll(async () => {
  49  |   server.closeAllConnections()
  50  |   await new Promise<void>((r) => server.close(() => r()))
  51  | })
  52  | test('real Playwright locators, auto-wait, input, scrolling, iframe and screenshot', async ({
  53  |   page,
  54  | }) => {
  55  |   await page.goto(url)
  56  |   await page.getByRole('button', { name: 'Click', exact: true }).click()
  57  |   await expect(page.locator('output')).toHaveText('clicked')
  58  |   await page.getByPlaceholder('Name').fill('CEF + Playwright')
  59  |   await expect(page.getByPlaceholder('Name')).toHaveValue('CEF + Playwright')
  60  |   await page.getByRole('combobox', { name: 'Choice' }).selectOption('b')
  61  |   await expect(page.getByRole('combobox')).toHaveValue('b')
  62  |   await page.frameLocator('iframe').getByRole('button').click()
  63  |   await page.mouse.move(600, 300)
  64  |   await page.mouse.wheel(0, 500)
  65  |   await expect.poll(() => page.evaluate(() => scrollY)).toBeGreaterThan(100)
  66  |   await page.screenshot({ path: resolve(artifactDirectory, 'playwright.png') })
  67  | })
  68  | test('native audio readiness waits for delayed sound before returning a recording', async ({
  69  |   page,
  70  |   suitecutEngine,
  71  | }) => {
  72  |   await page.goto(`${url}/silent`)
  73  |   let settled = false
  74  |   const recording = suitecutEngine.record(page, {
  75  |     destination: resolve(artifactDirectory, 'audio-readiness.mkv'),
  76  |     audioCodec: 'pcm_s16le',
  77  |   })
  78  |   void recording.then(
  79  |     () => {
  80  |       settled = true
  81  |     },
  82  |     () => {
  83  |       settled = true
  84  |     },
  85  |   )
  86  |   await delay(150)
  87  |   expect(settled).toBe(false)
  88  |   await page.getByRole('button', { name: 'Start audio' }).click()
  89  |   const output = await recording
  90  |   expect((await output.status()).ready).toBe(true)
  91  |   await delay(250)
  92  |   const final = await output.stop()
  93  |   expect(final.error).toBe('')
  94  |   expect(final.audioSamples).toBeGreaterThan(0)
  95  | })
  96  | test('streaming waits for delayed native sound without a startup-policy option', async ({
  97  |   page,
  98  |   suitecutEngine,
  99  | }) => {
  100 |   await page.goto(`${url}/silent`)
  101 |   const probe = socketServer()
  102 |   await new Promise<void>((ready) => probe.listen(0, '127.0.0.1', ready))
  103 |   const address = probe.address()
  104 |   if (!address || typeof address === 'string') throw new Error('Missing RTMP port')
  105 |   await new Promise<void>((ready) => probe.close(() => ready()))
  106 |   const destination = `rtmp://127.0.0.1:${address.port}/live/readiness`
  107 |   const receiver = spawn(
  108 |     'ffmpeg',
  109 |     [
  110 |       '-v',
  111 |       'error',
  112 |       '-y',
  113 |       '-listen',
  114 |       '1',
  115 |       '-i',
  116 |       destination,
  117 |       '-c',
  118 |       'copy',
  119 |       resolve(artifactDirectory, 'delayed-stream.flv'),
  120 |     ],
  121 |     { stdio: ['ignore', 'ignore', 'pipe'] },
  122 |   )
  123 |   let stderr = ''
  124 |   receiver.stderr.on('data', (chunk: Buffer) => {
  125 |     stderr += chunk.toString()
  126 |   })
  127 |   const closed = once(receiver, 'close')
  128 |   void closed.catch(() => undefined)
  129 |   await delay(300)
  130 |   let settled = false
  131 |   const pending = suitecutEngine.stream(page, { destination })
  132 |   void pending.then(
  133 |     () => {
  134 |       settled = true
  135 |     },
  136 |     () => {
  137 |       settled = true
  138 |     },
  139 |   )
  140 |   try {
  141 |     await delay(150)
> 142 |     expect(settled).toBe(false)
      |                     ^ Error: expect(received).toBe(expected) // Object.is equality
  143 |     await page.getByRole('button', { name: 'Start audio' }).click()
  144 |     const output = await pending
  145 |     try {
  146 |       expect((await output.status()).ready).toBe(true)
  147 |       await delay(500)
  148 |     } finally {
  149 |       expect((await output.stop()).audioSamples).toBeGreaterThan(0)
  150 |     }
  151 |   } finally {
  152 |     receiver.kill('SIGINT')
  153 |     await Promise.race([closed, delay(2000)])
  154 |     if (receiver.exitCode === null && receiver.signalCode === null) {
  155 |       receiver.kill('SIGKILL')
  156 |       await closed
  157 |     }
  158 |     await writeFile(resolve(artifactDirectory, 'delayed-stream-receiver.log'), stderr)
  159 |   }
  160 | })
  161 | test('audio readiness times out without sound and leaves the engine usable', async ({
  162 |   page,
  163 |   suitecutEngine,
  164 | }) => {
  165 |   await page.goto(`${url}/silent`)
  166 |   await expect(
  167 |     suitecutEngine.record(page, {
  168 |       destination: resolve(artifactDirectory, 'audio-timeout.mkv'),
  169 |       audioStartupTimeoutMs: 150,
  170 |     }),
  171 |   ).rejects.toThrow('Native audio readiness timed out')
  172 |   const output = await suitecutEngine.record(page, {
  173 |     destination: resolve(artifactDirectory, 'silent-video-only.mkv'),
  174 |     audio: false,
  175 |     audioCodec: 'pcm_s16le',
  176 |   })
  177 |   await delay(100)
  178 |   expect((await output.stop()).error).toBe('')
  179 | })
  180 | test('native protocol rejects bypasses and requires audio readiness when audio is omitted', async ({
  181 |   page,
  182 |   suitecutEngine,
  183 | }) => {
  184 |   await page.goto(`${url}/silent`)
  185 |   const command = suitecutEngine.source.command!.bind(suitecutEngine.source)
  186 |   const config = {
  187 |     destination: resolve(artifactDirectory, 'protocol-timeout.mkv'),
  188 |     width: 640,
  189 |     height: 360,
  190 |     fps: 30,
  191 |     streaming: false,
  192 |     audioStartupTimeoutMs: 150,
  193 |   }
  194 |   for (const audioStartup of ['immediate', 'wait-for-packet', 'wait-for-signal']) {
  195 |     await expect(command('media.start', { ...config, audioStartup })).rejects.toThrow(
  196 |       'Audio startup policies were removed',
  197 |     )
  198 |   }
  199 |   const started = (await command('media.start', config)) as {
  200 |     outputId: number
  201 |     audioReadinessRequired: boolean
  202 |   }
  203 |   expect(started.audioReadinessRequired).toBe(true)
  204 |   try {
  205 |     await expect
  206 |       .poll(async () => {
  207 |         try {
  208 |           const value = (await command('media.status', { outputId: started.outputId })) as {
  209 |             ready: boolean
  210 |           }
  211 |           expect(value.ready).toBe(false)
  212 |           return ''
  213 |         } catch (error) {
  214 |           return error instanceof Error ? error.message : String(error)
  215 |         }
  216 |       })
  217 |       .toContain('Native audio readiness timed out')
  218 |   } finally {
  219 |     await expect(command('media.stop', { outputId: started.outputId })).rejects.toThrow(
  220 |       'Native audio readiness timed out',
  221 |     )
  222 |   }
  223 | })
  224 | test('network interception, init scripts and storage isolation', async ({ browser }) => {
  225 |   const a = await browser.newContext(),
  226 |     b = await browser.newContext()
  227 |   try {
  228 |     await a.addInitScript(() => {
  229 |       localStorage.setItem('engine', 'a')
  230 |     })
  231 |     await a.addCookies([{ name: 'before-page', value: 'isolated', url }])
  232 |     expect((await a.cookies(url)).map((c) => c.name)).toContain('before-page')
  233 |     expect(await b.cookies(url)).toEqual([])
  234 |     const pa = await a.newPage(),
  235 |       pb = await b.newPage()
  236 |     await pa.route('**/data', (route) => route.fulfill({ json: { value: 'intercepted' } }))
  237 |     await pa.goto(url)
  238 |     await pb.goto(url)
  239 |     expect(await pa.evaluate(() => fetch('/data').then((r) => r.json()))).toEqual({
  240 |       value: 'intercepted',
  241 |     })
  242 |     expect(await pb.evaluate(() => localStorage.getItem('engine'))).toBeNull()
```