// SPDX-License-Identifier: MIT
/* global history */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage, NativeResponse } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const requests = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  requests.push(request.url ?? '')
  response.setHeader('Content-Type', 'text/html')
  response.setHeader('Cache-Control', 'no-store')
  if (request.url === '/missing') response.statusCode = 404
  response.end(`<link rel="icon" href="data:,"><title>${request.url}</title><p>${request.url}</p>`)
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let nativeStderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (message) => {
    nativeStderr += message
  },
})
const page = createNativePage(source)
const checks = []
try {
  assert.equal(await page.goBack(), null)
  assert.equal(await page.goForward(), null)
  checks.push('empty native browser history returns null without a navigation')

  await page.goto(`${origin}/a`)
  await page.goto(`${origin}/b`)
  const previous = await page.goBack()
  assert.equal(page.url(), `${origin}/a`)
  assert.equal(await page.title(), '/a')
  assert(previous instanceof NativeResponse)
  assert.equal(previous.status(), 200)
  assert.equal(previous.url(), `${origin}/a`)
  const following = await page.goForward({ waitUntil: 'domcontentloaded' })
  assert.equal(page.url(), `${origin}/b`)
  assert.equal(await page.title(), '/b')
  assert(following instanceof NativeResponse)
  assert.equal(following.url(), `${origin}/b`)
  checks.push('actual back and forward restore documents with their real response metadata')

  await page.evaluate(() => {
    history.pushState({ native: 1 }, '', '#one')
    history.pushState({ native: 2 }, '', '#two')
  })
  assert.equal(await page.goBack(), null)
  assert.equal(page.url(), `${origin}/b#one`)
  assert.equal(await page.evaluate(() => JSON.stringify(history.state)), '{"native":1}')
  assert.equal(await page.goForward(), null)
  assert.equal(page.url(), `${origin}/b#two`)
  assert.equal(await page.evaluate(() => JSON.stringify(history.state)), '{"native":2}')
  checks.push('same-document history commits actual fragment and state and returns no response')

  await page.goBack()
  await page.goto(`${origin}/missing`)
  assert.equal(await page.goForward(), null)
  const restored = await page.goBack({ waitUntil: 'commit', timeout: 1000 })
  assert.equal(page.url(), `${origin}/b#one`)
  // Commit precedes HTML parsing; title is an assertion about document load.
  await page.waitForLoadState('load')
  assert.equal(await page.title(), '/b')
  const missing = await page.goForward()
  assert(missing instanceof NativeResponse)
  assert.equal(missing.status(), 404)
  assert.equal(page.url(), `${origin}/missing`)
  assert.equal(await page.title(), '/missing')
  checks.push(
    'a new document truncates forward history and actual HTTP404 history remains navigable',
  )
  if (restored !== null) assert(restored instanceof NativeResponse)

  const diagnostics = z
    .record(z.string(), z.number())
    .parse(await page.runAutomation('diagnostics'))
  for (const key of ['navigations', 'otherOperations', 'pendingDevTools', 'queuedDevTools'])
    assert.equal(diagnostics[key], 0, key)
  await source.close()
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  const directory = resolve('.suitecut/native-history')
  await mkdir(directory, { recursive: true })
  const report = {
    completed: true,
    binarySHA256,
    cefVersion: source.cefVersion,
    checks,
    requests,
    diagnostics,
    nativeStderr,
    exit: source.processExit?.(),
  }
  assert.equal(report.nativeStderr, '')
  assert.equal(report.exit?.code, 0)
  await writeFile(resolve(directory, 'report.json'), `${JSON.stringify(report, null, 2)}\n`)
  console.log(`Native history verified: ${resolve(directory, 'report.json')}`)
} finally {
  await source.close()
  await new Promise((done, reject) => server.close((error) => (error ? reject(error) : done())))
}
