// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = resolve('.suitecut/native-binding-capacity')
await mkdir(directory, { recursive: true })
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 640,
  height: 480,
})
const gate = new AbortController()
try {
  assert(
    typeof source.runContext === 'function' &&
      typeof source.pageSource === 'function' &&
      typeof source.onContextEvent === 'function',
  )
  const context = await new NativeBrowserContexts(source).newContext()
  const page = await context.newPage()
  const shared = await page.evaluateHandle('globalThis.sharedBindingObject={native:true}')
  let startedCount = 0
  const startedSignal = new AbortController()
  const started = new Promise((done) =>
    startedSignal.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  await page.exposeFunction('saturatedBinding', async () => {
    const released = new Promise((done) =>
      gate.signal.addEventListener('abort', () => done(undefined), { once: true }),
    )
    if (++startedCount === 64) startedSignal.abort()
    await released
    return shared
  })
  const evaluations = Array.from({ length: 64 }, () =>
    page.evaluate('saturatedBinding().then(value=>value===sharedBindingObject)'),
  )
  await started
  assert.equal(startedCount, 64)
  // Every ordinary evaluation is still awaiting its renderer promise. Native
  // binding acknowledgments must therefore use reserved control ownership.
  gate.abort()
  assert.deepEqual(
    await Promise.all(evaluations),
    Array.from({ length: 64 }, () => true),
  )
  await page.waitForTimeout(20)
  await shared.dispose()
  const state = z
    .object({
      otherOperations: z.number(),
      bindingControlOperations: z.number(),
      bindingCalls: z.number(),
      bindingArgumentBytes: z.number(),
      bindingResultBytes: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await page.source.runAutomation('diagnostics'))
  assert.deepEqual(state, {
    otherOperations: 0,
    bindingControlOperations: 0,
    bindingCalls: 0,
    bindingArgumentBytes: 0,
    bindingResultBytes: 0,
    evaluationGroups: 0,
    activeHandles: 0,
    pendingHandles: 0,
  })
  const report = {
    cefVersion: source.cefVersion,
    simultaneousOrdinaryEvaluations: 64,
    exactNativeHandleResults: 64,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  gate.abort()
  await source.close()
}
