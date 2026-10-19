// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts, NativeBindings } from '../dist/native.js'

const directory = resolve('.suitecut/native-binding-physical')
await mkdir(directory, { recursive: true })
const options = {
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 640,
  height: 480,
}
const firstSource = await launchNativeBrowser(options)
const sources = new Set([firstSource])
const finish = new Set([new AbortController()])
finish.clear()
const launch = async (source = firstSource) => {
  sources.add(source)
  assert(
    typeof source.runContext === 'function' &&
      typeof source.pageSource === 'function' &&
      typeof source.onContextEvent === 'function',
  )
  const context = await new NativeBrowserContexts(source).newContext()
  return { source, page: await context.newPage() }
}
try {
  const first = await launch()
  const startedSignal = new AbortController()
  const started = new Promise((done) =>
    startedSignal.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  await first.page.exposeFunction('physicalHostWork', async () => {
    const gate = new AbortController()
    const released = new Promise((done) =>
      gate.signal.addEventListener('abort', () => done(undefined), { once: true }),
    )
    finish.add(gate)
    if (finish.size === 128) startedSignal.abort()
    await released
    return 42
  })
  await first.page.evaluate('for(let i=0;i<128;i++) physicalHostWork(i).catch(()=>{}); true')
  await started
  assert.equal(NativeBindings.hostDiagnostics().physical, 128)
  await first.source.close()
  assert.equal(NativeBindings.hostDiagnostics().physical, 128)
  const second = await launch(await launchNativeBrowser(options))
  let invoked = 0
  await second.page.exposeFunction('freshHostWork', () => {
    invoked++
    return 42
  })
  await assert.rejects(second.page.evaluate('freshHostWork()'), /host binding callback capacity/u)
  assert.equal(invoked, 0)
  assert.equal(NativeBindings.hostDiagnostics().physical, 128)
  for (const gate of finish) gate.abort()
  await Promise.resolve()
  await Promise.resolve()
  assert.deepEqual(NativeBindings.hostDiagnostics(), { physical: 0, physicalBytes: 0 })
  assert.equal(await second.page.evaluate('freshHostWork()'), 42)
  assert.equal(invoked, 1)
  await second.page.waitForTimeout(20)
  assert.deepEqual(NativeBindings.hostDiagnostics(), { physical: 0, physicalBytes: 0 })
  const native = z
    .object({
      otherOperations: z.number(),
      bindingCalls: z.number(),
      bindingArgumentBytes: z.number(),
    })
    .parse(await second.page.source.runAutomation('diagnostics'))
  assert.deepEqual(native, { otherOperations: 0, bindingCalls: 0, bindingArgumentBytes: 0 })
  const report = {
    cefVersion: second.source.cefVersion,
    saturatedPhysical: 128,
    retiredHelperPhysical: 128,
    rejectedHostInvocations: 0,
    recoveredHostInvocations: invoked,
    sdk: NativeBindings.hostDiagnostics(),
    native,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  for (const gate of finish) gate.abort()
  await Promise.allSettled(Array.from(sources, (source) => source.close()))
}
