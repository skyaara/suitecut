// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as timer } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_RECOVERY_DIRECTORY ??
    '.suitecut/native-page-recovery-deadlines-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Bounds observation without delaying or completing the underlying action.
 * @template T
 * @param {Promise<T>} value
 * @returns {Promise<T>}
 */
async function bounded(value) {
  const owner = new AbortController()
  try {
    return await Promise.race([
      value,
      timer(10000, undefined, { signal: owner.signal }).then(() => {
        throw new Error('Native recovery proof operation exceeded verifier deadline')
      }),
    ])
  } finally {
    owner.abort()
  }
}
/**
 * Creates a real server admission latch.
 * @returns {{promise:Promise<void>,resolve:()=>void}}
 */
function latch() {
  let resolve = () => {
    throw new Error('Latch resolver unavailable')
  }
  const promise = new Promise((accept) => {
    resolve = () => accept(undefined)
  })
  return { promise: promise.then(() => undefined), resolve }
}
let documents = 0
let held = z
  .object({ entered: z.function({ input: [], output: z.void() }), release: z.promise(z.void()) })
  .optional()
  .parse(undefined)
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  response.setHeader('Content-Type', 'text/html')
  if (request.url !== '/recovery') {
    response.end('native-server')
    return
  }
  const title = `real-document-${++documents}`
  const finish = () => response.end(`<title>${title}</title><body>real recovery document</body>`)
  const actual = held
  held = undefined
  if (actual) {
    actual.entered()
    void actual.release.then(finish)
  } else finish()
})
await new Promise((accept) => server.listen(0, '127.0.0.1', accept))
const address = server.address()
assert(address && typeof address === 'object')
const url = `http://127.0.0.1:${address.port}/recovery`
const nativeStderr = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  onDiagnostic: nativeStderr.observe,
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat: 'i420',
  headless: true,
})
if (!source.runContext || !source.pageSource || !source.onContextEvent)
  throw new Error('Real native context transport unavailable')
const contexts = new NativeBrowserContexts(
  Object.assign(source, {
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  }),
)
const results = []
const behaviorErrors = z.array(z.unknown()).parse([])
try {
  for (const mode of ['timeout', 'cancel', 'close']) {
    const context = await contexts.newContext()
    const page = await context.newPage()
    await page.goto(url)
    const entered = latch(),
      release = latch()
    held = { entered: entered.resolve, release: release.promise }
    const crashWait = page.waitForEvent('crash', { timeout: 5000 })
    const crash = page.source
      .sendDevToolsCommand('Page.crash')
      .catch((error) => (error instanceof Error ? error.message : 'Non-Error failure'))
    await bounded(crashWait)
    const oldSignal = page.source.signal
    const begin = performance.now()
    const owner = new AbortController()
    const authored = new Error('Authored original recovery cancellation')
    const pending =
      mode === 'cancel'
        ? page.source
            .runAutomation(
              'recoverPage',
              { waitUntil: 'load', timeoutMs: 5000, api: 'page.reload' },
              owner.signal,
            )
            .catch((error) =>
              error instanceof Error ? error : new Error('Non-Error recovery failure'),
            )
        : page
            .reload({ timeout: mode === 'timeout' ? 40 : 5000 })
            .catch((error) =>
              error instanceof Error ? error : new Error('Non-Error recovery failure'),
            )
    await bounded(entered.promise)
    let result
    if (mode === 'close') {
      await bounded(context.close())
      result = await bounded(pending)
      assert(result instanceof Error)
      assert.equal(page.isClosed(), true)
      assert.equal(page.source.pageSignal?.aborted, true)
      release.resolve()
    } else {
      if (mode === 'cancel') owner.abort(authored)
      result = await bounded(pending)
      assert(result instanceof Error)
      assert.equal(page.source.state, 'failed')
      assert.equal(oldSignal.aborted, true)
      assert.equal(page.source.pageSignal?.aborted, false)
      if (mode === 'cancel') assert.equal(result, authored)
      else {
        assert.match(result.message, /timed out|Timeout/iu)
        assert(performance.now() - begin < 1000)
      }
      // A second observer joins the actual outstanding CEF reload; it must not issue another HTTP request.
      const before = documents
      const joined = page.reload({ timeout: 5000 })
      release.resolve()
      await bounded(joined)
      assert.equal(documents, before)
      assert.equal(page.source.state, 'running')
      assert.equal(oldSignal.aborted, true)
      assert.equal(page.source.signal.aborted, false)
      assert.match(await page.title(), /^real-document-/u)
      await context.close()
    }
    results.push({
      mode,
      error: result.message,
      elapsedMs: performance.now() - begin,
      actualCrash: await bounded(crash),
      oldEpochRemainsAborted: oldSignal.aborted,
      actualPageClosed: page.isClosed(),
    })
  }
  const healthy = z
    .object({ result: z.object({ value: z.number() }) })
    .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
  assert.equal(healthy.result.value, 42)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        documents,
        results,
        healthyRootValue: healthy.result.value,
      },
      null,
      2,
    ) + '\n',
  )
  console.log(
    'Actual native recovery original deadline, cancellation, join and physical context close proof passed',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        documents,
        error: error instanceof Error ? error.stack : 'Non-Error failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  try {
    await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
  } finally {
    await new Promise((accept, reject) =>
      server.close((error) => (error ? reject(error) : accept(undefined))),
    )
  }
}
