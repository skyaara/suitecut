// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { EventEmitter, once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, ServerResponse } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeAPIRequestContext,
  NativeBrowserContexts,
} from '../dist/native.js'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-http-trace',
)
await mkdir(directory, { recursive: true })
const caller = fileURLToPath(import.meta.url)
const provenance = z.record(z.string(), z.string()).parse({})
for (const path of [
  executablePath,
  caller,
  resolve('dist/native-http.js'),
  resolve('dist/native-tracing.js'),
  resolve('native/browser/http_trace_controls.h'),
  resolve('native/browser/http_controls.cc'),
])
  provenance[path] = createHash('sha256')
    .update(await readFile(path))
    .digest('hex')

const observed = z
  .array(z.object({ url: z.string(), method: z.string(), body: z.string(), cookie: z.string() }))
  .parse([])
const serverErrors = z.array(z.string()).parse([])
let heldResponse = z.instanceof(ServerResponse).optional().parse(undefined)
const events = new EventEmitter()
const heldReady = once(events, 'held')
const binary = Buffer.from(Array.from({ length: 256 }, (_, index) => index))
const server = createServer((request, response) => {
  void (async () => {
    const chunks = []
    for await (const chunk of request) {
      assert(Buffer.isBuffer(chunk))
      chunks.push(chunk)
    }
    const bytes = Buffer.concat(chunks)
    observed.push({
      url: request.url ?? '/',
      method: request.method ?? 'GET',
      body: bytes.toString('base64'),
      cookie: request.headers.cookie ?? '',
    })
    if (request.url === '/held') {
      heldResponse = response
      events.emit('held')
      return
    }
    if (request.url === '/never') return
    if (request.url === '/binary') {
      response.writeHead(200, { 'Content-Type': 'application/octet-stream' })
      response.end(binary)
      return
    }
    if (request.url === '/simple.json') {
      response.writeHead(201, { 'Content-Type': 'application/json' })
      response.end(JSON.stringify({ actualMethod: request.method, actualBody: bytes.toString() }))
      return
    }
    response.writeHead(200, {
      'Content-Type': request.url === '/style.css' ? 'text/css' : 'text/html',
    })
    response.end(
      request.url === '/style.css'
        ? 'button { color: rgb(12,34,56); }'
        : '<link rel="stylesheet" href="/style.css"><button>Actual browser trace</button>',
    )
  })().catch((error) => {
    serverErrors.push(String(error))
    response.destroy()
  })
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`

let source
let stderr = '',
  phase = 'launch'

const report = z
  .record(z.string(), z.unknown())
  .parse({ result: 'running', origin, caller, provenance, observed, serverErrors })
try {
  source = await launchNativeBrowser({
    executablePath,
    width: 320,
    height: 240,
    headless: true,
    onDiagnostic: (message) => {
      if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
    },
  })
  const contexts = new NativeBrowserContexts(source),
    context = await contexts.newContext(),
    page = await context.newPage(),
    api = context.request
  assert.notEqual(api.tracing, context.tracing)
  assert.equal(page.request, api)
  await context.addCookies([{ name: 'actual-shared', value: 'native-cookie', url: origin }])
  phase = 'independent genuine browser and API traces'
  await context.tracing.start({ snapshots: true })
  await api.tracing.start({ snapshots: true, sources: true })
  await page.goto(origin + '/index.html')
  const response = await page.request.post(origin + '/simple.json', { data: { foo: 'bar' } })
  assert.equal(response.status(), 201)
  assert.deepEqual(await response.json(), { actualMethod: 'POST', actualBody: '{"foo":"bar"}' })
  await context.tracing.stop({ path: directory + '/browser.zip' })
  await api.tracing.stop({ path: directory + '/api.zip' })
  await response.dispose()
  assert(
    observed.some(
      (event) =>
        event.url === '/simple.json' && event.cookie.includes('actual-shared=native-cookie'),
    ),
  )
  phase = 'independent native HTTP context exact binary response'
  const independent = await NativeAPIRequestContext.create(source)
  await independent.tracing.start({ snapshots: true })
  const exact = await independent.get(origin + '/binary')
  assert.deepEqual(await exact.body(), binary)
  await independent.tracing.stop({ path: directory + '/binary.zip' })
  await exact.dispose()
  await independent.dispose()
  phase = 'late completion excluded from replacement chunk'
  await api.tracing.start({ snapshots: true })
  const pending = api.get(origin + '/held')
  void pending.catch(() => undefined)
  await heldReady
  await api.tracing.stopChunk({ path: directory + '/held-first.zip' })
  await api.tracing.startChunk()
  assert(heldResponse)
  heldResponse.writeHead(200, { 'Content-Type': 'application/json' })
  heldResponse.end('{"actualLate":true}')
  const late = await pending
  assert.deepEqual(await late.json(), { actualLate: true })
  await api.tracing.stop({ path: directory + '/held-second.zip' })
  await late.dispose()
  phase = 'original native HTTP request deadline'
  await api.tracing.start({ snapshots: true })
  await assert.rejects(api.get(origin + '/never', { timeout: 80 }), /timeout|timed out|deadline/i)
  await api.tracing.stop({ path: directory + '/deadline.zip' })
  const diagnostics = z
    .record(z.string(), z.unknown())
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(diagnostics))
    if (key.startsWith('apiTrace')) assert.equal(value, 0, key)
  report.diagnostics = diagnostics
  assert.deepEqual(serverErrors, [])
  await context.close()
  report.result = 'pass'
} catch (error) {
  report.result = 'failed'
  report.phase = phase
  report.error = String(error)
  throw error
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
    report.result = 'failed_shutdown'
  } finally {
    server.closeAllConnections()
    await new Promise((resolve, reject) =>
      server.close((error) => (error ? reject(error) : resolve())),
    )
    report.nativeStderrBytes = Buffer.byteLength(stderr)
    if (stderr && report.result === 'pass') {
      report.result = 'failed_stderr'
      report.error = stderr
    }
    await writeFile(directory + '/report.json', JSON.stringify(report, null, 2))
    await writeFile(directory + '/native-stderr.log', stderr)
  }
}
assert.equal(report.result, 'pass', JSON.stringify(report))
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(
  JSON.stringify({
    result: report.result,
    directory,
    nativeShutdown: report.nativeShutdown,
    nativeStderrBytes: report.nativeStderrBytes,
  }),
)
