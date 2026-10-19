// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-debugger-unowned-pauses',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(`<!doctype html><button id=target>Actual native pause receipt</button><script>
    globalThis.actualTransfer=new DataTransfer();globalThis.actualDrops=0;
    document.querySelector('#target').addEventListener('drop',event=>{if(event.dataTransfer===globalThis.actualTransfer)++globalThis.actualDrops});
  </script>`)
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(3000)
const state = z.object({
  nativeDebuggerUnownedPauses: z.number().int().nonnegative(),
  nativeDebuggerDeferredUnownedPauses: z.number().int().nonnegative(),
  nativeDebuggerLeases: z.number().int().nonnegative(),
  nativeDebuggerPhysical: z.number().int().nonnegative(),
  nativeDebuggerFactoryReceipts: z.number().int().nonnegative(),
  nativeDebuggerPauses: z.number().int().nonnegative(),
  nativeCoveragePhysical: z.number().int().nonnegative(),
  pristineDispatchContexts: z.number().int().nonnegative(),
  pristineDispatchGroups: z.number().int().nonnegative(),
  pristineDispatchPhysical: z.number().int().nonnegative(),
  activeHandles: z.number().int().nonnegative(),
})
/** Observe actual native physical receipts; no delay manufactures a presentation or pause. */
async function settled() {
  const end = performance.now() + 3000
  while (true) {
    const value = state.parse(await source.runAutomation('diagnostics'))
    if (
      value.nativeDebuggerPhysical === 0 &&
      value.nativeDebuggerFactoryReceipts === 0 &&
      value.nativeDebuggerPauses === 0 &&
      value.nativeCoveragePhysical === 0 &&
      value.pristineDispatchPhysical === 0
    )
      return value
    assert(
      performance.now() < end,
      'Actual debugger/factory/coverage physical receipts did not settle',
    )
  }
}
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
try {
  await page.goto(`http://127.0.0.1:${address.port}/`)
  // Coverage diagnostics are published only after the genuine lazy manager
  // exists. Complete an actual recording before the factory-only baseline;
  // absent fields must not be manufactured into zero physical receipts.
  await page.coverage.startJSCoverage()
  await page.coverage.stopJSCoverage()
  const initial = await settled()
  assert(
    initial.pristineDispatchContexts > 0 &&
      initial.pristineDispatchGroups > 0 &&
      initial.nativeDebuggerLeases > 0,
  )
  const transfer = await page.evaluateHandle('globalThis.actualTransfer')
  assert.equal(await page.evaluate('(()=>{debugger;debugger;return 41+1})()'), 42)
  const factoryOnly = await settled()
  assert(
    factoryOnly.nativeDebuggerUnownedPauses >= initial.nativeDebuggerUnownedPauses + 2,
    'Two genuine unowned Inspector pause receipts required; returning JS alone is insufficient',
  )
  assert.equal(factoryOnly.nativeDebuggerLeases, initial.nativeDebuggerLeases)
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops'), 1)
  checks.push(
    'Two genuine author debugger pauses auto-resume while the same factory dispatches the actual DataTransfer',
  )

  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  const active = await settled()
  assert.equal(active.nativeDebuggerLeases, initial.nativeDebuggerLeases + 1)
  assert.equal(
    await page.evaluate(
      'function actualUnownedPauseCoverage(){debugger;debugger;return 6*7};actualUnownedPauseCoverage()',
    ),
    42,
  )
  const afterAuthor = await settled()
  assert(
    afterAuthor.nativeDebuggerUnownedPauses >= factoryOnly.nativeDebuggerUnownedPauses + 2,
    'Active coverage must preserve actual factory skip=false pause delivery',
  )
  const entries = await page.coverage.stopJSCoverage()
  assert(
    entries.some((entry) =>
      entry.functions.some(
        (fn) =>
          fn.functionName === 'actualUnownedPauseCoverage' &&
          fn.ranges.some((range) => range.count > 0),
      ),
    ),
  )
  const stopped = await settled()
  assert.equal(stopped.nativeDebuggerLeases, initial.nativeDebuggerLeases)
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops'), 2)
  checks.push(
    'Real executed coverage survives two actual unowned pauses and stopping coverage preserves the same factory lease',
  )

  await transfer.dispose()
  const final = await settled()
  assert.equal(final.activeHandles, 0)
  assert.equal(final.pristineDispatchContexts, initial.pristineDispatchContexts)
  assert.equal(final.pristineDispatchGroups, initial.pristineDispatchGroups)
  const deferred =
    final.nativeDebuggerDeferredUnownedPauses - initial.nativeDebuggerDeferredUnownedPauses
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: createHash('sha256')
          .update(await readFile(executablePath))
          .digest('hex'),
        checks,
        initial,
        final,
        actualDeferredUnownedPauses: deferred,
        unexercised:
          deferred === 0
            ? [
                'Exact next-pause-before-previous-resume-ACK ordering was not observed; controlled native regression covers it separately',
              ]
            : [],
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
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native actual unowned debugger pause coexistence2 passed')
