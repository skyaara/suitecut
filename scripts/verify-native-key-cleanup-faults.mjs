// SPDX-License-Identifier: MIT
// Private native fault injection; every close comes from real CEF target teardown.
/* global document, XMLHttpRequest */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_CLEANUP_FAULT_DIRECTORY ?? '.suitecut/native-key-cleanup-faults',
)
await mkdir(directory, { recursive: true })
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Exact local snapshot is recorded by hash.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
const digest = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const before = await digest()
/** @type {(()=>void)|undefined} */
let release
/** @type {(()=>void)|undefined} */
let held
const server = createServer((request, response) => {
  if (request.url === '/held') {
    release = () => response.end('release')
    held?.()
  } else response.end('<!doctype html><input id=a><input id=b>')
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const checks = []
try {
  for (const mode of ['NO_ID', 'REJECT', 'BAD_ACK']) {
    const flag = `SUITECUT_TEST_CLEANUP_${mode}`
    process.env[flag] = '1'
    let diagnostics = ''
    const source = await native.launchNativeBrowser({
      executablePath: executable,
      headless: true,
      onDiagnostic: (line) => {
        diagnostics += line.slice(0, Math.max(0, 1048576 - diagnostics.length))
      },
    })
    delete process.env[flag]
    const browser = new native.NativeBrowserContexts(source)
    try {
      const context = await browser.newContext(),
        page = await context.newPage()
      await page.goto(`http://127.0.0.1:${address.port}/`)
      await page.evaluate((url) => {
        document.addEventListener(
          'keydown',
          (event) => {
            if (event.key === 'Shift') {
              const request = new XMLHttpRequest()
              request.open('GET', url, false)
              request.send()
            }
          },
          { once: true },
        )
      }, `http://127.0.0.1:${address.port}/held`)
      const heldPromise = new Promise((accept) => {
        held = () => accept(undefined)
      })
      const controller = new AbortController(),
        cause = new Error('original cancellation cause')
      const first = page.runAutomation(
        'locator',
        { selector: '#a', action: 'sendKeys', value: '\uE008A', timeoutMs: 5000 },
        controller.signal,
      )
      const rejected = assert.rejects(first, (error) => error === cause)
      await heldPromise
      controller.abort(cause)
      await rejected
      const next = page.locator('#b').sendKeys('never', { timeout: 5000 })
      const nextRejected = assert.rejects(next, /Native send-keys cleanup/iu)
      const closed = page.waitForEvent('close', { timeout: 5000 })
      assert(release)
      release()
      await Promise.all([closed, nextRejected])
      assert(page.isClosed())
      assert.equal(context.pages().length, 0)
      const recovery = await context.newPage()
      await recovery.goto(`http://127.0.0.1:${address.port}/`)
      await recovery.locator('#a').sendKeys('recovered')
      assert.equal(await recovery.locator('#a').inputValue(), 'recovered')
      await context.close()
      checks.push({
        mode,
        actualTargetClosed: true,
        nextOwnerRejected: true,
        newTargetRecovered: true,
        diagnostics,
      })
    } finally {
      release?.()
      await source.close()
    }
  }
} finally {
  server.closeAllConnections()
  await new Promise((accept) => server.close(accept))
}
assert.equal(await digest(), before)
await writeFile(
  `${directory}/report.json`,
  JSON.stringify(
    {
      privateFaultInstrumentation: true,
      passed: checks.length,
      checks,
      executable,
      sha256: before,
      inputsUnchanged: true,
    },
    null,
    2,
  ) + '\n',
)
console.log(`Actual CEF cleanup fail-closed: ${checks.length} fault cases passed`)
