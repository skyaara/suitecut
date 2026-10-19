// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'
import { parseTraceRaw } from './native-trace-reader.mjs'

const directory = resolve('.suitecut/native-reference-tracing')
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: diagnostics.observe,
})
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
const actionSchema = z.object({
  callId: z.string(),
  class: z.literal('Frame'),
  method: z.literal('ariaSnapshot'),
  apiName: z.literal('Capture ARIA snapshot'),
  pageId: z.string(),
  startTime: z.number(),
  endTime: z.number(),
  params: z.object({
    query: z.array(z.object({ engine: z.literal('css'), selector: z.literal('#actual-snapshot') })),
    accessibilityBoxes: z.literal(true),
    referenceDepth: z.literal(3),
  }),
})
const ownership = z.object({
  nativeSnapshotReferences: z.number(),
  nativeSnapshotReferenceResidents: z.number(),
  nativeSnapshotReferenceGroups: z.number(),
  nativeSnapshotReferenceInstalls: z.number(),
  nativeSnapshotReferenceReleases: z.number(),
  nativeSnapshotReferenceBorrows: z.number(),
  nativeSnapshotReferenceDispatches: z.number(),
  nativeSnapshotReferenceProcessDispatches: z.number(),
  nativeSnapshotReferenceProcessInstalls: z.number(),
  nativeSnapshotReferenceProcessReleases: z.number(),
  nativeSnapshotReferenceProcessResidents: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  evaluationGroups: z.number(),
})
try {
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext(),
    page = await context.newPage()
  await page.setContent('<button id="actual-snapshot">Actual trace target</button>')
  await context.tracing.start({ snapshots: true, screenshots: true, sources: true })
  const yaml = await page
    .locator('#actual-snapshot')
    .ariaSnapshot({ mode: 'ai', boxes: true, depth: 3 })
  assert(yaml.includes('Actual trace target') && /\[ref=r[A-Za-z0-9]+_[1-9][0-9]*_0\]/u.test(yaml))
  checks.push(
    'the trace surrounds an actual successful native AI reference capture with real box metadata',
  )
  await page.goto('about:blank')
  const path = resolve(directory, 'actual.zip')
  await context.tracing.stop({ path })
  const raw = await parseTraceRaw(path),
    matches = raw.actionObjects.filter((action) => action.method === 'ariaSnapshot')
  assert.equal(matches.length, 1)
  const action = actionSchema.parse(matches[0])
  assert.equal(action.pageId, `page@${page.source.pageId}`)
  assert(action.endTime >= action.startTime)
  assert(!Object.hasOwn(z.record(z.string(), z.unknown()).parse(matches[0]), 'error'))
  checks.push(
    'the actual archive records one native ariaSnapshot action with its genuine page, original query/options, title and successful before/after receipt',
  )
  const ownedPhases = raw.events.filter((event) => {
    if (event.type !== 'frame-snapshot') return false
    const snapshot = z.record(z.string(), z.unknown()).parse(event.snapshot)
    return snapshot.callId === action.callId
  })
  assert.equal(ownedPhases.length, 0)
  const frames = z
    .array(
      z.object({ file: z.string(), line: z.number().positive(), column: z.number().positive() }),
    )
    .parse(raw.stacks.get(action.callId))
  assert(frames.some((frame) => frame.file === new URL(import.meta.url).pathname))
  checks.push(
    'read-only capture avoids recursive action DOM phases while retaining the real user caller stack',
  )
  const deadline = performance.now() + 1000
  let state = ownership.parse(await page.source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(1)
    state = ownership.parse(await page.source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await context.close()
  checks.push(
    'navigation and actual trace stop retire every tracked snapshot/native physical resource before context closure',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ completed: true, checks, action, state }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    directory + '/failure.json',
    JSON.stringify(
      {
        completed: false,
        checks,
        error:
          error instanceof Error
            ? { name: error.name, message: error.message, stack: error.stack }
            : String(error),
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
if (errors.length) throw errors[0]
