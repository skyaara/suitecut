// SPDX-License-Identifier: MIT
/* global fetch */
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_HAR_ORDER_DIRECTORY ?? '.suitecut/native-har-route-order',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([]),
  wire = z.array(z.string()).parse([])
const receipts = z.array(z.object({ phase: z.string(), value: z.unknown() })).parse([])
const server = createServer((request, response) => {
  wire.push(request.url ?? '')
  response.setHeader('content-type', 'text/plain')
  response.end('actual-wire')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/**
 * Write the real archive consumed by the native file reader.
 * @param {string} name
 * @param {string} [path]
 */
async function archive(name, path) {
  const file = resolve(directory, `${name}.har`)
  await writeFile(
    file,
    JSON.stringify({
      log: {
        entries:
          path === undefined
            ? []
            : [
                {
                  request: { method: 'GET', url: origin + path, headers: [] },
                  response: {
                    status: 200,
                    statusText: 'OK',
                    headers: [],
                    content: { text: `archive-${name}` },
                  },
                },
              ],
      },
    }),
  )
  return file
}
/**
 * Fetch through the actual Page and return its response bytes as text.
 * @param {import('../dist/native.js').NativePage} page
 * @param {string} path
 */
const read = (page, path) =>
  page.evaluate((url) => fetch(url).then((response) => response.text()), origin + path)
let close = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  close = () => source.close()
  const runContext = source.runContext?.bind(source),
    pageSource = source.pageSource?.bind(source),
    onContextEvent = source.onContextEvent?.bind(source)
  assert.ok(runContext && pageSource && onContextEvent)
  const browser = new NativeBrowserContexts({ ...source, runContext, pageSource, onContextEvent })
  {
    const context = await browser.newContext(),
      current = await context.newPage()
    await current.goto(origin)
    let ordinary = 0
    await context.route(origin + '/context-new', async (route) => {
      ++ordinary
      await route.fulfill({ body: 'older-handler' })
    })
    await context.routeFromHAR(await archive('new-context', '/context-new'), {
      url: origin + '/context-new',
    })
    const future = await context.newPage()
    await future.goto(origin)
    assert.equal(await read(current, '/context-new'), 'archive-new-context')
    assert.equal(await read(future, '/context-new'), 'archive-new-context')
    assert.equal(ordinary, 0)
    assert.equal(wire.includes('/context-new'), false)
    await context.close()
    checks.push('Newer Context HAR has identical precedence on current and future real Pages')
  }
  {
    const context = await browser.newContext(),
      current = await context.newPage()
    await current.goto(origin)
    await context.routeFromHAR(await archive('old-context', '/context-old'), {
      url: origin + '/context-old',
    })
    let ordinary = 0
    await context.route(origin + '/context-old', async (route) => {
      ++ordinary
      await route.fallback()
    })
    const future = await context.newPage()
    await future.goto(origin)
    assert.equal(await read(current, '/context-old'), 'archive-old-context')
    assert.equal(await read(future, '/context-old'), 'archive-old-context')
    assert.equal(ordinary, 2)
    assert.equal(wire.includes('/context-old'), false)
    await context.close()
    checks.push('Newer Context callbacks fall back to older native HAR on current and future Pages')
  }
  {
    const context = await browser.newContext(),
      page = await context.newPage()
    await page.goto(origin)
    let called = 0
    await page.route(origin + '/page-first', async (route) => {
      ++called
      await route.fallback()
    })
    await context.routeFromHAR(await archive('page-first', '/page-first'), {
      url: origin + '/page-first',
    })
    assert.equal(await read(page, '/page-first'), 'archive-page-first')
    assert.equal(called, 1)
    assert.equal(wire.includes('/page-first'), false)
    await context.close()
    checks.push('Actual Page policy precedes a newer Context HAR and can fall back into it')
  }
  {
    const context = await browser.newContext(),
      page = await context.newPage()
    await page.goto(origin)
    await page.routeFromHAR(await archive('override', '/changed'), { url: origin + '/original' })
    let callback = 0,
      rematched = 0
    await page.route(origin + '/original', async (route) => {
      ++callback
      await route.fallback({ url: origin + '/changed' })
    })
    await page.route(origin + '/changed', async (route) => {
      ++rematched
      await route.fulfill({ body: 'wrong-rematch' })
    })
    assert.equal(await read(page, '/original'), 'archive-override')
    assert.equal(callback, 1)
    assert.equal(rematched, 0)
    assert.equal(wire.includes('/original') || wire.includes('/changed'), false)
    await context.close()
    checks.push(
      'HAR uses actual overridden request bytes while native selection retains the original URL snapshot',
    )
  }
  {
    const context = await browser.newContext(),
      page = await context.newPage()
    await page.goto(origin)
    let called = 0
    await page.route(origin + '/page-miss', async (route) => {
      ++called
      await route.fulfill({ body: 'older-page' })
    })
    await page.routeFromHAR(await archive('empty-page'), {
      url: origin + '/page-miss',
      notFound: 'fallback',
    })
    await context.routeFromHAR(await archive('empty-context'), { url: origin + '/page-miss' })
    assert.equal(await read(page, '/page-miss'), 'older-page')
    assert.equal(called, 1)
    assert.equal(wire.includes('/page-miss'), false)
    await context.close()
    checks.push('A native Page HAR miss advances to the older Page callback before Context policy')
  }
  {
    const context = await browser.newContext(),
      current = await context.newPage()
    await current.goto(origin)
    let once = 0
    await context.route(
      origin + '/context-once',
      async (route) => {
        ++once
        await route.fulfill({ body: 'once' })
      },
      { times: 1 },
    )
    await context.routeFromHAR(await archive('empty-once'), {
      url: origin + '/context-once',
      notFound: 'fallback',
    })
    const future = await context.newPage()
    await future.goto(origin)
    assert.equal(await read(current, '/context-once'), 'once')
    assert.equal(await read(future, '/context-once'), 'actual-wire')
    assert.equal(once, 1)
    assert.equal(wire.filter((path) => path === '/context-once').length, 1)
    await context.close()
    checks.push('Native HAR fallback preserves one actual context-global callback use across Pages')
  }
  await source.close()
  await source.processClosed
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  receipts.push({ phase: 'actual helper process and stdio closure', value: source.processExit?.() })
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await close().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        binarySHA256,
        checks,
        failures,
        receipts,
        wire,
        scope:
          'Native glob HAR/callback chronology and captured fallback order. Async file ordering/body/receipt controls are separate native components; no HAR RegExp, recorder or vendor favicon parity claim.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
