// SPDX-License-Identifier: MIT
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
const directory = resolve('.suitecut/native-har-metadata')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const HAR = z.object({
  log: z.object({
    pages: z.array(
      z.object({
        id: z.string().min(1),
        title: z.string(),
        startedDateTime: z.string(),
        pageTimings: z.object({ onContentLoad: z.number(), onLoad: z.number() }),
      }),
    ),
    entries: z.array(
      z.object({
        pageref: z.string().optional(),
        request: z.object({ url: z.string(), bodySize: z.number() }),
        response: z.object({ status: z.number(), redirectURL: z.string() }),
      }),
    ),
  }),
})
const server = createServer((request, response) => {
  if (request.url === '/relative/redirect') {
    response.writeHead(302, { Location: '../document' })
    response.end()
  } else if (request.url === '/theme.css') {
    response.setHeader('content-type', 'text/css')
    response.end('body { color: rgb(1, 2, 3); }')
  } else {
    response.setHeader('content-type', 'text/html')
    response.end(
      '<title>Actual CEF title</title><link rel="stylesheet" href="/theme.css"><body>native observed HAR</body>',
    )
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const before = Date.now()
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
  const path = resolve(directory, 'actual-page.har')
  const context = await browser.newContext({ recordHar: { path, content: 'embed' } })
  const page = await context.newPage()
  await page.goto(origin + '/relative/redirect')
  assert.equal(await page.title(), 'Actual CEF title')
  const actualPageId = page.source.pageId
  assert.ok(actualPageId)
  await context.close()
  const archive = HAR.parse(JSON.parse(await readFile(path, 'utf8')))
  assert.equal(archive.log.pages.length, 1)
  const recordedPage = archive.log.pages[0]
  assert.ok(recordedPage)
  assert.equal(recordedPage.id, 'page@' + String(actualPageId))
  assert.equal(recordedPage.title, 'Actual CEF title')
  assert.ok(Date.parse(recordedPage.startedDateTime) >= before - 1000)
  assert.ok(Date.parse(recordedPage.startedDateTime) <= Date.now())
  assert.ok(recordedPage.pageTimings.onContentLoad > 0)
  assert.ok(recordedPage.pageTimings.onLoad > 0)
  checks.push(
    'HAR page identity is the actual CEF Source page, with genuine title and loader lifecycle timestamps',
  )
  const document = archive.log.entries.find((entry) => entry.request.url === origin + '/document')
  const redirect = archive.log.entries.find(
    (entry) => entry.request.url === origin + '/relative/redirect',
  )
  const stylesheet = archive.log.entries.find(
    (entry) => entry.request.url === origin + '/theme.css',
  )
  assert.ok(document && redirect && stylesheet)
  for (const entry of [document, redirect, stylesheet]) assert.equal(entry.pageref, recordedPage.id)
  assert.equal(redirect.response.status, 302)
  assert.equal(redirect.response.redirectURL, origin + '/document')
  checks.push(
    'Actual redirect Location resolves natively to absolute URL; real document and stylesheet share canonical pageref',
  )
  for (const entry of [document, redirect, stylesheet]) assert.equal(entry.request.bodySize, 0)
  checks.push(
    'Actual original CDP request with absent body records bodySize0 without guessing from method',
  )

  const dataPath = resolve(directory, 'actual-data-page.har')
  const dataContext = await browser.newContext({ recordHar: { path: dataPath } })
  const dataPage = await dataContext.newPage()
  await dataPage.goto('data:text/html,<title>Native data title</title><body>data</body>')
  await dataPage.waitForLoadState('domcontentloaded')
  assert.equal(await dataPage.title(), 'Native data title')
  await dataContext.close()
  const data = HAR.parse(JSON.parse(await readFile(dataPath, 'utf8')))
  assert.equal(data.log.pages.length, 1)
  const actualData = data.log.pages[0]
  assert.ok(actualData)
  assert.equal(actualData.title, 'Native data title')
  assert.ok(actualData.pageTimings.onContentLoad > 0)
  assert.ok(actualData.pageTimings.onLoad > 0)
  assert.notEqual(actualData.id, recordedPage.id)
  checks.push(
    'Actual data URL page and distinct context retain native title/lifecycle even without an HTTP request',
  )
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await source.close().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  if (
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex') !== binarySHA256
  )
    failures.push('Native executable changed during proof')
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, checks, failures, diagnostics }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
