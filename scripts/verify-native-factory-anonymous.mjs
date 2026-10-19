// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-factory-anonymous',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const factoryHeader =
  process.env.SUITECUT_NATIVE_FACTORY_HEADER ??
  fileURLToPath(new URL('../native/browser/pristine_dispatch_factory.h', import.meta.url))
await mkdir(directory, { recursive: true })
const header = await readFile(factoryHeader, 'utf8')
const factory = /R"SCFACTORY\(([\s\S]*?)\)SCFACTORY";/u.exec(header)?.[1]
assert(factory, 'Actual sealed native factory source is unavailable')
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end('<!doctype html><button id=target>Actual anonymous provenance</button>')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(5000)
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
try {
  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  // The native factory executes in a fresh genuine document while profiling is active.
  await page.goto(`http://127.0.0.1:${address.port}/actual`)
  const actualFactory = z
    .object({
      pristineDispatchContexts: z.number().int().positive(),
      pristineDispatchGroups: z.number().int().positive(),
    })
    .parse(await source.runAutomation('diagnostics'))
  // This is an authored raw Runtime evaluation of byte-identical source and URL,
  // outside the native factory admission/group/provenance path.
  const group = 'actual-author-identical-factory-source'
  const evaluated = z
    .object({ result: z.object({ objectId: z.string().min(1), type: z.literal('function') }) })
    .parse(
      await source.sendDevToolsCommand('Runtime.evaluate', {
        expression: factory,
        returnByValue: false,
        objectGroup: group,
      }),
    )
  const properties = z
    .object({ internalProperties: z.array(z.object({ name: z.string(), value: z.unknown() })) })
    .parse(
      await source.sendDevToolsCommand('Runtime.getProperties', {
        objectId: evaluated.result.objectId,
        ownProperties: true,
        generatePreview: false,
      }),
    )
  const location = properties.internalProperties.find(
    (value) => value.name === '[[FunctionLocation]]',
  )
  assert(location)
  const author = z
    .object({
      subtype: z.literal('internal#location'),
      value: z.object({ scriptId: z.string().min(1) }),
    })
    .parse(location.value).value
  const coverage = await page.coverage.stopJSCoverage()
  const retained = coverage.find((entry) => entry.scriptId === author.scriptId)
  assert(retained, 'Identical authored anonymous replay was incorrectly filtered')
  assert.equal(retained.source, factory)
  assert.equal(retained.url, '')
  const identicalEntries = coverage.filter((entry) => entry.source === factory && entry.url === '')
  assert.deepEqual(
    identicalEntries.map((entry) => entry.scriptId),
    [author.scriptId],
    'Only the genuine author replay may retain these exact bytes/URL; native source oracle must not leak',
  )
  assert(retained.functions.some((fn) => fn.ranges.some((range) => range.count > 0)))
  checks.push(
    'Native factory source is excluded while byte-identical authored anonymous code retains its actual ID/source/empty URL/counts',
  )
  await source.sendDevToolsCommand('Runtime.releaseObjectGroup', { objectGroup: group })
  const transfer = await page.evaluateHandle('new DataTransfer()')
  await page.evaluate(
    "document.querySelector('#target').addEventListener('drop',event=>globalThis.actualSameTransfer=event.dataTransfer)",
  )
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(
    await transfer.evaluate(
      (value) =>
        value ===
        /** @type {typeof globalThis & {actualSameTransfer: DataTransfer}} */ (globalThis)
          .actualSameTransfer,
    ),
    true,
  )
  await transfer.dispose()
  const physical = z.object({
    nativeCoverageRecordings: z.number(),
    nativeCoveragePhysical: z.number(),
    nativeCoverageQueuedSources: z.number(),
    nativeCoverageSourceBytes: z.number(),
    nativeCoverageSharedPayloadBytes: z.number(),
    nativeCoverageBackendPhysical: z.number(),
    nativeDebuggerPhysical: z.number(),
    nativeDebuggerFactoryReceipts: z.number(),
    nativeDebuggerPauses: z.number(),
    pristineDispatchPhysical: z.number(),
    pristineDispatchRetiringGroups: z.number(),
    activeHandles: z.number(),
  })
  const settleEnd = performance.now() + 3000
  let finalOwnership
  while (true) {
    finalOwnership = physical.parse(await source.runAutomation('diagnostics'))
    if (Object.values(finalOwnership).every((value) => value === 0)) break
    assert(
      performance.now() < settleEnd,
      'Actual temporary coverage/factory/handle receipts did not settle',
    )
  }
  checks.push(
    'Stopping coverage and replaying identical author bytes preserve the actual retained native factory and genuine DataTransfer identity',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        scope:
          'Ordinary native source-exclusion oracle; direct production script ID attribution requires a separate read-only observer',
        binarySHA256: createHash('sha256')
          .update(await readFile(executablePath))
          .digest('hex'),
        factoryHeaderSHA256: createHash('sha256').update(header).digest('hex'),
        factorySourceSHA256: createHash('sha256').update(factory).digest('hex'),
        checks,
        actualFactory,
        authorScriptId: author.scriptId,
        identicalEntries,
        retained,
        finalOwnership,
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
console.info('Actual native anonymous exclusion2 passed')
