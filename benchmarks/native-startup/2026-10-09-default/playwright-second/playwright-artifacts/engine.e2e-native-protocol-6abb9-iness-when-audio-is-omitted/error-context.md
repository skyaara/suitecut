# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> native protocol rejects bypasses and requires audio readiness when audio is omitted
- Location: benchmarks/native-engine/engine.e2e.spec.ts:180:1

# Error details

```
Error: expect(received).toContain(expected) // indexOf

Expected substring: "Native audio readiness timed out"
Received string:    ""

Call Log:
- Timeout 5000ms exceeded while waiting on the predicate
```

# Test source

```ts
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
  142 |     expect(settled).toBe(false)
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
> 217 |       .toContain('Native audio readiness timed out')
      |        ^ Error: expect(received).toContain(expected) // indexOf
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
  243 |     await a.addCookies([{ name: 'engine', value: 'a', url }])
  244 |     expect((await a.cookies(url)).map((c) => c.value)).toContain('a')
  245 |     expect(await b.cookies(url)).toEqual([])
  246 |     await a.clearCookies()
  247 |     expect(await a.cookies(url)).toEqual([])
  248 |   } finally {
  249 |     await a.close()
  250 |     await b.close()
  251 |   }
  252 | })
  253 | test('popup, dialog, file upload, download and public CDP sessions', async ({ page, context }) => {
  254 |   await page.goto(url)
  255 |   const popup = context.waitForEvent('page')
  256 |   await page.getByRole('button', { name: 'Popup' }).click()
  257 |   const child = await popup
  258 |   await child.waitForLoadState()
  259 |   expect(child.url()).toContain('/popup')
  260 |   await child.close()
  261 |   page.once('dialog', async (dialog) => {
  262 |     expect(dialog.message()).toBe('native dialog')
  263 |     await dialog.accept()
  264 |   })
  265 |   await page.getByRole('button', { name: 'Dialog' }).click()
  266 |   await page.getByLabel('Upload').setInputFiles({
  267 |     name: 'input.txt',
  268 |     mimeType: 'text/plain',
  269 |     buffer: Buffer.from('native upload'),
  270 |   })
  271 |   expect(
  272 |     await page.getByLabel('Upload').evaluate((input: HTMLInputElement) => input.files?.[0]?.name),
  273 |   ).toBe('input.txt')
  274 |   const download = page.waitForEvent('download')
  275 |   await page.getByRole('link', { name: 'Download' }).click()
  276 |   const file = await download
  277 |   const path = await file.path()
  278 |   expect(file.suggestedFilename()).toBe('native.txt')
  279 |   if (!path) throw new Error('Missing download path')
  280 |   expect(await readFile(path, 'utf8')).toBe('native download')
  281 |   const session = await context.newCDPSession(page)
  282 |   expect(
  283 |     (await session.send('Runtime.evaluate', { expression: '6*7', returnByValue: true })).result
  284 |       .value,
  285 |   ).toBe(42)
  286 |   await session.detach()
  287 |   await expect(session.send('Runtime.evaluate', { expression: '1' })).rejects.toThrow()
  288 | })
  289 | test('native recording survives blocked Node, pause and page switching', async ({
  290 |   suitecutEngine,
  291 |   page,
  292 |   context,
  293 | }) => {
  294 |   await page.goto(url)
  295 |   await page.waitForTimeout(1000)
  296 |   const output = await suitecutEngine.record(page, {
  297 |     destination: resolve(artifactDirectory, 'playwright-recording.mp4'),
  298 |     width: 640,
  299 |     height: 360,
  300 |     fps: 30,
  301 |   })
  302 |   try {
  303 |     Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 4000)
  304 |     const captured = await output.status()
  305 |     expect(captured.videoFrames).toBeGreaterThan(100)
  306 |     expect(captured.droppedAudioSamples).toBe(0)
  307 |     await output.pause(true)
  308 |     const before = await output.status()
  309 |     await page.waitForTimeout(500)
  310 |     const paused = await output.status()
  311 |     expect(paused.videoFrames).toBe(before.videoFrames)
  312 |     await output.pause(false)
  313 |     const next = await context.newPage()
  314 |     await next.goto(url)
  315 |     await suitecutEngine.select(output, next)
  316 |     await next.waitForTimeout(500)
  317 |     await suitecutEngine.select(output, page)
```