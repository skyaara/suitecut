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
  process.env.SUITECUT_NATIVE_PAGE_ROUTE_REMOVAL_DIRECTORY ?? '.suitecut/native-page-route-removal',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([])
const receipts = z.array(z.object({ phase: z.string(), value: z.unknown() })).parse([]),
  wire = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  wire.push(request.url ?? '')
  response.end('actual-wire')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
function gate() {
  /**
   * Release this authored host gate.
   * @type {() => void}
   */
  let open = () => {
    throw new Error('Actual gate initializer is unavailable')
  }
  const promise = new Promise((done) => {
    open = () => done(undefined)
  })
  return { promise, open }
}
/** @type {Awaited<ReturnType<typeof launchNativeBrowser>> | undefined} */
let source
const release = z.array(z.function({ input: [], output: z.void() })).parse([])
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  const runContext = source.runContext?.bind(source),
    pageSource = source.pageSource?.bind(source),
    onContextEvent = source.onContextEvent?.bind(source)
  assert.ok(runContext && pageSource && onContextEvent)
  const contexts = new NativeBrowserContexts({ ...source, runContext, pageSource, onContextEvent }),
    context = contexts.defaultContext(),
    page = context.pages()[0]
  assert.ok(page)
  page.setDefaultTimeout(5000)
  await page.goto(origin)
  /**
   * Observe only actual Page-owner policy admission/control ACK packets.
   * @param {NonNullable<typeof source>} actualSource Genuine native facade.
   */
  const mutations = (actualSource) => {
    const admitted = gate(),
      applied = gate()
    let admittedId = ''
    const subscribe = actualSource.onNetworkMessage?.bind(actualSource)
    assert.ok(subscribe)
    const detach = subscribe((record) => {
      if (record.event !== 'routemutation') return
      receipts.push({ phase: 'actual Page policy ' + record.phase, value: record })
      if (record.phase === 'admitted') {
        admittedId = record.mutationId
        admitted.open()
      } else {
        assert.equal(record.mutationId, admittedId)
        applied.open()
      }
    })
    return { admitted: admitted.promise, applied: applied.promise, detach }
  }
  /**
   * Fetch one actual author request.
   * @param {string} path Actual author request path.
   */
  const read = (path) =>
    page.evaluate((url) => fetch(url).then((response) => response.text()), origin + path)
  const entered = gate(),
    finish = gate(),
    contextEntered = gate(),
    contextFinish = gate()
  release.push(finish.open, contextFinish.open)
  await page.route(
    origin + '/wait',
    async (route) => {
      entered.open()
      await finish.promise
      await route.fulfill({ body: 'actual-held-Page-callback' })
    },
    { times: 1 },
  )
  await context.route(origin + '/context', async (route) => {
    contextEntered.open()
    await contextFinish.promise
    await route.fulfill({ body: 'actual-independent-Context-callback' })
  })
  const reading = read('/wait'),
    contextReading = read('/context')
  reading.catch(() => undefined)
  contextReading.catch(() => undefined)
  await entered.promise
  await contextEntered.promise
  const first = mutations(source)
  let completed = false
  const waiting = page.unrouteAll({ behavior: 'wait' }).then(() => {
    completed = true
  })
  waiting.catch(() => undefined)
  try {
    await first.admitted
    await first.applied
    await page.route(origin + '/future', (route) => route.fulfill({ body: 'future-Page-policy' }))
    assert.equal(completed, false)
    finish.open()
    assert.equal(await reading, 'actual-held-Page-callback')
    await waiting
    assert.equal(completed, true)
    assert.equal(await read('/future'), 'future-Page-policy')
    contextFinish.open()
    assert.equal(await contextReading, 'actual-independent-Context-callback')
    assert.equal(wire.includes('/wait') || wire.includes('/context'), false)
    checks.push(
      'Page wait observes its exhausted actual callback through Finish ACK, excludes a running Context callback and preserves a future Page policy',
    )
  } finally {
    first.detach()
  }
  await page.unrouteAll()
  await context.unrouteAll()

  const throwing = gate(),
    throwNow = gate()
  release.push(throwNow.open)
  await page.route(origin + '/ignore', async () => {
    throwing.open()
    await throwNow.promise
    throw new Error('selected actual Page user exception')
  })
  const failedFetch = page.evaluate(
    (url) =>
      fetch(url).then(
        () => false,
        () => true,
      ),
    origin + '/ignore',
  )
  failedFetch.catch(() => undefined)
  await throwing.promise
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  throwNow.open()
  assert.equal(await failedFetch, true)
  await page.unrouteAll({ behavior: 'wait' })
  assert.deepEqual(page.networkController().takeRouteErrors(), [])
  assert.equal(wire.includes('/ignore'), false)
  checks.push(
    'Only the actually selected Page user exception is ignored after genuine removal, and the paused request is actually aborted',
  )

  const timeoutEntered = gate(),
    timeoutFinish = gate()
  release.push(timeoutFinish.open)
  await page.route(origin + '/timeout', async (route) => {
    timeoutEntered.open()
    await timeoutFinish.promise
    await route.fulfill({ body: 'still-actually-running' })
  })
  const timeoutRead = read('/timeout')
  timeoutRead.catch(() => undefined)
  await timeoutEntered.promise
  page.setDefaultTimeout(100)
  await assert.rejects(page.unrouteAll({ behavior: 'wait' }), /deadline|timeout|timed out/iu)
  page.setDefaultTimeout(5000)
  timeoutFinish.open()
  assert.equal(await timeoutRead, 'still-actually-running')
  await page.unrouteAll({ behavior: 'wait' })
  checks.push(
    'The original Page removal deadline rejects while its real paused request and authored callback remain usable',
  )

  const independent = await contexts.newContext(),
    child = await independent.newPage(),
    closeEntered = gate(),
    closeFinish = gate()
  independent.setDefaultTimeout(5000)
  release.push(closeFinish.open)
  await child.goto(origin)
  await child.route(origin + '/close', async (route) => {
    closeEntered.open()
    await closeFinish.promise
    await route.fulfill({ body: 'cannot-fabricate-after-close' })
  })
  const childRead = child.evaluate(
    (url) =>
      fetch(url).then(
        () => false,
        () => true,
      ),
    origin + '/close',
  )
  childRead.catch(() => undefined)
  await closeEntered.promise
  const childSource = child.source
  const fourth = mutations(childSource)
  const closeWait = child.unrouteAll({ behavior: 'wait' })
  const closed = assert.rejects(closeWait, /closed|aborted|unavailable|disposed/iu)
  try {
    await fourth.admitted
    await child.close()
    await closed
    closeFinish.open()
    await childRead.catch(() => undefined)
    assert.equal(await page.evaluate(() => 7), 7)
    checks.push(
      'Actual Page close rejects its observer without fabricating host settlement, while an independent genuine Page remains usable',
    )
  } finally {
    fourth.detach()
  }
  await independent.close()

  const defaultEntered = gate(),
    defaultFinish = gate()
  release.push(defaultFinish.open)
  await page.route(origin + '/default', async (route) => {
    defaultEntered.open()
    await defaultFinish.promise
    await route.fulfill({ body: 'default-held-callback' })
  })
  const defaultRead = read('/default')
  defaultRead.catch(() => undefined)
  await defaultEntered.promise
  await page.unrouteAll()
  defaultFinish.open()
  assert.equal(await defaultRead, 'default-held-callback')
  assert.equal(wire.includes('/default'), false)
  checks.push(
    'Default Page removal still returns while an admitted real callback remains able to fulfill its actual pause',
  )
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  for (const done of release) done()
  if (source) {
    try {
      await source.close()
      await source.processClosed
      const exit = source.processExit?.()
      receipts.push({ phase: 'actual helper process and stdio closure', value: exit })
      assert.deepEqual(exit, { code: 0, signal: null })
    } catch (error) {
      failures.push(String(error))
    }
  }
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
          'Actual Page string-glob callback removals only; Context callback selection is excluded, native control-failure injection and HAR ordering are separate controlled/regression scopes.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 5)
