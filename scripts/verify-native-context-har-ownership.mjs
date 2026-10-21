// SPDX-License-Identifier: MIT
/* global XMLHttpRequest */
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
const directory = resolve('.suitecut/native-context-har-ownership')
await mkdir(directory, { recursive: true })
const archivePath = resolve(process.env.SUITECUT_HAR_ARCHIVE ?? `${directory}/captured.har`)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const records = z
  .array(
    z.object({
      group: z.string(),
      observation: z.record(z.string(), z.union([z.string(), z.number(), z.boolean()])),
    }),
  )
  .parse([])
const Native = z.object({
  nativeContextHARPhysical: z.number(),
  nativeContextHARPending: z.number(),
  pendingNetworkCommands: z.number(),
  queuedDevTools: z.number(),
  nativeHARReplayBytes: z.number(),
})
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (message) => diagnostics.push(message),
})
try {
  if (
    typeof source.runContext !== 'function' ||
    typeof source.pageSource !== 'function' ||
    typeof source.onContextEvent !== 'function'
  )
    throw new Error('Native context transport unavailable')
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  // Capture the replay input through the actual native HAR recorder in this
  // isolated attempt. No preceding proof or pre-existing workspace file is needed.
  if (!process.env.SUITECUT_HAR_ARCHIVE) {
    const fixture = createServer((_request, response) => {
      response.setHeader('content-type', 'text/html')
      response.end('<!doctype html><title>HAR ownership fixture</title>')
    })
    await new Promise((done) => fixture.listen(0, '127.0.0.1', () => done(undefined)))
    const address = fixture.address()
    assert(address && typeof address !== 'string')
    const recording = await browser.newContext()
    try {
      await recording.routeFromHAR(archivePath, {
        update: true,
        updateContent: 'embed',
        updateMode: 'full',
      })
      const page = await recording.newPage()
      await page.goto(`http://127.0.0.1:${address.port}/fixture`)
    } finally {
      await recording.close()
      fixture.closeAllConnections()
      await new Promise((done) => fixture.close(() => done(undefined)))
    }
  }
  await readFile(archivePath)
  const observerContext = await browser.newContext()
  const observer = await observerContext.newPage()
  for (const group of ['Page close', 'Context close', 'Original deadline']) {
    const context = await browser.newContext()
    context.setDefaultTimeout(group === 'Original deadline' ? 1000 : 5000)
    const page = await context.newPage()
    let release = () => {
      return
    }
    let receive = () => {
      return
    }
    const held = new Promise((done) => {
      receive = () => done(undefined)
    })
    const server = createServer((request, response) => {
      if (request.url === '/hold') {
        release = () => response.end('released actual sync XHR')
        receive()
      } else {
        response.setHeader('content-type', 'text/html')
        response.end('<body>actual held renderer</body>')
      }
    })
    await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
    const address = server.address()
    assert.ok(address && typeof address !== 'string')
    const origin = `http://127.0.0.1:${address.port}`
    try {
      await page.goto(origin + '/document')
      const closed = page.waitForEvent('close', { timeout: 10000 })
      void closed.catch(() => undefined)
      const evaluating = page.evaluate(() => {
        const xhr = new XMLHttpRequest()
        xhr.open('GET', '/hold', false)
        xhr.send()
        return xhr.responseText
      })
      const evaluated = evaluating.then(
        (value) => ({ ok: true, value }),
        (error) => ({ ok: false, value: String(error) }),
      )
      await Promise.race([
        held,
        evaluated.then(() => {
          throw new Error('Actual renderer exited before reaching synchronous XHR')
        }),
      ])
      const installing = context.routeFromHAR(archivePath)
      const installed = installing.then(
        () => ({ ok: true, error: '' }),
        (error) => ({ ok: false, error: String(error) }),
      )
      let observed = false
      for (let attempt = 0; attempt < 512; ++attempt) {
        const native = Native.parse(await page.source.runAutomation('diagnostics', {}))
        if (
          native.nativeContextHARPhysical > 0 &&
          native.nativeContextHARPending > 0 &&
          native.pendingNetworkCommands > 0 &&
          native.queuedDevTools === 0
        ) {
          observed = true
          records.push({
            group,
            observation: { stage: 'actual dispatched command receipt outstanding', ...native },
          })
          break
        }
      }
      assert.ok(
        observed,
        'Real renderer block did not retain a dispatched HAR configuration ACK; no held-ACK claim',
      )
      if (group === 'Page close') await page.close()
      else if (group === 'Context close') await context.close()
      // Original deadline is owned by native Engine/policy/control registries;
      // the verifier neither delays SDK callbacks nor resets that deadline.
      const outcome = await installed
      assert.equal(outcome.ok, false)
      if (group === 'Original deadline')
        assert.match(outcome.error, /deadline|timed out|Timeout|closed/)
      await closed
      const evaluation = await evaluated
      assert.equal(evaluation.ok, false)
      let final = Native.parse(await observer.source.runAutomation('diagnostics', {}))
      for (
        let attempt = 0;
        attempt < 512 && (final.nativeContextHARPending || final.nativeContextHARPhysical);
        ++attempt
      )
        final = Native.parse(await observer.source.runAutomation('diagnostics', {}))
      assert.equal(final.nativeContextHARPending, 0)
      assert.equal(final.nativeContextHARPhysical, 0)
      records.push({
        group,
        observation: {
          stage: 'actual close and receipt retirement',
          ...final,
          originalError: outcome.error,
        },
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
} finally {
  await source.close().catch((error) => failures.push(String(error)))
  if (
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex') !== binarySHA256
  )
    failures.push('Native executable changed during proof')
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, records, failures, diagnostics }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
