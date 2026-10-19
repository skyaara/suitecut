// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-bootstrap-siblings',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const observation = `({count:globalThis.actualInitCount,version:globalThis.actualInitVersion,language:navigator.language,zone:Intl.DateTimeFormat().resolvedOptions().timeZone})`
const firstAuthor = `<script>globalThis.actualFirstAuthor=${observation};globalThis.actualTransfer=new DataTransfer();globalThis.actualEvents=0;document.addEventListener('actual-event',()=>globalThis.actualEvents++)</script>`
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(
    `<!doctype html><button id=target>Actual target</button>${firstAuthor}${request.url === '/child' ? '' : '<iframe id=left name=duplicate src=/child></iframe><iframe id=right name=duplicate src=/child></iframe>'}`,
  )
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  width: 360,
  height: 240,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const binarySHA256 = createHash('sha256')
  .update(await readFile(executablePath))
  .digest('hex')
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const behaviorErrors = []
const expected = { count: 1, version: 1, language: 'fr-FR', zone: 'Europe/Paris' }
try {
  const browser = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: (action, params, signal) => {
      assert(typeof source.runContext === 'function')
      return source.runContext(action, params, signal)
    },
    pageSource: (id) => {
      assert(typeof source.pageSource === 'function')
      return source.pageSource(id)
    },
    onContextEvent: (listener) => {
      assert(typeof source.onContextEvent === 'function')
      return source.onContextEvent(listener)
    },
  })
  const context = await browser.newContext({ locale: 'fr-FR', timezoneId: 'Europe/Paris' })
  await context.addInitScript(
    'globalThis.actualInitCount=(globalThis.actualInitCount||0)+1;globalThis.actualInitVersion=1;void 0',
  )
  const page = await context.newPage()
  page.setDefaultTimeout(2000)
  assert.deepEqual(await page.evaluate(observation), expected)
  checks.push(
    'Actual initial blank main document receives native settings and one init-script execution before author code',
  )

  await page.goto(`http://127.0.0.1:${address.port}/`)
  assert.deepEqual(await page.evaluate('globalThis.actualFirstAuthor'), expected)
  for (const id of ['left', 'right']) {
    const target = page.frameLocator('#' + id).locator('#target')
    assert.deepEqual(
      await target.evaluate(
        () => /** @type {unknown} */ (Reflect.get(globalThis, 'actualFirstAuthor')),
      ),
      expected,
    )
  }
  assert.equal(
    await page.evaluate(
      'document.querySelector("#left").contentDocument!==document.querySelector("#right").contentDocument',
    ),
    true,
  )
  checks.push(
    'Two actual same-origin sibling documents complete synchronous snapshot bootstrap and first-author settings/init observations',
  )

  for (const id of ['left', 'right']) {
    const target = page.frameLocator('#' + id).locator('#target')
    await target.dispatchEvent('actual-event')
    assert.equal(
      await target.evaluate(() => /** @type {unknown} */ (Reflect.get(globalThis, 'actualEvents'))),
      1,
    )
    const transfer = await target.evaluateHandle(() => {
      const actual = /** @type {typeof globalThis & {actualTransfer:DataTransfer}} */ (globalThis)
      return actual.actualTransfer
    })
    await assert.rejects(
      target.dispatchEvent('drop', { dataTransfer: transfer }),
      /no pre-author capture barrier for this same-process child/u,
    )
    await transfer.dispose()
  }
  const transfer = await page.evaluateHandle('globalThis.actualTransfer')
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  await transfer.dispose()
  checks.push(
    'Utility child dispatch remains functional; opaque child dispatch fails explicitly while main-document native factory remains usable',
  )

  await context.addInitScript('globalThis.actualInitVersion=2;void 0')
  await page.goto(`http://127.0.0.1:${address.port}/next`)
  for (const id of ['left', 'right'])
    assert.deepEqual(
      await page
        .frameLocator('#' + id)
        .locator('#target')
        .evaluate(() => /** @type {unknown} */ (Reflect.get(globalThis, 'actualFirstAuthor'))),
      { ...expected, version: 2 },
    )
  await context.close()
  assert.equal(await createNativePage(source).evaluate('6*7'), 42)
  checks.push(
    'Later sibling documents fetch the current accepted init registry rather than a renderer cache; actual context closure preserves the healthy owner',
  )
  assert.equal(checks.length, 4)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, binarySHA256, checks }, null, 2) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, binarySHA256, checks, error: String(error) }, null, 2) +
      '\n',
  )
  throw error
} finally {
  try {
    await finishNativeProof(source, diagnostics, directory, behaviorErrors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native sibling bootstrap4 passed')
