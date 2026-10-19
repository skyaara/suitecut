// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-trace-active',
)
await mkdir(directory, { recursive: true })
const raw = directory + '/journals'
const css = 'body{background:rgb(12,34,56)}/*actual idle network bytes*/'
const server = createServer((request, response) => {
  response.writeHead(200, {
    'Content-Type': request.url === '/idle.css' ? 'text/css' : 'text/html',
  })
  response.end(request.url === '/idle.css' ? css : '<title>Native active trace</title>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const row = z.record(z.string(), z.unknown())
const report = row.parse({
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  raw,
  css,
  groups: [],
})
const groups = z.array(z.string()).parse([])
let source,
  stderr = '',
  phase = 'launch'
/**
 * Waits only on genuine filesystem bytes; it sends no browser command.
 * @param {string} path - The actual native output file.
 * @param {(text: string) => boolean} match - The expected byte predicate.
 */
async function textUntil(path, match) {
  const deadline = Date.now() + 5000
  for (;;) {
    try {
      const text = await readFile(path, 'utf8')
      if (match(text)) return text
    } catch (error) {
      if (!(error instanceof Error) || !('code' in error) || error.code !== 'ENOENT') throw error
    }
    assert(Date.now() < deadline, `Actual native file did not reach expected bytes: ${path}`)
    await setTimeout(20)
  }
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    tracesDir: raw,
    width: 320,
    height: 240,
    headless: true,
    onDiagnostic: (message) => (stderr += message),
  })
  const page = createNativePage(source)
  const context = page.context()
  await page.goto(origin)
  await context.tracing.start({ name: 'active', snapshots: true })
  phase = 'asynchronous console and network idle flush without a following browser command'
  await page.evaluate(
    `setTimeout(()=>{console.log('native-idle-'+String.fromCharCode(0xd800));fetch('/idle.css').then(r=>r.text()).then(text=>console.log('native-idle-body-'+text.length))},350)`,
  )
  const started = Date.now()
  const activeTrace = await textUntil(raw + '/active.trace', (text) =>
    text
      .split('\n')
      .filter(Boolean)
      .map((line) => row.parse(JSON.parse(line)))
      .some(
        (event) =>
          event.type === 'console' &&
          typeof event.text === 'string' &&
          event.text.startsWith('native-idle-body-'),
      ),
  )
  const consoleRows = activeTrace
    .split('\n')
    .filter(Boolean)
    .map((line) => row.parse(JSON.parse(line)))
  assert(
    consoleRows.some((event) => event.type === 'console' && event.text === 'native-idle-\ud800'),
  )
  const activeNetwork = await textUntil(raw + '/active.network', (text) =>
    text.includes('text/css'),
  )
  const resources = activeNetwork
    .split('\n')
    .filter(Boolean)
    .map((line) => row.parse(JSON.parse(line)))
  const cssSnapshot = resources
    .map((event) => row.parse(event.snapshot))
    .find((snapshot) => {
      const response = row.parse(snapshot.response)
      return row.parse(response.content).mimeType === 'text/css'
    })
  assert(cssSnapshot)
  const content = row.parse(row.parse(cssSnapshot.response).content)
  const resource = z.string().parse(content._file)
  const bytes = await readFile(raw + '/' + resource)
  assert.equal(bytes.toString(), css)
  const digest = z.string().parse(content._sha1).split('.')[0]
  assert.equal(createHash('sha1').update(bytes).digest('hex'), digest)
  report.idleObservationMs = Date.now() - started
  report.activeTrace = activeTrace
  report.activeNetwork = activeNetwork
  groups.push(
    'actual idle console UTF16 and network bytes reach raw disk without another browser action',
  )
  phase = 'final raw and ZIP settle in the same actual chunk namespace'
  await context.tracing.stopChunk({ path: directory + '/active.zip' })
  const finalNetwork = await readFile(raw + '/active.network', 'utf8')
  assert(finalNetwork.includes(resource))
  assert.equal(await readFile(raw + '/' + resource, 'utf8'), css)
  report.resourcePath = resource
  groups.push('native stop includes pending flush and preserves exact live resource namespace')
  await context.tracing.startChunk({ name: 'second' })
  await page.evaluate(`setTimeout(()=>console.log('native-second-idle'),350)`)
  await textUntil(raw + '/second.trace', (text) =>
    text
      .split('\n')
      .filter(Boolean)
      .map((line) => row.parse(JSON.parse(line)))
      .some((event) => event.type === 'console' && event.text === 'native-second-idle'),
  )
  await context.tracing.stop({ path: directory + '/second.zip' })
  const secondTrace = await readFile(raw + '/second.trace', 'utf8')
  assert(!secondTrace.includes('native-idle-body-'))
  groups.push('replacement chunk idle output excludes actual previous console records')
  phase = 'genuine independent API context active raw output'
  await context.request.tracing.start({ name: 'api-active', snapshots: true })
  const response = await context.request.get(origin + '/idle.css')
  await response.dispose()
  await textUntil(raw + '/api-active.network', (text) => text.includes('text/css'))
  await context.request.tracing.stop()
  groups.push('actual API request context uses its own live native journal')
  const diagnostics = row.parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.nativeTracePhysical, 0)
  assert.equal(diagnostics.nativeTraceLiveSnapshotBytes, 0)
  report.diagnostics = diagnostics
  assert(!(await readdir(raw)).some((name) => name.startsWith('.suitecut-trace-')))
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
  }
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
  report.groups = groups
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(directory + '/native-stderr.log', stderr)
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
process.stdout.write(JSON.stringify(report) + '\n')
