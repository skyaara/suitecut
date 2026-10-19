// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-locator-highlight'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 240,
  height: 160,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
/** @type {{width:number,data:Buffer}|undefined} */
let latest
let frames = 0
const unsubscribe = source.onFrame((frame) => {
  latest = { width: frame.width, data: Buffer.from(frame.data) }
  ++frames
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const errors = []
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  nativeHighlights: z.number(),
  nativeHighlightResidents: z.number(),
  nativeHighlightPhysical: z.number(),
  nativeHighlightProcessPhysical: z.number(),
  nativeDOMDomainOwners: z.number(),
  nativeDOMDomainPhysical: z.number(),
  nativeDOMDomainReleases: z.number(),
})
const markup =
  '<style>html,body{margin:0;background:white}#target{position:absolute;left:40px;top:40px;width:80px;height:60px;background:rgb(200,0,0)}</style><div id="target"></div>'
/**
 * Observe actual compositor bytes at a point inside the genuine target.
 * @param {{width:number,data:Buffer}} frame
 */
const pixel = (frame) => [
  ...frame.data.subarray((70 * frame.width + 70) * 4, (70 * frame.width + 70) * 4 + 4),
]
/**
 * Await a real native paint satisfying its byte predicate, under a bounded proof budget.
 * @param {(pixel:number[])=>boolean} predicate
 */
async function painted(predicate) {
  const deadline = performance.now() + 2000
  while (!latest || !predicate(pixel(latest))) {
    assert(performance.now() < deadline, 'Actual native Overlay paint was not observed')
    await delay(2)
  }
  return latest
}
const plain = (/** @type {number[]} */ value) =>
  value[0] === 0 && value[1] === 0 && value[2] === 200
const drawn = (/** @type {number[]} */ value) =>
  z.number().parse(value[0]) > 0 && z.number().parse(value[1]) > 0
const state = async () => ownership.parse(await source.runAutomation('diagnostics'))
async function retired() {
  const deadline = performance.now() + 2000
  let actual = await state()
  while (Object.values(actual).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    actual = await state()
  }
  for (const [key, value] of Object.entries(actual)) assert.equal(value, 0, key)
  return actual
}
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end('<button>Actual child</button>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
try {
  await page.setContent(markup)
  await painted(plain)
  const before = await page.locator('html').innerHTML()
  const first = await page.locator('#target').highlight()
  const actual = await painted(drawn)
  await writeFile(directory + '/actual-highlight.bgra', actual.data)
  checks.push(
    'public locator highlight changes real CEF OSR pixels through a genuine native Overlay node tool',
  )
  assert.equal(await page.locator('html').innerHTML(), before)
  checks.push('native highlighting leaves actual author DOM and CSS unchanged')
  await first.dispose()
  await painted(plain)
  await retired()
  checks.push(
    'public disposable waits actual overlay teardown and restores original compositor pixels',
  )
  const stale = await page.locator('#target').highlight(),
    replacement = await page.locator('#target').highlight()
  await stale.dispose()
  assert.equal((await state()).nativeHighlights, 1)
  await painted(drawn)
  await replacement[Symbol.asyncDispose]()
  await retired()
  checks.push(
    'an older native token cannot remove a genuinely newer tool in the same Inspector session',
  )
  const prior = await page.locator('#target').highlight(),
    empty = await page.locator('#absent').highlight()
  await retired()
  await prior.dispose()
  await empty.dispose()
  await painted(plain)
  checks.push('a genuine empty query clears the prior tool and yields a no-resource disposable')
  await page.setContent('<button>One</button><button>Two</button>')
  await assert.rejects(page.locator('button').highlight(), /multiple locator matches/u)
  await assert.rejects(
    page.locator('button').first().highlight({ style: 'color:red' }),
    /arbitrary CSS/u,
  )
  await retired()
  checks.push(
    'unsupported arbitrary multiple matches and CSS style reject explicitly without an invented overlay',
  )
  await page.setContent('<div id="host"></div>')
  await page.locator('#host').evaluate((element) => {
    element.attachShadow({ mode: 'open' }).innerHTML = '<button>Actual shadow</button>'
  })
  const shadow = await page.getByRole('button', { name: 'Actual shadow' }).highlight()
  assert.equal((await state()).nativeHighlights, 1)
  await shadow.dispose()
  await retired()
  checks.push(
    'actual open-shadow selection reaches a genuine Element backend through the native utility query path',
  )
  await page.setContent(markup)
  const shared = await page.locator('#target').highlight()
  await page.coverage.startCSSCoverage()
  assert.equal((await state()).nativeDOMDomainOwners, 2)
  await page.coverage.stopCSSCoverage()
  assert.equal((await state()).nativeDOMDomainOwners, 1)
  await painted(drawn)
  await shared.dispose()
  await retired()
  checks.push(
    'real CSS coverage and native Overlay share DOM-domain ownership until each actual client retires',
  )
  const guarded = await page.locator('#target').highlight()
  await assert.rejects(page.sendDevToolsCommand('DOM.disable'), /conflicts/u)
  await assert.rejects(page.sendDevToolsCommand('Overlay.disable'), /conflicts/u)
  assert.equal((await state()).nativeHighlights, 1)
  await guarded.dispose()
  await retired()
  await page.sendDevToolsCommand('DOM.enable')
  await page.sendDevToolsCommand('Overlay.enable')
  await assert.rejects(page.locator('#target').highlight(), /raw Overlay/u)
  await page.sendDevToolsCommand('Overlay.disable')
  await page.sendDevToolsCommand('DOM.disable')
  await retired()
  checks.push(
    'explicit raw domain ownership and native tool ownership cannot silently disable one another',
  )
  await page.goto(`http://127.0.0.1:${address.port}/main`)
  await page.setContent('<iframe id="child"></iframe>')
  await page
    .locator('#child')
    .evaluate(
      (element, url) => element.setAttribute('src', url),
      `http://localhost:${address.port}/child`,
    )
  await page.frameLocator('#child').getByText('Actual child').waitFor({ timeout: 2000 })
  z.object({ sessions: z.number().positive() }).parse(await source.runAutomation('diagnostics'))
  const child = await page.frameLocator('#child').getByText('Actual child').highlight()
  assert.equal((await state()).nativeHighlights, 1)
  await page.goto('about:blank')
  await retired()
  await child.dispose()
  checks.push(
    'actual cross-process child frame identity owns its tool and real navigation retires it without rematching a stale locator',
  )
  await page.setContent(markup)
  const began = new AbortController(),
    started = new Promise((resolve) =>
      began.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const bridge = await page.exposeFunction('actualHighlightHeld', () => began.abort())
  const held = page.evaluate(
    '(()=>{globalThis.actualHighlightHeld();const until=Date.now()+250;while(Date.now()<until){};})()',
  )
  await started
  page.setDefaultTimeout(10)
  await assert.rejects(page.locator('#target').highlight(), /timed out/u)
  page.setDefaultTimeout(30000)
  assert((await state()).nativeSelectorWaitPhysical > 0)
  await held
  await bridge.dispose()
  await retired()
  checks.push(
    'an actual renderer-held selection observes the original deadline while physical query ownership survives to its real ACK',
  )
  const unclaimed = z.object({ token: z.string() }).parse(
    await source.runAutomation('highlight', {
      action: 'show',
      query: [{ engine: 'css', selector: '#target' }],
      timeoutMs: 30000,
    }),
  )
  assert(unclaimed.token.startsWith('h'))
  assert.equal((await state()).nativeHighlights, 1)
  const final = await retired()
  checks.push(
    'an actual unclaimed native delivery expires under its bounded TTL and every tracked physical resource retires',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ completed: true, checks, state: final, frames }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    directory + '/failure.json',
    JSON.stringify(
      {
        completed: false,
        checks,
        error:
          error instanceof Error
            ? { name: error.name, message: error.message, stack: error.stack }
            : String(error),
        frames,
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  unsubscribe()
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
if (errors.length) throw errors[0]
