// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile, unlink } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

const binary = z
  .string()
  .min(1)
  .parse(process.env.SUITECUT_NATIVE_EXECUTABLE ?? process.env.SUITECUT_TRACE_BINARY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ??
    process.env.SUITECUT_TRACE_PROOF_DIRECTORY ??
    '.suitecut/native-trace-rawdirs',
)
await mkdir(directory, { recursive: true })
const raw = directory + '/nested/journals',
  caller = fileURLToPath(import.meta.url)
const css =
  'html,body{margin:0;background:rgb(0,0,255)}button{position:absolute;left:10px;top:10px}'
const html = `<link rel="stylesheet" href="/actual.css"><button onclick="document.body.style.background='rgb(255,0,0)';this.textContent='After'">Before</button><script>document.body.setAttribute(String.fromCharCode(0xd800),String.fromCharCode(0xdfff));document.body.setAttribute('__suitecutNativeUTF16Scalar','literal');document.body.appendChild(document.createTextNode(String.fromCharCode(0xd800)));</script>`
const server = createServer((request, response) => {
  response.writeHead(200, {
    'Content-Type': request.url === '/actual.css' ? 'text/css' : 'text/html',
  })
  response.end(request.url === '/actual.css' ? css : html)
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let source,
  stderr = '',
  phase = 'launch'
const groups = z.array(z.string()).parse([])
const report = z.record(z.string(), z.unknown()).parse({
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(binary))
    .digest('hex'),
  caller,
  callerSHA1: createHash('sha1')
    .update(await readFile(caller))
    .digest('hex'),
  origin,
  css,
  html,
  raw,
  groups,
})
try {
  source = await launchNativeBrowser({
    executablePath: binary,
    width: 320,
    height: 240,
    headless: true,
    tracesDir: raw,
    onDiagnostic: (m) => (stderr += m),
  })
  const page = createNativePage(source),
    context = page.context()
  phase = 'named chunk raw journals and actual PNG/ARIA resources'
  await context.tracing.start({
    name: 'first',
    snapshots: { dom: true, screen: true, aria: true },
    sources: true,
  })
  await page.goto(origin + '/index.html')
  await page.getByRole('button', { name: 'Before' }).click()
  await context.tracing.stopChunk({ path: directory + '/first.zip' })
  for (const extension of ['trace', 'network', 'stacks'])
    assert((await readFile(raw + '/first.' + extension)).length > 0)
  groups.push('named raw + ZIP, actual native PNG/ARIA/DOM/HAR bytes')
  phase = 'second chunk resource namespace and raw-only stop'
  await context.tracing.startChunk({ name: 'second' })
  await page.getByRole('button', { name: 'After' }).click()
  await context.tracing.stop()
  assert((await readFile(raw + '/second.trace')).length > 0)
  assert(!(await readdir(directory)).includes('second.zip'))
  groups.push('raw-only second chunk and independent physical resources')
  phase = 'unresolved action before previous chunk cannot create orphan after'
  await context.tracing.start({ name: 'pending-first', snapshots: true })
  const ready = page.waitForEvent('console')
  const held = page.evaluate(
    '(() => { console.log("actual-held"); return new Promise(resolve => { globalThis.actualTraceRelease = resolve; }); })()',
  )
  void held.catch(() => undefined)
  await ready
  await context.tracing.stopChunk({ path: directory + '/pending-first.zip' })
  await context.tracing.startChunk({ name: 'pending-second' })
  await page.evaluate('void globalThis.actualTraceRelease()')
  await held
  await context.tracing.stop({ path: directory + '/pending-second.zip' })
  groups.push('actual pending evaluate excluded from replacement raw chunk')
  phase = 'independent native API context raw-only output'
  await context.request.tracing.start({ name: 'actual-api', snapshots: true })
  const response = await context.request.get(origin + '/api')
  await response.dispose()
  await context.request.tracing.stop()
  assert((await readFile(raw + '/actual-api.trace')).length > 0)
  groups.push('genuine independent API raw journals')
  const diagnostics = z
    .record(z.string(), z.unknown())
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(diagnostics))
    if ((key.startsWith('nativeTrace') && !key.includes('Readbacks')) || key.startsWith('apiTrace'))
      assert.equal(value, 0, key)
  report.diagnostics = diagnostics
  report.rawEntries = await readdir(raw)
  await source.close()
  source = undefined
  phase = 'actual raw directory ENOTDIR and same native context recovery'
  const blocker = directory + '/actual-file'
  await writeFile(blocker, 'actual file, not a directory')
  source = await launchNativeBrowser({
    executablePath: binary,
    width: 320,
    height: 240,
    headless: true,
    tracesDir: blocker + '/nested',
    onDiagnostic: (m) => (stderr += m),
  })
  const recovery = createNativePage(source).context()
  await recovery.tracing.start({ name: 'blocked' })
  await assert.rejects(recovery.tracing.stop(), /ENOTDIR/)
  const failed = z.record(z.string(), z.unknown()).parse(await source.runAutomation('diagnostics'))
  assert.equal(failed.nativeTracePhysical, 0)
  assert.equal(failed.nativeTraceContexts, 0)
  await unlink(blocker)
  await recovery.tracing.start()
  await recovery.tracing.stop()
  const files = await readdir(blocker + '/nested')
  report.defaultName = files.find((name) => /^browser-\d+-\d+-\d+-\d+\.trace$/.test(name))
  assert(report.defaultName)
  groups.push('actual raw ENOTDIR retires session; same native context recovers with default name')
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
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(resolve))
    report.nativeStderrBytes = Buffer.byteLength(stderr)
    await writeFile(directory + '/native-stderr.log', stderr)
    await writeFile(directory + '/report.json', JSON.stringify(report, null, 2))
  }
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(JSON.stringify(report))
