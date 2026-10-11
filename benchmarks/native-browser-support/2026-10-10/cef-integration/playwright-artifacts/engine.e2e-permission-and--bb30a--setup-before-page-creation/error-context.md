# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> permission and geolocation setup before page creation
- Location: benchmarks/native-engine/engine.e2e.spec.ts:434:1

# Error details

```
Error: browserContext.newPage: Protocol error (Browser.setWindowBounds): Window bounds exceed native capture dimensions
```

# Test source

```ts
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
  415 |   await page.evaluate(() => {
  416 |     document.body.style.backgroundColor = 'blue'
  417 |   })
  418 |   await delay(200)
  419 |   await output.pause(false)
  420 |   await delay(250)
  421 |   expect((await output.stop()).error).toBe('')
  422 | })
  423 | test('repeated context/page shutdown keeps the engine usable', async ({ browser, page }) => {
  424 |   for (let i = 0; i < 10; i++) {
  425 |     const context = await browser.newContext()
  426 |     const other = await context.newPage()
  427 |     await other.goto(url)
  428 |     await context.close()
  429 |   }
  430 |   await page.goto(url)
  431 |   await expect(page.getByRole('button', { name: 'Click', exact: true })).toBeVisible()
  432 | })
  433 | 
  434 | test('permission and geolocation setup before page creation', async ({ browser }) => {
  435 |   const context = await browser.newContext({
  436 |     geolocation: { latitude: 12.34, longitude: 56.78 },
  437 |     permissions: ['geolocation'],
  438 |   })
  439 |   try {
> 440 |     const page = await context.newPage()
      |                                ^ Error: browserContext.newPage: Protocol error (Browser.setWindowBounds): Window bounds exceed native capture dimensions
  441 |     await page.goto(url)
  442 |     expect(
  443 |       await page.evaluate(
  444 |         async () => (await navigator.permissions.query({ name: 'geolocation' })).state,
  445 |       ),
  446 |     ).toBe('granted')
  447 |     const location = await page.evaluate(
  448 |       () =>
  449 |         new Promise<{ latitude: number; longitude: number }>((resolve, reject) => {
  450 |           navigator.geolocation.getCurrentPosition(
  451 |             (position) =>
  452 |               resolve({ latitude: position.coords.latitude, longitude: position.coords.longitude }),
  453 |             (error) => reject(new Error(error.message)),
  454 |             { timeout: 5000 },
  455 |           )
  456 |         }),
  457 |     )
  458 |     expect(location).toEqual({ latitude: 12.34, longitude: 56.78 })
  459 |     await context.clearPermissions()
  460 |     expect(
  461 |       await page.evaluate(
  462 |         async () => (await navigator.permissions.query({ name: 'geolocation' })).state,
  463 |       ),
  464 |     ).toBe('prompt')
  465 |   } finally {
  466 |     await context.close()
  467 |   }
  468 | })
  469 | 
  470 | test('explicit session retention is bounded and capacity returns after detach', async ({
  471 |   browser,
  472 |   page,
  473 |   context,
  474 | }) => {
  475 |   const pageSession = await context.newCDPSession(page)
  476 |   const { targetInfo } = await pageSession.send('Target.getTargetInfo')
  477 |   await pageSession.detach()
  478 |   const rootSession = await browser.newBrowserCDPSession()
  479 |   const sessions: string[] = []
  480 |   try {
  481 |     for (let i = 0; i < 256; ++i) {
  482 |       const { sessionId } = await rootSession.send('Target.attachToTarget', {
  483 |         targetId: targetInfo.targetId,
  484 |         flatten: true,
  485 |       })
  486 |       sessions.push(sessionId)
  487 |     }
  488 |     await expect(
  489 |       rootSession.send('Target.attachToTarget', { targetId: targetInfo.targetId, flatten: true }),
  490 |     ).rejects.toThrow('CEF session limit exceeded')
  491 |   } finally {
  492 |     for (const sessionId of sessions)
  493 |       await rootSession.send('Target.detachFromTarget', { sessionId })
  494 |   }
  495 |   const { sessionId } = await rootSession.send('Target.attachToTarget', {
  496 |     targetId: targetInfo.targetId,
  497 |     flatten: true,
  498 |   })
  499 |   await rootSession.send('Target.detachFromTarget', { sessionId })
  500 |   await rootSession.detach()
  501 |   await expect(page.locator('body')).toBeVisible()
  502 | })
  503 | 
  504 | test('rapid popup and opener closure leaves the engine usable', async ({ browser, page }) => {
  505 |   for (let i = 0; i < 20; ++i) {
  506 |     const context = await browser.newContext()
  507 |     const opener = await context.newPage()
  508 |     await opener.goto(url)
  509 |     await opener.evaluate(() => {
  510 |       window.open('about:blank')
  511 |       window.open('about:blank')
  512 |     })
  513 |     await context.close()
  514 |   }
  515 |   await page.goto(url)
  516 |   await expect(page.getByRole('button', { name: 'Click', exact: true })).toBeVisible()
  517 | })
  518 | 
  519 | test('native shutdown joins a pending pause and releases active outputs', async () => {
  520 |   const engine = await launchEngine({
  521 |     executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE ?? '',
  522 |     width: 640,
  523 |     height: 360,
  524 |     framesPerSecond: 30,
  525 |   })
  526 |   try {
  527 |     const page = engine.browser.contexts()[0]?.pages()[0]
  528 |     if (!page) throw new Error('Missing primary CEF page')
  529 |     await page.goto(url)
  530 |     const output = await engine.record(page, {
  531 |       destination: resolve(artifactDirectory, 'pending-pause-close.mp4'),
  532 |     })
  533 |     const pending = output.pause(true)
  534 |     void pending.catch(() => undefined)
  535 |     const began = performance.now()
  536 |     await engine.close()
  537 |     await Promise.allSettled([pending])
  538 |     expect(performance.now() - began).toBeLessThan(5000)
  539 |     expect(engine.source.state).toBe('closed')
  540 |   } finally {
```