# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> popup, dialog, file upload, download and public CDP sessions
- Location: benchmarks/native-engine/engine.e2e.spec.ts:295:1

# Error details

```
Error: Protocol error (Page.handleJavaScriptDialog): No dialog is showing
```

```
Error: locator.evaluate: Test ended.
Call log:
  - waiting for getByLabel('Upload')

```

# Test source

```ts
  214 |     audio: false,
  215 |   })
  216 |   await delay(100)
  217 |   expect((await videoOnly.stop()).error).toBe('')
  218 | })
  219 | test('native protocol rejects bypasses and requires audio readiness when audio is omitted', async ({
  220 |   page,
  221 |   suitecutEngine,
  222 | }) => {
  223 |   await page.goto(`${url}/silent`)
  224 |   const source = suitecutEngine.source
  225 |   if (!source.command) throw new Error('Native command adapter is unavailable')
  226 |   const command = source.command.bind(source)
  227 |   const config = {
  228 |     destination: resolve(artifactDirectory, 'protocol-silent.mkv'),
  229 |     width: 640,
  230 |     height: 360,
  231 |     fps: 30,
  232 |     streaming: false,
  233 |     audioStartupTimeoutMs: 150,
  234 |   }
  235 |   for (const audioStartup of ['immediate', 'wait-for-packet', 'wait-for-signal']) {
  236 |     await expect(command('media.start', { ...config, audioStartup })).rejects.toThrow(
  237 |       'Audio startup policies were removed',
  238 |     )
  239 |   }
  240 |   const started = (await command('media.start', config)) as {
  241 |     outputId: number
  242 |     audioReadinessRequired: boolean
  243 |     audioReadiness: string
  244 |   }
  245 |   expect(started.audioReadinessRequired).toBe(true)
  246 |   expect(started.audioReadiness).toBe('engine')
  247 |   try {
  248 |     await expect
  249 |       .poll(async () => {
  250 |         const value = (await command('media.status', { outputId: started.outputId })) as {
  251 |           ready: boolean
  252 |         }
  253 |         return value.ready
  254 |       })
  255 |       .toBe(true)
  256 |     await delay(200)
  257 |   } finally {
  258 |     const final = (await command('media.stop', { outputId: started.outputId })) as {
  259 |       error: string
  260 |       audioSamples: number
  261 |     }
  262 |     expect(final.error).toBe('')
  263 |     expect(final.audioSamples).toBeGreaterThan(0)
  264 |   }
  265 | })
  266 | test('network interception, init scripts and storage isolation', async ({ browser }) => {
  267 |   const a = await browser.newContext(),
  268 |     b = await browser.newContext()
  269 |   try {
  270 |     await a.addInitScript(() => {
  271 |       localStorage.setItem('engine', 'a')
  272 |     })
  273 |     await a.addCookies([{ name: 'before-page', value: 'isolated', url }])
  274 |     expect((await a.cookies(url)).map((c) => c.name)).toContain('before-page')
  275 |     expect(await b.cookies(url)).toEqual([])
  276 |     const pa = await a.newPage(),
  277 |       pb = await b.newPage()
  278 |     await pa.route('**/data', (route) => route.fulfill({ json: { value: 'intercepted' } }))
  279 |     await pa.goto(url)
  280 |     await pb.goto(url)
  281 |     expect(await pa.evaluate(() => fetch('/data').then((r) => r.json()))).toEqual({
  282 |       value: 'intercepted',
  283 |     })
  284 |     expect(await pb.evaluate(() => localStorage.getItem('engine'))).toBeNull()
  285 |     await a.addCookies([{ name: 'engine', value: 'a', url }])
  286 |     expect((await a.cookies(url)).map((c) => c.value)).toContain('a')
  287 |     expect(await b.cookies(url)).toEqual([])
  288 |     await a.clearCookies()
  289 |     expect(await a.cookies(url)).toEqual([])
  290 |   } finally {
  291 |     await a.close()
  292 |     await b.close()
  293 |   }
  294 | })
  295 | test('popup, dialog, file upload, download and public CDP sessions', async ({ page, context }) => {
  296 |   await page.goto(url)
  297 |   const popup = context.waitForEvent('page')
  298 |   await page.getByRole('button', { name: 'Popup' }).click()
  299 |   const child = await popup
  300 |   await child.waitForLoadState()
  301 |   expect(child.url()).toContain('/popup')
  302 |   await child.close()
  303 |   page.once('dialog', async (dialog) => {
  304 |     expect(dialog.message()).toBe('native dialog')
  305 |     await dialog.accept()
  306 |   })
  307 |   await page.getByRole('button', { name: 'Dialog' }).click()
  308 |   await page.getByLabel('Upload').setInputFiles({
  309 |     name: 'input.txt',
  310 |     mimeType: 'text/plain',
  311 |     buffer: Buffer.from('native upload'),
  312 |   })
  313 |   expect(
> 314 |     await page.getByLabel('Upload').evaluate((input: HTMLInputElement) => input.files?.[0]?.name),
      |                                     ^ Error: locator.evaluate: Test ended.
  315 |   ).toBe('input.txt')
  316 |   const download = page.waitForEvent('download')
  317 |   await page.getByRole('link', { name: 'Download' }).click()
  318 |   const file = await download
  319 |   const path = await file.path()
  320 |   expect(file.suggestedFilename()).toBe('native.txt')
  321 |   if (!path) throw new Error('Missing download path')
  322 |   expect(await readFile(path, 'utf8')).toBe('native download')
  323 |   const session = await context.newCDPSession(page)
  324 |   expect(
  325 |     (await session.send('Runtime.evaluate', { expression: '6*7', returnByValue: true })).result
  326 |       .value,
  327 |   ).toBe(42)
  328 |   await session.detach()
  329 |   await expect(session.send('Runtime.evaluate', { expression: '1' })).rejects.toThrow()
  330 | })
  331 | test('explicit CDP sessions receive page events and tracing completion', async ({
  332 |   page,
  333 |   context,
  334 | }) => {
  335 |   await page.goto(url)
  336 |   const session = await context.newCDPSession(page)
  337 |   const messages: string[] = []
  338 |   session.on('Runtime.consoleAPICalled', (event) => {
  339 |     messages.push(...event.args.map((arg) => String(arg.value)))
  340 |   })
  341 |   await session.send('Runtime.enable')
  342 |   await page.evaluate(() => console.log('explicit-session-event'))
  343 |   await expect.poll(() => messages.includes('explicit-session-event')).toBe(true)
  344 |   const other = await context.newPage()
  345 |   await other.goto(url)
  346 |   await other.evaluate(() => console.log('other-page-event'))
  347 |   await page.evaluate(() => console.log('session-barrier'))
  348 |   await expect.poll(() => messages.includes('session-barrier')).toBe(true)
  349 |   expect(messages).not.toContain('other-page-event')
  350 |   await other.close()
  351 |   const completed = new Promise<void>((resolve) => {
  352 |     session.once('Tracing.tracingComplete', () => resolve())
  353 |   })
  354 |   await session.send('Tracing.start', { categories: 'audio', transferMode: 'ReportEvents' })
  355 |   await page.waitForTimeout(250)
  356 |   await session.send('Tracing.end')
  357 |   await completed
  358 |   await session.detach()
  359 |   await expect(session.send('Runtime.evaluate', { expression: '1' })).rejects.toThrow()
  360 |   // The primary Playwright session continues to work after the alias detaches.
  361 |   expect(await page.evaluate(() => 6 * 7)).toBe(42)
  362 | })
  363 | test('native recording survives blocked Node, pause and page switching', async ({
  364 |   suitecutEngine,
  365 |   page,
  366 |   context,
  367 | }) => {
  368 |   await page.goto(url)
  369 |   await page.waitForTimeout(1000)
  370 |   const output = await suitecutEngine.record(page, {
  371 |     destination: resolve(artifactDirectory, 'playwright-recording.mp4'),
  372 |     width: 640,
  373 |     height: 360,
  374 |     fps: 30,
  375 |   })
  376 |   try {
  377 |     Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 4000)
  378 |     const captured = await output.status()
  379 |     expect(captured.videoFrames).toBeGreaterThan(100)
  380 |     expect(captured.droppedAudioSamples).toBe(0)
  381 |     await output.pause(true)
  382 |     const before = await output.status()
  383 |     await page.waitForTimeout(500)
  384 |     const paused = await output.status()
  385 |     expect(paused.videoFrames).toBe(before.videoFrames)
  386 |     await output.pause(false)
  387 |     const next = await context.newPage()
  388 |     await next.goto(url)
  389 |     await suitecutEngine.select(output, next)
  390 |     await next.waitForTimeout(500)
  391 |     await suitecutEngine.select(output, page)
  392 |     await next.close()
  393 |   } finally {
  394 |     const final = await output.stop()
  395 |     expect(final.queuedVideoBytes).toBe(0)
  396 |     expect(final.queuedAudioBytes).toBe(0)
  397 |     expect(await output.stop()).toEqual(final)
  398 |   }
  399 | })
  400 | test('pause drains pending media and resume captures a prepared static page', async ({
  401 |   page,
  402 |   suitecutEngine,
  403 | }) => {
  404 |   await page.goto(`${url}/silent`)
  405 |   await page.evaluate(() => {
  406 |     document.body.style.backgroundColor = 'red'
  407 |   })
  408 |   await delay(200)
  409 |   const output = await suitecutEngine.record(page, {
  410 |     destination: resolve(artifactDirectory, 'paused-static.mkv'),
  411 |     audioCodec: 'pcm_s16le',
  412 |   })
  413 |   await delay(250)
  414 |   await output.pause(true)
```