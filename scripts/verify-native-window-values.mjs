// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable && isAbsolute(executable), 'Set the absolute native executable')
const sdk = resolve('dist')
const directory = resolve(
  process.env.SUITECUT_NATIVE_WINDOW_PROOF ?? '.suitecut/native-window-values',
)
await mkdir(directory, { recursive: true })
const server = createServer((_, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end('<title>Actual cross-realm Window</title><p>Native values</p>')
})
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const checks = []
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic(message) {
    stderr += z.string().parse(message)
  },
})
try {
  const browser = new NativeBrowserContexts(source)
  const context = browser.defaultContext()
  const page = await context.newPage()
  assert.equal(
    await page.evaluate("globalThis.nativePopup = window.open('about:blank')"),
    'ref: <Window>',
  )
  checks.push('genuine opaque-origin cross-realm Window result')
  assert.deepEqual(await page.evaluate('({own:window, child:nativePopup, nested:[nativePopup]})'), {
    own: 'ref: <Window>',
    child: 'ref: <Window>',
    nested: ['ref: <Window>'],
  })
  checks.push('same-realm and nested cross-realm Window values')
  const handle = await page.evaluateHandle('nativePopup')
  try {
    assert.equal(await handle.jsonValue(), 'ref: <Window>')
  } finally {
    await handle.dispose()
  }
  checks.push('real cross-realm Window handle JSON value and disposal')
  assert.deepEqual(
    await page.evaluate(
      `(()=>{const fake=Object.create(Window.prototype);fake.own=17;return {tag:{[Symbol.toStringTag]:'Window',own:23},prototype:fake}})()`,
    ),
    {
      tag: { own: 23 },
      prototype: { own: 17 },
    },
  )
  checks.push('author tags and inherited Window prototype retain ordinary fields')
  await assert.rejects(
    page.evaluate(`({get own(){throw new Error('genuine author getter failure')}})`),
    /genuine author getter failure/u,
  )
  checks.push('ordinary author getter errors remain failures')
  await page.goto(`http://127.0.0.1:${address.port}/`)
  const popupEvent = context.waitForEvent('page')
  const crossURL = `http://localhost:${address.port}/`
  assert.equal(
    await page.evaluate(`globalThis.nativeCross = window.open(${JSON.stringify(crossURL)})`),
    'ref: <Window>',
  )
  const popup = await popupEvent
  await popup.waitForFunction('location.href === ' + JSON.stringify(crossURL))
  assert.equal(
    await page.evaluate(
      `(()=>{try{void nativeCross.document;return 'accessible'}catch(error){return error.name}})()`,
    ),
    'SecurityError',
  )
  assert.equal(await page.evaluate('nativeCross'), 'ref: <Window>')
  checks.push('genuine cross-origin WindowProxy result')
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        checks,
        executable,
        executableSHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        codecSHA256: createHash('sha256')
          .update(await readFile(resolve(sdk, 'native-values.js')))
          .digest('hex'),
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  await source.close()
  await writeFile(resolve(directory, 'native-stderr.log'), stderr)
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
