// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-developer-script-inventory',
)
await mkdir(directory, { recursive: true })
const diagnostic = await captureNativeStderr(directory)
const count = 90
/**
 * Exact source bytes served for one author script.
 * @param {number} index
 */
function author(index) {
  return `globalThis.actualInventory${index}=()=>${index};\n`
}
const server = createServer((request, response) => {
  const index = /^\/author-(\d+)\.js$/u.exec(request.url ?? '')?.[1]
  if (index !== undefined) {
    response.setHeader('Content-Type', 'text/javascript; charset=utf-8')
    response.end(author(Number(index)))
  } else {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.end(
      '<!doctype html>' +
        Array.from(
          { length: count },
          (_, index) => `<script src="/author-${index}.js"></script>`,
        ).join(''),
    )
  }
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic: diagnostic.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(15_000)
const stateSchema = z.object({
  nativeDeveloperDebuggerSessions: z.number().int().nonnegative(),
  nativeDeveloperDebuggerScriptCursors: z.number().int().nonnegative(),
  nativeDeveloperDebuggerProcessScriptCursors: z.number().int().nonnegative(),
  nativeDeveloperDebuggerPhysical: z.number().int().nonnegative(),
  nativeDebuggerDeveloperReceipts: z.number().int().nonnegative(),
})
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
/** @type {import('../dist/native.js').NativeDebuggerSession|undefined} */ let session
try {
  await page.goto(`http://127.0.0.1:${address.port}/`)
  session = await page.createDebuggerSession()
  const first = await session.scripts(undefined, { timeout: 0 })
  assert.equal(first.scripts.length, 64)
  assert(first.next)
  const second = await session.scripts(first.next)
  const replay = await session.scripts(first.next)
  assert.deepEqual(replay.scripts, second.scripts)
  checks.push(
    'Fresh no-coverage current root lists64 actual metadata records and replays a cursor deterministically',
  )

  const records = [...first.scripts, ...second.scripts]
  let cursor = second.next
  while (cursor) {
    const next = await session.scripts(cursor)
    records.push(...next.scripts)
    cursor = next.next
  }
  assert.equal(new Set(records.map((record) => record.scriptId)).size, records.length)
  assert.deepEqual(
    records.map((record) => record.scriptId),
    records.map((record) => record.scriptId).sort(),
  )
  const authorRecords = records.filter((record) => /\/author-\d+\.js$/u.test(record.url))
  assert.equal(authorRecords.length, count)
  for (const record of authorRecords) {
    const index = Number(/\/author-(\d+)\.js$/u.exec(record.url)?.[1])
    assert.equal(await session.scriptSource(record.scriptId), author(index))
  }
  checks.push(
    'Actual source requests verify all90 served author IDs without cloned source snapshots or duplicate pages',
  )

  const selected = authorRecords.find((record) => record.url.endsWith('/author-3.js'))
  assert(selected && !selected.attestedNative)
  const breakpoint = await session.setBreakpoint({ scriptId: selected.scriptId, lineNumber: 0 })
  assert.equal(breakpoint.location.scriptId, selected.scriptId)
  await session.removeBreakpoint(breakpoint)
  const native = records.find((record) => record.attestedNative)
  assert(native, 'Actual factory FunctionLocation attestation must be present')
  assert((await session.scriptSource(native.scriptId)).length > 0)
  await assert.rejects(
    session.setBreakpoint({ scriptId: native.scriptId, lineNumber: 0 }),
    /actual current author script/u,
  )
  checks.push(
    'Listed actual author ID permits genuine breakpoint mutation; read-only attested native ID remains ineligible',
  )

  await page.evaluate(
    `(()=>{const script=document.createElement('script');script.textContent='globalThis.actualInventoryAdded=1;\\n//# sourceURL=inventory-added';document.head.appendChild(script);return 1})()`,
  )
  await assert.rejects(session.scripts(first.next), /changed.*restart/u)
  const changed = await session.scripts()
  assert(changed.next)
  checks.push('Real root scriptParsed addition makes the exact prior inventory cursor fail stale')

  await page.evaluate(
    `(()=>{const script=document.createElement('script');script.textContent='globalThis.actualInventoryUnits=1;\\n//# sourceURL=inventory-units-'+String.fromCharCode(55296);document.head.appendChild(script);return 1})()`,
  )
  /** @type {import('../dist/native.js').NativeDebuggerScript[]} */ const unitsRecords = []
  let units = await session.scripts()
  while (true) {
    unitsRecords.push(...units.scripts)
    if (!units.next) break
    units = await session.scripts(units.next)
  }
  assert(unitsRecords.some((record) => record.url === 'inventory-units-\ud800'))
  checks.push(
    'Actual protocol sourceURL isolated UTF-16 unit survives native metadata projection and SDK decoding',
  )

  const beforeClose = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert(beforeClose.nativeDeveloperDebuggerScriptCursors > 0)
  assert(beforeClose.nativeDeveloperDebuggerScriptCursors <= 256)
  await session.close()
  session = undefined
  const closed = stateSchema.parse(await source.runAutomation('diagnostics'))
  assert.equal(closed.nativeDeveloperDebuggerScriptCursors, 0)
  assert.equal(closed.nativeDeveloperDebuggerSessions, 0)
  assert.equal(closed.nativeDeveloperDebuggerPhysical, 0)
  assert.equal(closed.nativeDebuggerDeveloperReceipts, 0)
  await source.close()
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  checks.push(
    'Actual canonical session cleanup frees logical cursor positions and real helper exits0 with no accepted developer receipts',
  )
  assert.equal(checks.length, 6)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        checks,
        beforeClose,
        closed,
        binarySHA256: createHash('sha256')
          .update(await readFile(executablePath))
          .digest('hex'),
        actualProcessExit: source.processExit?.(),
        scope:
          'Actual current-root metadata, fail-stale native revision and independent exact-ID source/breakpoint checks; no source snapshot or cross-document/child/worker cursor claim',
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, error: String(error) }, null, 2) + '\n',
  )
  throw error
} finally {
  try {
    if (session && source.state !== 'closed')
      await session.close().catch((error) => errors.push(error))
    await finishNativeProof(source, diagnostic, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native actual script inventory6 passed')
