// SPDX-License-Identifier: MIT
/* global document, requestAnimationFrame */
import assert from 'node:assert/strict'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-control-verification')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    '<button id="button" onclick="this.textContent=\'clicked\'">button</button><input id="input" disabled><div id="cover" style="position:fixed;inset:0;background:white"></div>',
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
let nativeDiagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 640,
  height: 360,
  headless: true,
  onDiagnostic: (message) => {
    if (nativeDiagnostics.length < 1024 * 1024)
      nativeDiagnostics += message.slice(0, 1024 * 1024 - nativeDiagnostics.length)
  },
}).catch((error) => {
  server.close()
  throw error
})
try {
  const page = createNativePage(source)
  const url = `http://127.0.0.1:${address.port}`
  await page.goto(url)
  const click = page.locator('#button').click()
  assert.equal(await page.evaluate(() => document.querySelector('button').textContent), 'button')
  await page.evaluate(() => document.querySelector('#cover').remove())
  await click
  assert.equal(await page.locator('button').textContent(), 'clicked')
  const fill = page.locator('input').fill('native input')
  assert.equal(await page.locator('input').inputValue(), '')
  await page.evaluate(() => {
    document.querySelector('input').disabled = false
  })
  await fill
  assert.equal(await page.locator('input').inputValue(), 'native input')
  await assert.rejects(
    source.runAutomation('locator', { selector: '#missing', action: 'click', timeoutMs: 50 }),
    /timed out/,
  )
  const cancellation = new AbortController()
  const pending = source.runAutomation(
    'locator',
    { selector: '#canceled', action: 'click', timeoutMs: 0 },
    cancellation.signal,
  )
  const canceled = assert.rejects(pending, /deliberate cancel/)
  cancellation.abort(new Error('deliberate cancel'))
  await canceled
  await page.evaluate(() =>
    document.body.insertAdjacentHTML(
      'beforeend',
      '<button id="canceled" onclick="this.textContent=\'bad\'">unclicked</button>',
    ),
  )
  assert.equal(await page.locator('#canceled').textContent(), 'unclicked')
  await page.setContent('<input type=date>')
  await page.fill('input', '2026-10-13')
  assert.equal(await page.locator('input').inputValue(), '2026-10-13')
  await assert.rejects(page.fill('input', 'not-a-date'), /Malformed value/)
  const heapSchema = z.object({ usedSize: z.number(), totalSize: z.number() })
  await source.sendDevToolsCommand('HeapProfiler.collectGarbage')
  const rendererBefore = heapSchema.parse(await source.sendDevToolsCommand('Runtime.getHeapUsage'))
  const errorIterations = z.coerce
    .number()
    .int()
    .min(1)
    .max(1000000)
    .parse(process.argv[2] ?? 5000)
  for (let index = 0; index < errorIterations; index++)
    await assert.rejects(
      page.evaluate(() => {
        throw new Error('expected evaluation failure')
      }),
      /expected evaluation failure/,
    )
  console.log(`Completed ${errorIterations} evaluation errors`)
  const handleIterations = 1000
  for (let index = 0; index < handleIterations; index++) {
    const handle = await page.evaluateHandle(() => ({ value: 42, date: new Date('2026-10-13') }))
    assert.equal(
      await handle.evaluate((value) => {
        if (!value || typeof value !== 'object' || !('value' in value))
          throw new Error('Missing handle value')
        return value.value
      }),
      42,
    )
    const decoded = z
      .object({ value: z.literal(42), date: z.date() })
      .parse(await handle.jsonValue())
    assert.equal(decoded.date.toISOString(), '2026-10-13T00:00:00.000Z')
    assert.equal(
      await page.evaluate(({ first, repeated }) => first === repeated[0], {
        first: handle,
        repeated: [handle],
      }),
      true,
    )
    const property = await handle.getProperty('value')
    assert.equal(await property.jsonValue(), 42)
    await property.dispose()
    const properties = await handle.getProperties()
    assert.deepEqual([...properties.keys()], ['value', 'date'])
    for (const property of properties.values()) await property.dispose()
    await handle.dispose()
    await handle.dispose()
    await assert.rejects(handle.jsonValue(), /disposed/)
  }
  const capacityHandles = []
  console.log(`Completed ${handleIterations} nested/property handle cycles`)
  for (let index = 0; index < 1024; index++) capacityHandles.push(await page.evaluateHandle('42'))
  await assert.rejects(page.evaluateHandle('42'), /handle limit exceeded/)
  for (const handle of capacityHandles) await handle.dispose()
  const capacityRecovery = await page.evaluateHandle('42')
  assert.equal(await capacityRecovery.jsonValue(), 42)
  await capacityRecovery.dispose()
  console.log('Completed handle-capacity saturation and recovery')
  const lateHandleIterations = 1000
  for (let index = 0; index < lateHandleIterations; index++) {
    const controller = new AbortController()
    const pending = source.runAutomation(
      'handle',
      {
        action: 'create',
        expression:
          'new Promise(resolve=>requestAnimationFrame(()=>resolve({value:42,payload:Array.from({length:8192},(_,i)=>i)})))',
      },
      controller.signal,
    )
    const canceled = assert.rejects(pending, /cancel late handle/)
    controller.abort(new Error('cancel late handle'))
    await canceled
  }
  // Drain deferred browser completion before measuring released remote groups.
  await page.evaluate(async () => {
    await new Promise(requestAnimationFrame)
    await new Promise(requestAnimationFrame)
  })
  await source.sendDevToolsCommand('HeapProfiler.collectGarbage')
  const rendererAfter = heapSchema.parse(await source.sendDevToolsCommand('Runtime.getHeapUsage'))
  assert(
    rendererAfter.usedSize - rendererBefore.usedSize < 4 * 1024 * 1024,
    'Renderer retains excessive evaluation-error state after garbage collection',
  )
  const nativeState = z
    .object({
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      pendingDevTools: z.number(),
      otherOperations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(nativeState, {
    activeHandles: 0,
    pendingHandles: 0,
    evaluationGroups: 0,
    pendingDevTools: 0,
    otherOperations: 0,
  })
  const stale = await page.evaluateHandle('({value:42})')
  await page.goto(url)
  await assert.rejects(stale.jsonValue(), /disposed|navigation|context/)
  await stale.dispose()
  for (let i = 0; i < 2000; i++) createNativePage(source)
  await setImmediate()
  const collect = globalThis.gc
  assert(typeof collect === 'function', 'Run this verifier with node --expose-gc')
  collect()
  const sdkBefore = process.memoryUsage().heapUsed
  const wrapperIterations = 100000
  for (let i = 0; i < wrapperIterations; i++) createNativePage(source)
  await setImmediate()
  collect()
  const sdkAfter = process.memoryUsage().heapUsed
  assert(
    sdkAfter - sdkBefore < 4 * 1024 * 1024,
    'Discarded page wrappers retain lifecycle observers',
  )
  const controlSource = new AbortController()
  const detachedPage = createNativePage(source, controlSource.signal)
  controlSource.abort(new Error('page lifetime ended'))
  await assert.rejects(detachedPage.waitForTimeout(1000), /aborted/)
  const report = {
    cefVersion: source.cefVersion,
    platform: process.platform,
    nativeClickWaiting: 'passed',
    nativeFillWaiting: 'passed',
    nativeDeadlines: 'passed',
    nativeCancellation: 'passed',
    specializedFill: 'passed',
    errorIterations,
    handleIterations,
    nestedHandleIterations: handleIterations,
    propertyIterations: handleIterations,
    handleCapacity: 1024,
    handleCapacityRecovery: 'passed',
    lateHandleIterations,
    navigationInvalidation: 'passed',
    nativeState,
    rendererBefore,
    rendererAfter,
    rendererHeapDelta: rendererAfter.usedSize - rendererBefore.usedSize,
    wrapperIterations,
    sdkHeapDelta: sdkAfter - sdkBefore,
    note: 'These are targeted live state-retention checks, not proof that the entire CEF engine is leak-free.',
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, nativeDiagnostics)
  server.closeAllConnections()
  server.close()
}
