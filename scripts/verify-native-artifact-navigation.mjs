// SPDX-License-Identifier: MIT
/* global document, location, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import image from 'sharp'
import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { nativeBrowserModuleFiles } from './native-browser-source-files.mjs'

const directory = resolve('.suitecut/native-artifact-navigation')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const files = [
  executablePath,
  resolve('dist/native-page.js'),
  resolve('dist/native-artifacts.js'),
  resolve('native/browser/artifact_controls.h'),
  resolve('native/browser/controllers/artifacts/artifact_navigation.inc'),
  resolve('native/browser/controllers/artifacts/artifact_before_screenshot.inc'),
  resolve('native/browser/controllers/artifacts/artifact_view_lease.inc'),
  ...(await nativeBrowserModuleFiles()),
]
const provenance = z.record(z.string(), z.string()).parse({})
for (const file of files)
  provenance[file] = createHash('sha256')
    .update(await readFile(file))
    .digest('hex')
let loopRequests = 0
let looping = false
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.setHeader('Cache-Control', 'no-store')
  if (request.url?.startsWith('/loop')) {
    ++loopRequests
    response.end(
      `<script>globalThis.startNativeLoop=()=>setTimeout(()=>location.replace('/loop?'+Date.now()),5);${looping ? 'startNativeLoop()' : "document.documentElement.dataset.loopStopped='1'"}</script>`,
    )
    return
  }
  response.end(
    '<style>html,body{margin:0;background:blue;scrollbar-width:none}body{height:1000px}#offscreen{position:absolute;left:20px;top:500px;width:40px;height:30px;background:red}</style><div id="offscreen"></div>',
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 200,
  height: 120,
  deviceScaleFactor: 2,
  headless: true,
  onDiagnostic: (message) => {
    diagnostics += message.slice(0, Math.max(0, 1024 * 1024 - diagnostics.length))
  },
}).catch((error) => {
  server.close()
  throw error
})
/**
 * Decode an actual output pixel independently of native metadata.
 * @param {Buffer} encoded
 * @param {number} x
 * @param {number} y
 */
async function pixel(encoded, x, y) {
  const { data, info } = await image(encoded)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  return [...data.subarray((y * info.width + x) * 4, (y * info.width + x) * 4 + 4)]
}
try {
  const page = createNativePage(source)
  await page.goto(`${prefix}/grid`)
  await page.evaluate(() => {
    document.documentElement.dataset.artifactResize = '0'
    window.addEventListener('resize', () => {
      document.documentElement.dataset.artifactResize = String(
        Number(document.documentElement.dataset.artifactResize) + 1,
      )
    })
  })
  const viewport = await page.screenshot({ scale: 'css', path: `${directory}/viewport.png` })
  const device = await page.screenshot({ path: `${directory}/device.png` })
  const clip = await page.screenshot({
    scale: 'css',
    clip: { x: 160, y: 100, width: 100, height: 50 },
  })
  assert.deepEqual(
    await image(viewport)
      .metadata()
      .then(({ width, height }) => [width, height]),
    [200, 120],
  )
  assert.deepEqual(
    await image(device)
      .metadata()
      .then(({ width, height }) => [width, height]),
    [400, 240],
  )
  assert.deepEqual(
    await image(clip)
      .metadata()
      .then(({ width, height }) => [width, height]),
    [40, 20],
  )
  assert.deepEqual(await pixel(viewport, 199, 119), [0, 0, 255, 255])
  assert.deepEqual(await pixel(clip, 39, 19), [0, 0, 255, 255])
  assert.equal(
    await page.evaluate(() => Number(document.documentElement.dataset.artifactResize)),
    0,
  )
  const masked = await page.screenshot({
    fullPage: true,
    scale: 'css',
    mask: [page.locator('#offscreen')],
    path: `${directory}/masked.png`,
  })
  assert.deepEqual(await pixel(masked, 30, 510), [255, 0, 255, 255])
  const full = await page.screenshot({
    fullPage: true,
    scale: 'css',
    path: `${directory}/fullpage.png`,
  })
  assert.deepEqual(await pixel(full, 30, 510), [255, 0, 0, 255])
  assert.deepEqual(
    await page.evaluate(() => [window.innerWidth, window.innerHeight, window.devicePixelRatio]),
    [200, 120, 2],
  )

  // Exercise genuine old-document destruction concurrently with physical full
  // page captures; both public operations retain their native timeout policy.
  await Promise.all([
    (async () => {
      for (let i = 0; i < 5; ++i) await page.reload()
    })(),
    (async () => {
      for (let i = 0; i < 5; ++i) {
        const encoded = await page.screenshot({ fullPage: true, scale: 'css', timeout: 5000 })
        assert.deepEqual(await pixel(encoded, 30, 510), [255, 0, 0, 255])
      }
    })(),
  ])
  let hooks = 0
  const hooked = await page.screenshot({
    fullPage: true,
    scale: 'css',
    timeout: 5000,
    beforeScreenshot: async () => {
      ++hooks
      await page.evaluate(() => {
        window.setTimeout(() => location.reload(), 0)
      })
    },
    path: `${directory}/hook-navigation.png`,
  })
  assert.equal(hooks, 1)
  assert.deepEqual(await pixel(hooked, 30, 510), [255, 0, 0, 255])

  await page.goto(`${prefix}/loop`)
  const loopBefore = loopRequests
  looping = true
  await page.evaluate('startNativeLoop()')
  const admissionDeadline = performance.now() + 5000
  while (loopRequests < loopBefore + 2 && performance.now() < admissionDeadline) await delay(2)
  assert(loopRequests >= loopBefore + 2, 'Real navigation churn did not start')
  const started = performance.now()
  await assert.rejects(
    page.screenshot({ timeout: 400, fullPage: true }),
    /Cannot take a screenshot while page is navigating: Timeout 400ms exceeded/,
  )
  const elapsed = performance.now() - started
  assert(
    elapsed >= 350 && elapsed < 1000,
    `Navigation retries must preserve the original 400ms deadline; elapsed ${elapsed}ms`,
  )
  assert(
    loopRequests > loopBefore + 2,
    'Deadline proof requires genuine repeated native document navigations',
  )
  looping = false
  const stopped = await page.waitForFunction(
    () => document.documentElement.dataset.loopStopped === '1',
    undefined,
    { timeout: 5000 },
  )
  await stopped.dispose()
  await page.goto(`${prefix}/grid`)
  assert.deepEqual(await pixel(await page.screenshot({ scale: 'css' }), 199, 119), [0, 0, 255, 255])
  const state = z
    .object({
      otherOperations: z.number(),
      artifacts: z.number(),
      artifactPhysicalCapture: z.number(),
      pausedActions: z.number(),
      contextRetryQueued: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(state)) assert.equal(value, 0)
  for (const file of files)
    assert.equal(
      createHash('sha256')
        .update(await readFile(file))
        .digest('hex'),
      provenance[file],
      'Proof requires immutable native/SDK/source outputs',
    )
  await writeFile(
    `${directory}/report.json`,
    `${JSON.stringify({ result: 'passed', cefVersion: source.cefVersion, provenance, viewportDeviceClipPixelsAndNoResize: 'passed', fullPageOffscreenMaskAndRestoration: 'passed', concurrentFiveReloadsAndFiveFullPageScreenshots: 'passed', navigationHookExactlyOnce: hooks, continuousNavigationOriginalDeadline: { configuredMs: 400, elapsedMs: elapsed, actualDocumentRequests: loopRequests - loopBefore }, finalRegistries: state }, null, 2)}\n`,
  )
  console.info(
    'Native artifact navigation, capture flags, actual pixels, hook identity, and original deadline passed',
  )
} catch (error) {
  await writeFile(
    `${directory}/failure.json`,
    `${JSON.stringify({ error: String(error), provenance, loopRequests }, null, 2)}\n`,
  )
  throw error
} finally {
  await writeFile(`${directory}/native-diagnostics.log`, diagnostics)
  await source.close()
  server.close()
}
