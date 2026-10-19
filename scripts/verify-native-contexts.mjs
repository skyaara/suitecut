/* global document, localStorage, navigator, window */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable, 'SUITECUT_NATIVE_EXECUTABLE is required')
const directory = resolve('.suitecut/native-contexts')
await mkdir(directory, { recursive: true })
await rm(resolve(directory, 'report.json'), { force: true })
await rm(resolve(directory, 'failure.json'), { force: true })
const diagnostics = []
let diagnosticBytes = 0
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    `<button id="popup" onclick="window.open('/popup')">Open</button><p>${request.headers.cookie ?? ''}</p>`,
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const baseURL = `http://127.0.0.1:${server.address().port}`
const started = new Date().toISOString()
const executableSha256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
let source
let phase = 'launch'
let capturedError = null
try {
  source = await launchNativeBrowser({
    executablePath: resolve(executable),
    headless: true,
    width: 640,
    height: 480,
    onDiagnostic(message) {
      if (diagnosticBytes >= 1048576) return
      const bounded = message.slice(0, 1048576 - diagnosticBytes)
      diagnostics.push(bounded)
      diagnosticBytes += Buffer.byteLength(bounded)
    },
  })
  assert(
    typeof source.runContext === 'function' &&
      typeof source.pageSource === 'function' &&
      typeof source.onContextEvent === 'function',
    'Native context transport unavailable',
  )
  phase = 'native context creation'
  const browser = new NativeBrowserContexts(source)
  const a = await browser.newContext()
  const b = await browser.newContext()
  const pageA = await a.newPage()
  const pageB = await b.newPage()
  await pageA.goto(baseURL)
  await pageB.goto(baseURL)
  phase = 'native cookies and storage isolation'
  await a.addCookies([{ name: 'isolated', value: 'a', url: baseURL }])
  assert.equal(await pageA.evaluate(() => document.cookie), 'isolated=a')
  assert.equal(await pageB.evaluate(() => document.cookie), '')
  phase = 'native permission state and isolation'
  await a.grantPermissions(['notifications'], { origin: baseURL })
  assert.equal(
    await pageA.evaluate(
      async () => (await navigator.permissions.query({ name: 'notifications' })).state,
    ),
    'granted',
  )
  assert.equal(
    await pageB.evaluate(
      async () => (await navigator.permissions.query({ name: 'notifications' })).state,
    ),
    'prompt',
  )
  await a.clearPermissions()
  assert.equal(
    await pageA.evaluate(
      async () => (await navigator.permissions.query({ name: 'notifications' })).state,
    ),
    'prompt',
  )
  await pageA.evaluate(() => localStorage.setItem('isolation', 'a'))
  assert.equal(await pageB.evaluate(() => localStorage.getItem('isolation')), null)
  assert.equal((await a.cookies(baseURL))[0].value, 'a')
  assert.deepEqual(await b.cookies(), [])
  phase = 'distinct native frame delivery from two pages'
  const colors = [
    [210, 20, 30],
    [15, 80, 220],
  ]
  const framePromises = [pageA, pageB].map(
    (page, index) =>
      new Promise((done, reject) => {
        const color = colors[index]
        assert(color)
        const timeout = AbortSignal.timeout(10000)
        let detach = () => undefined
        let settled = false
        const cleanup = () => {
          timeout.removeEventListener('abort', expired)
          detach()
        }
        const expired = () => {
          cleanup()
          reject(new Error('Distinct native page frame timed out'))
        }
        timeout.addEventListener('abort', expired, { once: true })
        detach = page.source.onFrame((frame) => {
          if (
            frame.pixelFormat !== 'bgra' ||
            frame.data[0] !== color[2] ||
            frame.data[1] !== color[1] ||
            frame.data[2] !== color[0] ||
            frame.data[3] !== 255
          )
            return
          settled = true
          cleanup()
          done()
        })
        if (settled) detach()
      }),
  )
  await Promise.all(
    [pageA, pageB].map((page, index) =>
      page.evaluate((color) => {
        document.documentElement.style.cssText = 'margin:0;width:100%;height:100%'
        document.body.style.cssText = `margin:0;width:100%;height:100%;background:rgb(${color.join(',')})`
      }, colors[index]),
    ),
  )
  await Promise.all(framePromises)
  phase = 'native popup event and initial load'
  const popupReady = pageA.waitForEvent('popup', { timeout: 10000 })
  await pageA.locator('#popup').click()
  const popup = await popupReady
  await popup.waitForLoadState('load')
  assert.equal(await popup.opener(), pageA)
  assert.equal(await popup.evaluate(() => !!window.opener), true)
  assert.equal(await popup.evaluate(() => document.cookie), 'isolated=a')
  await popup.evaluate('window.opener.document.body.dataset.popup = "real-opener"')
  assert.equal(await pageA.evaluate(() => document.body.dataset.popup), 'real-opener')
  await popup.close()
  assert(popup.isClosed())
  assert.equal(a.pages().length, 1)
  await assert.rejects(pageA.waitForEvent('popup', { timeout: 20 }), /timed out/)
  const afterTimeout = z
    .object({ pendingEventWaits: z.number() })
    .parse(await source.runContext('list'))
  assert.equal(afterTimeout.pendingEventWaits, 0)
  await a.clearCookies({ name: 'isolated' })
  assert.deepEqual(await a.cookies(), [])
  phase = '100 native context/page lifecycle cycles'
  for (let iteration = 0; iteration < 100; iteration++) {
    const context = await browser.newContext()
    const page = await context.newPage()
    await page.evaluate(() => (document.body.textContent = 'churn'))
    await context.close()
    assert(page.isClosed())
  }
  const closed = pageA.waitForEvent('close', { timeout: 10000 })
  await a.close()
  assert.equal(await closed, pageA)
  assert(pageA.isClosed())
  assert.equal(await pageB.evaluate(() => document.body.tagName), 'BODY')
  await b.close()
  assert.equal(browser.contexts().length, 1)
  phase = 'native owning-page context teardown'
  const owned = await browser.newPage({ baseURL })
  const ownedContext = owned.context()
  await owned.goto('/owned')
  assert.equal(owned.url(), baseURL + '/owned')
  await owned.close()
  assert(owned.isClosed())
  assert(!browser.contexts().includes(ownedContext))
  phase = 'default page closure and process survival'
  const defaultPage = browser.defaultContext().pages()[0]
  assert(defaultPage)
  await defaultPage.close()
  assert(defaultPage.isClosed())
  assert.equal(source.state, 'running')
  assert.equal(source.signal.aborted, false)
  const survivor = await browser.newContext()
  const survivorPage = await survivor.newPage()
  assert.equal(await survivorPage.evaluate(() => document.body.tagName), 'BODY')
  await survivor.close()
  const native = z
    .object({
      contexts: z.array(z.object({ contextId: z.number() })),
      pageCount: z.number(),
      pendingEventWaits: z.number(),
    })
    .parse(await source.runContext('list'))
  assert.equal(native.contexts.length, 1)
  assert.equal(native.pageCount, 0)
  assert.equal(native.pendingEventWaits, 0)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    executableSha256,
    'Native executable changed during verification',
  )
  phase = 'graceful native process shutdown'
  await source.close()
  assert.equal(source.processExit?.()?.code, 0)
  assert.equal(source.processExit?.()?.signal, null)
  assert(
    !diagnostics.join('').includes('stack smashing detected'),
    'A native CEF process reported stack corruption during teardown',
  )
  const report = {
    started,
    completed: new Date().toISOString(),
    passed: true,
    executable: resolve(executable),
    executableSha256,
    cefVersion: source.cefVersion,
    contextsCreatedAndClosed: 104,
    realPopup: true,
    realOpenerReference: true,
    processSurvivesDefaultPageClosure: true,
    cookieAndLocalStorageIsolation: true,
    nativePermissionStateAndIsolation: true,
    distinctNativePageFrames: true,
    nativeOwnedPageContextTeardown: true,
    processExit: source.processExit?.(),
    finalContextCount: native.contexts.length,
    finalPageCount: native.pageCount,
    finalNativeEventWaitCount: native.pendingEventWaits,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} catch (error) {
  capturedError = error instanceof Error ? error : new Error(String(error))
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        started,
        completed: new Date().toISOString(),
        passed: false,
        phase,
        executableSha256,
        error: error instanceof Error ? error.message : String(error),
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  const results = await Promise.allSettled([
    source?.close(),
    new Promise((done) => server.close(done)),
  ])
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join(''))
  for (const result of results)
    if (result.status === 'rejected' && capturedError === null)
      capturedError =
        result.reason instanceof Error ? result.reason : new Error(String(result.reason))
}

if (capturedError) throw capturedError
