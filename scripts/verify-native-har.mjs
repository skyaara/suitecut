// SPDX-License-Identifier: MIT
/* global fetch */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve('.suitecut/native-public-har')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const bytes = Buffer.from(Array.from({ length: 256 }, (_, index) => index))
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z
  .array(z.object({ method: z.string(), url: z.string(), base64: z.string() }))
  .parse([])
const Header = z.object({ name: z.string(), value: z.string() })
const HAR = z.object({
  log: z.object({
    entries: z.array(
      z.object({
        request: z.object({
          url: z.string(),
          method: z.string(),
          postData: z.object({ text: z.string(), _encoding: z.string().optional() }).optional(),
        }),
        response: z.object({
          headers: z.array(Header),
          content: z.object({ text: z.string().optional(), encoding: z.string().optional() }),
        }),
      }),
    ),
  }),
})
const server = createServer((request, response) => {
  const chunks = z.array(z.instanceof(Buffer)).parse([])
  request.on('data', (chunk) =>
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(String(chunk))),
  )
  request.on('end', () => {
    const body = Buffer.concat(chunks)
    wire.push({
      method: request.method ?? '',
      url: request.url ?? '',
      base64: body.toString('base64'),
    })
    if (request.url === '/bytes' || request.url === '/echo') {
      response.setHeader('content-type', 'application/octet-stream')
      response.setHeader('set-cookie', ['nativeharone=one; Path=/', 'nativehartwo=two; Path=/'])
      response.end(request.url === '/echo' ? body : bytes)
    } else if (request.url === '/theme.css') {
      response.setHeader('content-type', 'text/css')
      response.end('body { color: rgb(1, 2, 3); }')
    } else {
      response.setHeader('content-type', 'text/html')
      response.end(
        '<link rel="stylesheet" href="/theme.css"><body>native HAR ' +
          (request.url ?? '') +
          '</body>',
      )
    }
  })
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
if (!address || typeof address === 'string') throw new Error('Expected actual TCP server address')
const origin = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic: (message) => diagnostics.push(message),
})
try {
  const browser = new NativeBrowserContexts(source)
  const path = resolve(directory, 'actual.har')
  const context = await browser.newContext({ recordHAR: { path, content: 'embed' } })
  const page = await context.newPage()
  page.setDefaultTimeout(4000)
  await page.goto(origin + '/document')
  assert.deepEqual(
    await page.evaluate(async () =>
      Array.from(new Uint8Array(await (await fetch('/bytes')).arrayBuffer())),
    ),
    Array.from(bytes),
  )
  assert.deepEqual(
    await page.evaluate(async () =>
      Array.from(
        new Uint8Array(
          await (
            await fetch('/echo', {
              method: 'POST',
              body: new Uint8Array(Array.from({ length: 256 }, (_, index) => index)),
            })
          ).arrayBuffer(),
        ),
      ),
    ),
    Array.from(bytes),
  )
  checks.push('Real HTML/CSS and binary256 request/response execute on actual wire')
  await context.close()
  const archive = HAR.parse(JSON.parse(await readFile(path, 'utf8')))
  assert.ok(archive.log.entries.find((entry) => entry.request.url === origin + '/document'))
  assert.ok(archive.log.entries.find((entry) => entry.request.url === origin + '/theme.css'))
  const binary = archive.log.entries.find((entry) => entry.request.url === origin + '/bytes')
  assert.ok(binary?.response.content.text)
  assert.deepEqual(
    Buffer.from(
      binary.response.content.text,
      binary.response.content.encoding === 'base64' ? 'base64' : 'utf8',
    ),
    bytes,
  )
  assert.equal(
    binary.response.headers.filter((header) => header.name.toLowerCase() === 'set-cookie').length,
    2,
  )
  const post = archive.log.entries.find((entry) => entry.request.url === origin + '/echo')
  assert.ok(post?.request.postData)
  assert.deepEqual(
    Buffer.from(
      post.request.postData.text,
      post.request.postData._encoding === 'base64' ? 'base64' : 'utf8',
    ),
    bytes,
  )
  checks.push(
    'Native Context.close commits HAR with exact binary POST/body and duplicate Set-Cookie',
  )
  const replayContext = await browser.newContext()
  const replay = await replayContext.newPage()
  replay.setDefaultTimeout(4000)
  await replay.routeFromHAR(path)
  const zipContext = await browser.newContext()
  const zipPage = await zipContext.newPage()
  zipPage.setDefaultTimeout(4000)
  const zip = resolve(directory, 'updated.zip')
  await zipPage.routeFromHAR(zip, { update: true, updateMode: 'minimal', updateContent: 'attach' })
  await zipPage.goto(origin + '/recorded')
  assert.deepEqual(
    await zipPage.evaluate(async () =>
      Array.from(new Uint8Array(await (await fetch('/bytes')).arrayBuffer())),
    ),
    Array.from(bytes),
  )
  await zipPage.close()
  await zipContext.close()
  assert.ok((await readFile(zip)).length > 100)
  checks.push('Page update mode records real traffic and Page.close then Context.close commits ZIP')
  const fallbackContext = await browser.newContext()
  const fallback = await fallbackContext.newPage()
  fallback.setDefaultTimeout(4000)
  await fallback.routeFromHAR(zip, { notFound: 'fallback' })
  await fallback.goto(origin + '/unrecorded')
  assert.ok((await fallback.locator('body').textContent())?.includes('/unrecorded'))
  checks.push('Native replay miss falls through to actual HTTP server')
  await new Promise((done) => server.close(done))
  const count = wire.length
  await replay.goto(origin + '/document')
  assert.deepEqual(
    await replay.evaluate(async () =>
      Array.from(new Uint8Array(await (await fetch('/bytes')).arrayBuffer())),
    ),
    Array.from(bytes),
  )
  assert.deepEqual(
    await replay.evaluate(async () =>
      Array.from(
        new Uint8Array(
          await (
            await fetch('/echo', {
              method: 'POST',
              body: new Uint8Array(Array.from({ length: 256 }, (_, index) => index)),
            })
          ).arrayBuffer(),
        ),
      ),
    ),
    Array.from(bytes),
  )
  assert.equal(wire.length, count)
  checks.push('Native HAR replay fulfills HTML/CSS and exact binary GET/POST with server offline')
  await fallback.goto(origin + '/recorded')
  assert.deepEqual(
    await fallback.evaluate(async () =>
      Array.from(new Uint8Array(await (await fetch('/bytes')).arrayBuffer())),
    ),
    Array.from(bytes),
  )
  assert.equal(wire.length, count)
  checks.push('Native attached-resource ZIP replay preserves exact bytes with server offline')
  const abortContext = await browser.newContext()
  const abort = await abortContext.newPage()
  abort.setDefaultTimeout(4000)
  await abort.routeFromHAR(zip)
  await assert.rejects(
    abort.goto(origin + '/missing'),
    /ERR_FAILED|ERR_ABORTED|aborted|Failed|failed/,
  )
  checks.push('Default native replay miss aborts genuine intercepted request')
  await abortContext.close()
  await fallbackContext.close()
  await replayContext.close()
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await source.close()
  server.close()
  const finalSHA256 = createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
  if (finalSHA256 !== binarySHA256) failures.push('Native executable changed during proof')
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ binarySHA256, checks, failures, wire, diagnostics }, null, 2),
  )
}
assert.equal(failures.length, 0, failures.join('\n'))
