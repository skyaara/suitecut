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
  process.env.SUITECUT_NATIVE_CONTEXT_ROUTE_REMOVAL_DIRECTORY ??
    '.suitecut/native-context-route-removal',
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
    onContextEvent = source.onContextEvent?.bind(source),
    subscribeMutation = source.onContextRouteMutation?.bind(source)
  assert.ok(runContext && pageSource && onContextEvent && subscribeMutation)
  const contexts = new NativeBrowserContexts({ ...source, runContext, pageSource, onContextEvent }),
    context = contexts.defaultContext(),
    page = context.pages()[0]
  assert.ok(page)
  context.setDefaultTimeout(5000)
  await page.goto(origin)
  /**
   * Await only the actual native admission/control receipts, never a timer substitute.
   * @param {number} contextId Actual native context.
   */
  const mutations = (contextId) => {
    const admitted = gate(),
      applied = gate()
    let admittedId = ''
    const detach = subscribeMutation((record) => {
      if (record.contextId !== contextId) return
      receipts.push({ phase: 'actual policy ' + record.phase, value: record })
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
   * Fetch an actual author request.
   * @param {string} path Actual requested path.
   */
  const read = (path) =>
    page.evaluate((url) => fetch(url).then((response) => response.text()), origin + path)

  const entered = gate(),
    finish = gate()
  release.push(finish.open)
  await context.route(
    origin + '/wait',
    async (route) => {
      entered.open()
      await finish.promise
      await route.fulfill({ body: 'actual-held-callback' })
    },
    { times: 1 },
  )
  const reading = read('/wait')
  reading.catch(() => undefined)
  await entered.promise
  const first = mutations(context.id)
  let completed = false
  const waiting = context.unrouteAll({ behavior: 'wait' }).then(() => {
    completed = true
  })
  waiting.catch(() => undefined)
  try {
    await first.admitted
    await first.applied
    await context.route(origin + '/future', (route) => route.fulfill({ body: 'future-policy' }))
    assert.equal(completed, false)
    finish.open()
    assert.equal(await reading, 'actual-held-callback')
    await waiting
    assert.equal(completed, true)
    assert.equal(wire.includes('/wait'), false)
    assert.equal(await read('/future'), 'future-policy')
    checks.push(
      'Wait captures an exhausted real callback through finish ACK while a future policy installs independently',
    )
  } finally {
    first.detach()
  }
  await context.unrouteAll()

  const throwing = gate(),
    throwNow = gate()
  release.push(throwNow.open)
  await context.route(origin + '/ignore', async () => {
    throwing.open()
    await throwNow.promise
    throw new Error('selected actual user exception')
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
  await context.unrouteAll({ behavior: 'ignoreErrors' })
  throwNow.open()
  assert.equal(await failedFetch, true)
  await context.unrouteAll({ behavior: 'wait' })
  assert.deepEqual(page.networkController().takeRouteErrors(), [])
  assert.equal(wire.includes('/ignore'), false)
  checks.push(
    'IgnoreErrors suppresses the selected user exception only after actual removal and resolves the real paused request by abort',
  )

  const timeoutEntered = gate(),
    timeoutFinish = gate()
  release.push(timeoutFinish.open)
  await context.route(origin + '/timeout', async (route) => {
    timeoutEntered.open()
    await timeoutFinish.promise
    await route.fulfill({ body: 'still-actually-running' })
  })
  const timeoutRead = read('/timeout')
  timeoutRead.catch(() => undefined)
  await timeoutEntered.promise
  context.setDefaultTimeout(100)
  await assert.rejects(context.unrouteAll({ behavior: 'wait' }), /deadline|timeout|timed out/iu)
  context.setDefaultTimeout(5000)
  timeoutFinish.open()
  assert.equal(await timeoutRead, 'still-actually-running')
  await context.unrouteAll({ behavior: 'wait' })
  checks.push(
    'The original removal deadline rejects without pretending an authored callback or its actual Fetch pause settled',
  )

  const independent = await contexts.newContext(),
    child = await independent.newPage(),
    closeEntered = gate(),
    closeFinish = gate()
  independent.setDefaultTimeout(5000)
  release.push(closeFinish.open)
  await child.goto(origin)
  await independent.route(origin + '/close', async (route) => {
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
  const fourth = mutations(independent.id)
  const closeWait = independent.unrouteAll({ behavior: 'wait' })
  const closed = assert.rejects(closeWait, /closed|aborted|unavailable/iu)
  try {
    await fourth.admitted
    await independent.close()
    await closed
    closeFinish.open()
    await childRead.catch(() => undefined)
    assert.equal(await page.evaluate(() => 7), 7)
    checks.push(
      'Actual independent context closure rejects its wait while a genuine still-live observer Page remains usable',
    )
  } finally {
    fourth.detach()
  }
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
          'Actual public context string-glob removal callback receipts only; Page removal/HAR ordering/nonstring matchers remain separate.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 4)
