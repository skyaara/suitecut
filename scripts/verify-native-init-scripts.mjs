/* global document, window */
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
const directory = resolve('.suitecut/native-init-scripts')
await mkdir(directory, { recursive: true })
await rm(resolve(directory, 'report.json'), { force: true })
await rm(resolve(directory, 'failure.json'), { force: true })
const diagnostics = []
let diagnosticBytes = 0
let phase = 'launch'
let source
let capturedError = null
const server = createServer((request, response) => {
  if (request.url === '/worker.js') {
    response.setHeader('Content-Type', 'application/javascript')
    response.end("postMessage('native-worker-ready')")
    return
  }
  if (request.url?.startsWith('/first-subresource')) {
    response.writeHead(204)
    response.end()
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.setHeader(
    'Content-Security-Policy',
    "script-src 'unsafe-inline'; connect-src 'none'; frame-src *; worker-src 'self'",
  )
  response.end(`<!doctype html><html><head><script>
    document.documentElement.dataset.firstCount = String(window.nativeInitCount ?? 'missing');
    document.documentElement.dataset.firstSecond = String(window.nativeInitSecond ?? 'missing');
    const firstImage = new Image(); firstImage.src = '/first-subresource?document=' + encodeURIComponent(location.pathname);
    </script></head><body><p>${request.url}</p><button id="popup" onclick="window.open('/popup')">Popup</button></body></html>`)
})
await new Promise((done) => server.listen(0, '0.0.0.0', done))
const address = server.address()
assert(address && typeof address === 'object')
const url = `http://127.0.0.1:${address.port}`
const crossURL = `http://localhost:${address.port}`
const started = new Date().toISOString()
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const executableSha256 = await hash()
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
  const browser = new NativeBrowserContexts(source)
  const a = await browser.newContext()
  const b = await browser.newContext()
  phase = 'initial blank page and context isolation'
  await a.addInitScript('window.nativeInitCount = (window.nativeInitCount || 0) + 1;')
  const page = await a.newPage()
  assert.equal(await page.evaluate('window.nativeInitCount'), 1)
  const isolated = await b.newPage()
  assert.equal(await isolated.evaluate('window.nativeInitCount'), undefined)
  phase = 'before first author script'
  await page.goto(url)
  assert.equal(await page.locator('html').getAttribute('data-first-count'), '1')
  phase = 'registration leaves current document untouched'
  await a.addInitScript('window.nativeInitSecond = "second";')
  assert.equal(await page.evaluate('window.nativeInitSecond'), undefined)
  const later = await a.newPage()
  assert.equal(await later.evaluate('window.nativeInitCount'), 1)
  assert.equal(await later.evaluate('window.nativeInitSecond'), 'second')
  phase = 'cross-site document inherits the latest registry'
  page.on('request', (request) => assert(!request.url().startsWith('suitecut-init:')))
  const routed = new Set(z.array(z.string()).parse([]))
  if (!process.env.SUITECUT_INIT_NO_ROUTE)
    await page.route('**/*', (route) => {
      routed.add(route.request().url())
      return route.continue()
    })
  await page.goto(crossURL)
  assert.equal(await page.locator('html').getAttribute('data-first-count'), '1')
  assert.equal(await page.locator('html').getAttribute('data-first-second'), 'second')
  phase = 'same-site and cross-site iframe initial scripts'
  const firstSubresources = Promise.allSettled(
    ['/same-frame', '/cross-frame'].map((suffix) =>
      page.waitForResponse(
        (response) => {
          const url = new URL(response.url())
          return (
            url.pathname === '/first-subresource' && url.searchParams.get('document') === suffix
          )
        },
        { timeout: 10000 },
      ),
    ),
  )
  await page.evaluate(
    ({ same, cross }) => {
      for (const url of [same, cross]) {
        const frame = document.createElement('iframe')
        frame.src = url
        document.body.append(frame)
      }
    },
    { same: `${crossURL}/same-frame`, cross: `${url}/cross-frame` },
  )
  for (const suffix of ['/same-frame', '/cross-frame']) {
    await page
      .frameLocator(`iframe[src$="${suffix}"]`)
      .locator('html[data-first-count="1"][data-first-second="second"]')
      .waitFor()
  }
  for (const result of await firstSubresources)
    if (result.status === 'rejected')
      throw result.reason instanceof Error ? result.reason : new Error(String(result.reason))
  if (!process.env.SUITECUT_INIT_NO_ROUTE)
    for (const suffix of ['/same-frame', '/cross-frame'])
      assert(
        [...routed].some((value) => new URL(value).searchParams.get('document') === suffix),
        `First author subresource did not inherit native routes for ${suffix}`,
      )
  phase = 'ignored native worker targets resume'
  assert.equal(
    await page.evaluate(
      "new Promise((resolve,reject)=>{const worker=new Worker('/worker.js');worker.onmessage=event=>{worker.terminate();resolve(event.data);};worker.onerror=event=>{worker.terminate();reject(Error(event.message));};})",
    ),
    'native-worker-ready',
  )
  phase = 'native popup inheritance before author script'
  const popupWait = page.waitForEvent('popup', { timeout: 10000 })
  await page.locator('#popup').click()
  const popup = await popupWait
  await popup.waitForLoadState('load')
  assert.equal(await popup.locator('html').getAttribute('data-first-count'), '1')
  assert.equal(await popup.locator('html').getAttribute('data-first-second'), 'second')
  phase = 'function arguments use native value serialization'
  await a.addInitScript(
    /**
     * Assign the serialized initialization argument.
     * @param {{date: Date, number: bigint}} arg The initialization argument.
     */
    (arg) => {
      window.nativeInitArgument = `${arg.date.toISOString()}:${arg.number}`
    },
    { date: new Date('2026-01-02T03:04:05Z'), number: 42n },
  )
  const argumentPage = await a.newPage()
  assert.equal(
    await argumentPage.evaluate('window.nativeInitArgument'),
    '2026-01-02T03:04:05.000Z:42',
  )
  phase = 'registration/frame/context lifecycle churn'
  for (let index = 0; index < 30; index++) {
    const context = await browser.newContext()
    await context.addInitScript(`window.nativeInitCycle = ${index};`)
    const child = await context.newPage()
    assert.equal(await child.evaluate('window.nativeInitCycle'), index)
    await context.addInitScript('window.nativeInitLate = true;')
    await context.close()
  }
  await Promise.all([a.close(), b.close()])
  const state = z
    .object({
      pendingInitScriptRegistrations: z.number(),
      pendingInitScriptAcks: z.number(),
      pendingInitScriptBytes: z.number(),
      initSnapshotLeases: z.number(),
      initSnapshotLeaseBytes: z.number(),
    })
    .parse(await source.runContext('list'))
  assert.deepEqual(state, {
    pendingInitScriptRegistrations: 0,
    pendingInitScriptAcks: 0,
    pendingInitScriptBytes: 0,
    initSnapshotLeases: 0,
    initSnapshotLeaseBytes: 0,
  })
  await source.close()
  assert.deepEqual(source.processExit(), { code: 0, signal: null })
  assert.doesNotMatch(diagnostics.join(''), /stack smashing detected|Check failed:/)
  assert.equal(await hash(), executableSha256)
} catch (error) {
  capturedError = error instanceof Error ? error : new Error(String(error))
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ phase, error: String(error) }, null, 2),
  )
} finally {
  const cleanup = await Promise.allSettled([
    source?.close(),
    new Promise((done, reject) => server.close((error) => (error ? reject(error) : done()))),
  ])
  const failure = cleanup.find((result) => result.status === 'rejected')
  if (!capturedError && failure?.status === 'rejected')
    capturedError = new Error(String(failure.reason))
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join(''))
}
if (capturedError) throw capturedError
const report = {
  started,
  completed: new Date().toISOString(),
  executableSha256,
  evidence: [
    'blank page',
    'first author script',
    'context isolation',
    'late registration',
    'cross-site renderer',
    'same-site and cross-site iframe',
    'first iframe author subresources inherit native interception',
    'ignored native worker targets resume',
    'native popup',
    'serialized Date/BigInt',
    '30 context churn cycles',
    'zero pending native registration state',
    'clean process exit',
  ],
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
console.log(JSON.stringify(report, null, 2))
