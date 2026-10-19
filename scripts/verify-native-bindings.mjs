// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = resolve('.suitecut/native-bindings')
await mkdir(directory, { recursive: true })
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html')
  response.end(request.url === '/frame' ? '<div>child</div>' : '<body>binding fixture</body>')
})
await new Promise((done) => server.listen(0, '0.0.0.0', done))
const address = server.address()
assert(address && typeof address === 'object')
const url = `http://127.0.0.1:${address.port}`
const hostGate = new AbortController()
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
try {
  assert(
    typeof source.runContext === 'function' &&
      typeof source.pageSource === 'function' &&
      typeof source.onContextEvent === 'function',
  )
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const page = await context.newPage()
  const checks = []
  await page.exposeFunction('multiply', (a, b) => z.number().parse(a) * z.number().parse(b))
  assert.equal(await page.evaluate('multiply(9,4)'), 36)
  checks.push('native private binding round trip')
  console.log('passed first native binding round trip')
  let receivedSource = { context, page, frame: page.mainFrame() }
  let sourceCalls = 0
  await page.exposeBinding('withSource', (where, a) => {
    receivedSource = where
    sourceCalls++
    return a
  })
  assert.equal(await page.evaluate('withSource(42)'), 42)
  assert.equal(sourceCalls, 1)
  assert.equal(receivedSource.context, context)
  assert.equal(receivedSource.page, page)
  assert.equal(receivedSource.frame, page.mainFrame())
  checks.push('binding owns actual context page and main frame')
  await page.exposeFunction('throwNull', () => {
    // Preserve the public arbitrary-thrown-value contract.
    // eslint-disable-next-line @typescript-eslint/only-throw-error
    throw null
  })
  assert.equal(await page.evaluate('throwNull().catch(error=>error)'), null)
  await page.exposeFunction('throwError', () => {
    throw new Error('host failure')
  })
  assert.deepEqual(
    await page.evaluate(
      "throwError().catch(error=>({message:error.message,stack:error.stack.includes('verify-native-bindings.mjs')}))",
    ),
    { message: 'host failure', stack: true },
  )
  checks.push('host exceptions preserve null message and stack')
  const handle = await page.evaluateHandle('globalThis.original={value:7}')
  await page.exposeFunction('returnHandle', () => [{ nested: handle }])
  assert.equal(await page.evaluate('returnHandle().then(value=>value[0].nested===original)'), true)
  await handle.dispose()
  checks.push('callback adopts real native handle into result')
  const largeHandle = await page.evaluateHandle('globalThis.largeIdentity={native:true}')
  await page.exposeFunction('largeValue', () => ({
    body: '🙂'.repeat(150000),
    identity: largeHandle,
  }))
  assert.deepEqual(
    await page.evaluate(
      'largeValue().then(value=>({bytes:new TextEncoder().encode(value.body).byteLength,length:value.body.length,identity:value.identity===largeIdentity}))',
    ),
    { bytes: 600000, length: 300000, identity: true },
  )
  await largeHandle.dispose()
  await page.exposeFunction('oversizedValue', () => 'x'.repeat(2 * 1024 * 1024))
  await assert.rejects(page.evaluate('oversizedValue()'), /result exceeds 1 MiB/u)
  const stagedState = z
    .object({ bindingResultBytes: z.number() })
    .parse(await page.source.runAutomation('diagnostics'))
  assert.equal(stagedState.bindingResultBytes, 0)
  checks.push('chunked Unicode result preserves exact native handle identity')
  checks.push('oversized result rejects and staged bytes return to zero')
  await assert.rejects(
    page.exposeFunction('multiply', () => 0),
    /already registered/u,
  )
  const exposed = await page.exposeFunction('disposable', () => 1)
  assert.equal(await page.evaluate('disposable()'), 1)
  await exposed.dispose()
  assert.equal(await page.evaluate('typeof disposable'), 'undefined')
  checks.push('duplicate registration and disposal')
  let received = {}
  await page.exposeFunction('observe', (arg) => {
    received = arg
    return true
  })
  await page.evaluate('const cyclic={};cyclic.self=cyclic;observe(cyclic)')
  assert(received && typeof received === 'object')
  assert.equal(Reflect.get(received, 'self'), received)
  await page.evaluate('observe([window,document,document.body])')
  assert.deepEqual(received, ['ref: <Window>', 'ref: <Document>', 'ref: <Node>'])
  checks.push('complex cycles and browser object aliases')
  await page.goto(url)
  assert.equal(await page.evaluate('multiply(3,5)'), 15)
  await page.goto(`http://localhost:${address.port}`)
  assert.equal(await page.evaluate('multiply(2,6)'), 12)
  await page.setContent('<script>window.setContentResult=multiply(3,2)</script>')
  assert.equal(await page.evaluate('setContentResult'), 6)
  checks.push('bindings survive navigation cross origin and setContent')
  await page.setContent(`<iframe src="${url}/frame"></iframe>`)
  await page.waitForTimeout(100)
  const child = page.frames().find((frame) => frame !== page.mainFrame())
  assert(child)
  assert.equal(await child.evaluate('multiply(3,7)'), 21)
  await child.evaluate('withSource(11)')
  assert.equal(receivedSource.frame, child)
  checks.push('existing and future child frame binding ownership')
  await page.exposeFunction('frameAfter', () => 22)
  assert.equal(await child.evaluate('frameAfter()'), 22)
  checks.push('registration installs into existing child frames')
  const startedSignal = new AbortController()
  const started = new Promise((done) =>
    startedSignal.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  await page.exposeFunction('pendingDetach', async () => {
    startedSignal.abort()
    await new Promise((done) =>
      hostGate.signal.addEventListener('abort', () => done(undefined), { once: true }),
    )
    return 42
  })
  const pending = child.evaluate('pendingDetach()').then(
    () => 'unexpected',
    (error) => (error instanceof Error ? error : new Error('Unexpected non-Error rejection')),
  )
  await started
  await page.evaluate(() => document.querySelector('iframe')?.remove())
  assert((await pending) instanceof Error)
  const canceled = z
    .object({ bindingCalls: z.number(), bindingArgumentBytes: z.number() })
    .parse(await page.source.runAutomation('diagnostics'))
  assert.deepEqual(canceled, { bindingCalls: 0, bindingArgumentBytes: 0 })
  hostGate.abort()
  await page.waitForTimeout(20)
  checks.push('frame detach releases native calls and suppresses late host reply')
  await page.goto(url)
  await page.addInitScript('globalThis.initCall=multiply(4,5)')
  await page.goto(page.url())
  assert.equal(await page.evaluate('initCall'), 20)
  checks.push('binding exists before authored initialization scripts')
  await page.evaluate('console=null;Array.prototype.map=null;Array.prototype.push=null')
  await page.exposeFunction('poisonProof', (a, b) => z.number().parse(a) + z.number().parse(b))
  assert.equal(await page.evaluate('poisonProof(5,6)'), 11)
  checks.push('overridden console and array map push')
  await page.goto(url)
  await page.evaluateHandle('Array.prototype.toJSON=()=>\'"[]"\'').then((value) => value.dispose())
  await assert.rejects(page.evaluate('multiply(2,2)'), /serializedArgs is not an array/u)
  checks.push('malformed array serialization fails explicitly')
  await page.goto(url)
  await Promise.all([
    page.exposeFunction('parallelA', () => 1),
    page.exposeBinding('parallelB', () => 2),
  ])
  assert.deepEqual(await page.evaluate('Promise.all([parallelA(),parallelB()])'), [1, 2])
  checks.push('parallel binding registrations')
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      bindingCalls: z.number(),
      bindingArgumentBytes: z.number(),
      bindingPendingRegistrations: z.number(),
      pendingBindingCommands: z.number(),
    })
    .parse(await page.source.runAutomation('diagnostics'))
  assert.deepEqual(state, {
    otherOperations: 0,
    evaluationGroups: 0,
    bindingCalls: 0,
    bindingArgumentBytes: 0,
    bindingPendingRegistrations: 0,
    pendingBindingCommands: 0,
  })
  const report = { cefVersion: source.cefVersion, checks, state }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
  await context.close()
} finally {
  hostGate.abort()
  await source.close()
  await new Promise((done, fail) => server.close((error) => (error ? fail(error) : done())))
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
