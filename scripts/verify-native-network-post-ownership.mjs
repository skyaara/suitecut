// SPDX-License-Identifier: MIT
/* global fetch, FormData, File, XMLHttpRequest */
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
  process.env.SUITECUT_NATIVE_POST_OWNERSHIP_DIRECTORY ?? '.suitecut/native-network-post-ownership',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const diagnostics = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const records = z
  .array(
    z.object({
      group: z.string(),
      stage: z.string(),
      physical: z.number(),
      pendingCommands: z.number(),
      queuedCommands: z.number(),
    }),
  )
  .parse([])
const Native = z.object({
  nativeRequestBodyPhysical: z.number(),
  pendingRequestBodyReads: z.number(),
  pendingNetworkCommands: z.number(),
  queuedDevTools: z.number(),
  networkPostPreparations: z.number(),
})
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (value) => diagnostics.push(value),
})
try {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Native context transport unavailable')
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const observerContext = await browser.newContext()
  const observer = await observerContext.newPage()
  for (const group of ['Page close', 'Context close', 'Original read deadline']) {
    const context = await browser.newContext()
    const page = await context.newPage()
    let release = () => {
      return
    }
    let received = () => {
      return
    }
    const held = new Promise((done) => {
      received = () => done(undefined)
    })
    const server = createServer((request, response) => {
      if (request.url === '/hold') {
        release = () => response.end('actual renderer released')
        received()
      } else {
        request.resume()
        response.setHeader('content-type', 'text/html')
        response.end('<body>actual native post receipt</body>')
      }
    })
    await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
    const address = server.address()
    assert.ok(address && typeof address !== 'string')
    try {
      await page.goto(`http://127.0.0.1:${address.port}/document`)
      const closed = page.waitForEvent('close', { timeout: 40000 })
      void closed.catch(() => undefined)
      // The real renderer blocks after starting its genuine File POST. The new
      // native getRequestPostData has actually been dispatched to that target;
      // no SDK callback, transport ACK or native sender is intercepted/delayed.
      const expression = `(${(() => {
        const form = new FormData()
        form.set('actual', new File([new Uint8Array([0, 255, 13, 10])], 'actual.bin'))
        void fetch('/post', { method: 'POST', body: form })
        const xhr = new XMLHttpRequest()
        xhr.open('GET', '/hold', false)
        xhr.send()
        return 'actual renderer released'
      }).toString()})()`
      const evaluating = page.source.runAutomation('evaluate', {
        expression,
        timeoutMs: group === 'Original read deadline' ? 500 : 0,
      })
      const evaluated = evaluating.then(
        () => ({ ok: true, error: '' }),
        (error) => ({ ok: false, error: String(error) }),
      )
      await Promise.race([
        held,
        evaluated.then(() => {
          throw new Error('Renderer exited before reaching actual synchronous XHR')
        }),
      ])
      let actual = Native.optional().parse(undefined)
      for (let attempt = 0; attempt < 512; ++attempt) {
        const state = Native.parse(await page.source.runAutomation('diagnostics', {}))
        if (
          state.pendingRequestBodyReads > 0 &&
          state.nativeRequestBodyPhysical > 0 &&
          state.pendingNetworkCommands > 0 &&
          state.queuedDevTools === 0 &&
          state.networkPostPreparations > 0
        ) {
          actual = state
          break
        }
      }
      assert.ok(actual, 'Actual native post read was not held; no physical-ownership claim')
      records.push({
        group,
        stage: 'actual dispatched native read outstanding',
        physical: actual.nativeRequestBodyPhysical,
        pendingCommands: actual.pendingNetworkCommands,
        queuedCommands: actual.queuedDevTools,
      })
      if (group === 'Page close') await page.close()
      else if (group === 'Context close') await context.close()
      else {
        const outcome = await evaluated
        assert.equal(outcome.ok, false)
        assert.match(outcome.error, /deadline|timed out|Timeout/)
        const retained = Native.parse(await observer.source.runAutomation('diagnostics', {}))
        assert.ok(
          retained.nativeRequestBodyPhysical > 0,
          'Logical evaluation expiry prematurely retired the actual native body read',
        )
        records.push({
          group,
          stage: 'logical caller expired; physical receipt retained',
          physical: retained.nativeRequestBodyPhysical,
          pendingCommands: retained.pendingNetworkCommands,
          queuedCommands: retained.queuedDevTools,
        })
      }
      // The reader's own original 30s deadline closes the real browser channel
      // in the last group. The verifier never resets that native deadline.
      await closed
      assert.equal((await evaluated).ok, false)
      const final = Native.parse(await observer.source.runAutomation('diagnostics', {}))
      assert.equal(final.nativeRequestBodyPhysical, 0)
      records.push({
        group,
        stage: 'actual channel close retired original receipt',
        physical: final.nativeRequestBodyPhysical,
        pendingCommands: final.pendingNetworkCommands,
        queuedCommands: final.queuedDevTools,
      })
    } catch (error) {
      failures.push(
        `${group}: ${error instanceof Error ? (error.stack ?? error.message) : String(error)}`,
      )
    } finally {
      release()
      await context.close().catch((error) => failures.push(String(error)))
      server.closeAllConnections()
      await new Promise((done) => server.close(() => done(undefined)))
    }
  }
  await observerContext.close()
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await source.close().catch((error) => failures.push(String(error)))
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  if (
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex') !== binarySHA256
  )
    failures.push('Native executable changed during proof')
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, records, failures, diagnostics }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
