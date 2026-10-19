// SPDX-License-Identifier: MIT
/* global document, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  NativeBindings,
  NativeJSHandle,
  launchNativeBrowser,
} from '../dist/native.js'

const directory = resolve('.suitecut/native-binding-handles')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
const handles = new Set(z.array(z.instanceof(NativeJSHandle)).parse([]))
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent('<button id=save>Save</button>')
  let invocations = 0
  /**
   * Observe the actual native argument and its originating frame.
   * @param {import('../src/native-bindings.js').NativeBindingSource} info
   * @param {import('../src/untrusted.js').UntrustedInput} value
   */
  const returnHandle = (info, value) => {
    assert(value instanceof NativeJSHandle)
    assert.equal(info.page, page)
    assert.equal(info.context, page.context())
    handles.add(value)
    invocations++
    return value
  }
  await page.exposeBinding('retainArgument', returnHandle, { handle: true })
  assert.equal(
    await page.evaluate(
      '(async()=>await retainArgument(document.querySelector("#save"))===document.querySelector("#save"))()',
    ),
    true,
  )
  checks.push('native retained DOM argument returns the actual original browser node')
  assert.equal(
    await page.evaluate(
      'globalThis.bindingCycle={value:7};bindingCycle.self=bindingCycle;(async()=>{const result=await retainArgument(bindingCycle);return result===bindingCycle&&result.self===result})()',
    ),
    true,
  )
  checks.push(
    'native retained cyclic object preserves its actual object identity without value serialization',
  )
  /**
   * Observe the actual native argument and its originating frame.
   * @param {import('../src/native-bindings.js').NativeBindingSource} info
   * @param {import('../src/untrusted.js').UntrustedInput} value
   */
  const inspectArgument = async (info, value) => {
    assert(value instanceof NativeJSHandle)
    assert.equal(info.page, page)
    handles.add(value)
    return value.evaluate((argument) => argument === Reflect.get(window, 'originalBindingArgument'))
  }
  await page.exposeBinding('inspectArgument', inspectArgument, { handle: true })
  assert.equal(
    await page.evaluate(
      'globalThis.originalBindingArgument=new Promise(()=>{});inspectArgument(originalBindingArgument)',
    ),
    true,
  )
  checks.push(
    'unsettled Promise arguments are retained as actual Promise objects and never awaited during handle creation',
  )
  assert.equal(
    await page.evaluate(
      'globalThis.originalBindingArgument="λ".repeat(2*1024*1024);inspectArgument(originalBindingArgument)',
    ),
    true,
  )
  checks.push(
    'large primitive arguments stay native references while only bounded preview metadata crosses IPC',
  )
  assert.equal(
    await page.evaluate('globalThis.originalBindingArgument=undefined;inspectArgument()'),
    true,
  )
  checks.push('zero argument handle invocation supplies a genuine undefined handle')
  const prior = invocations
  await assert.rejects(page.evaluate('retainArgument(1,2)'), /only one argument/u)
  assert.equal(invocations, prior)
  checks.push('multiple arguments reject before native host callback admission')
  await page.setContent('<iframe srcdoc="<button id=child>Child</button>"></iframe>')
  const child = page.frames().find((frame) => frame !== page.mainFrame())
  assert(child)
  /**
   * Observe the actual native argument and its originating frame.
   * @param {import('../src/native-bindings.js').NativeBindingSource} info
   * @param {import('../src/untrusted.js').UntrustedInput} value
   */
  const childArgument = async (info, value) => {
    assert(value instanceof NativeJSHandle)
    assert.equal(info.frame, child)
    assert.equal(info.page, page)
    handles.add(value)
    return value.evaluate((argument) => argument === document.querySelector('#child'))
  }
  await page.exposeBinding('childArgument', childArgument, { handle: true })
  assert.equal(await child.evaluate('childArgument(document.querySelector("#child"))'), true)
  checks.push('native argument handles and callback source identify the actual child frame')
  for (const handle of handles) await handle.dispose()
  handles.clear()
  const started = new AbortController()
  const gate = new AbortController()
  /**
   * Observe the actual native argument and its originating frame.
   * @param {import('../src/native-bindings.js').NativeBindingSource} _info
   * @param {import('../src/untrusted.js').UntrustedInput} value
   */
  const detachedArgument = async (_info, value) => {
    assert(value instanceof NativeJSHandle)
    handles.add(value)
    started.abort()
    await new Promise((resolve) =>
      gate.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
    return 42
  }
  await page.exposeBinding('detachedArgument', detachedArgument, { handle: true })
  const pending = child.evaluate('detachedArgument(document.querySelector("#child"))').then(
    () => 'unexpected',
    (error) => (error instanceof Error ? error : new Error('Expected a native context rejection')),
  )
  if (!started.signal.aborted)
    await new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  await page.evaluate(() => document.querySelector('iframe')?.remove())
  assert((await pending) instanceof Error)
  assert.equal(NativeBindings.hostDiagnostics().physical, 1)
  gate.abort()
  await page.waitForTimeout(30)
  assert.equal(NativeBindings.hostDiagnostics().physical, 0)
  for (const handle of handles) await handle.dispose().catch(() => undefined)
  handles.clear()
  checks.push(
    'detached frames cancel logical handle calls while physical host work remains counted until settlement',
  )
  await page.goto('data:text/html,<button id=save>After navigation</button>')
  assert.equal(
    await page.evaluate(
      '(async()=>await retainArgument(document.querySelector("#save"))===document.querySelector("#save"))()',
    ),
    true,
  )
  for (const handle of handles) await handle.dispose()
  handles.clear()
  checks.push('handle-mode registration survives navigation with new native context ownership')
  const capacityGate = new AbortController()
  const entered = new AbortController()
  let count = 0
  /**
   * Observe the actual native argument and its originating frame.
   * @param {import('../src/native-bindings.js').NativeBindingSource} _info
   * @param {import('../src/untrusted.js').UntrustedInput} value
   */
  const atCapacity = async (_info, value) => {
    assert(value instanceof NativeJSHandle)
    handles.add(value)
    if (++count === 64) entered.abort()
    await new Promise((resolve) =>
      capacityGate.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
    return value
  }
  await page.exposeBinding('atCapacity', atCapacity, { handle: true })
  const evaluations = Array.from({ length: 64 }, () =>
    page.evaluate(
      '(async()=>await atCapacity(document.querySelector("#save"))===document.querySelector("#save"))()',
    ),
  )
  // Observe rejections immediately so cleanup never masks an earlier failure.
  const results = Promise.all(evaluations)
  void results.catch(() => undefined)
  if (!entered.signal.aborted)
    await new Promise((resolve) =>
      entered.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  // The 64 ordinary slots are occupied; a diagnostic request would require
  // another ordinary slot. Admission is observable without extra transport.
  assert.equal(count, 64)
  assert.equal(NativeBindings.hostDiagnostics().physical, 64)
  capacityGate.abort()
  assert.deepEqual(
    await results,
    Array.from({ length: 64 }, () => true),
  )
  for (const handle of handles) await handle.dispose()
  handles.clear()
  checks.push(
    '64 genuine waiting evaluations create and resolve real native argument handles through reserved control slots',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      bindingCalls: z.number(),
      bindingArgumentControls: z.number(),
      bindingArgumentBytes: z.number(),
      bindingControlOperations: z.number(),
      bindingResultBytes: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(state)) assert.equal(value, 0)
  assert.deepEqual(NativeBindings.hostDiagnostics(), { physical: 0, physicalBytes: 0 })
  const report = {
    cefVersion: source.cefVersion,
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
