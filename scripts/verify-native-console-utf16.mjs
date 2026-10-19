// SPDX-License-Identifier: MIT
/* global Worker, SharedWorker, navigator, document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = resolve('.suitecut/native-console-utf16')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const server = createServer((request, response) => {
  if (request.url === '/favicon.ico') {
    response.writeHead(204)
    response.end()
    return
  }
  response.setHeader('content-type', request.url?.endsWith('.js') ? 'text/javascript' : 'text/html')
  if (request.url === '/shared.js')
    response.end('onconnect=event=>{globalThis.port=event.ports[0];port.start();};')
  else if (request.url === '/service.js')
    response.end('oninstall=()=>skipWaiting();onactivate=event=>event.waitUntil(clients.claim());')
  else if (request.url === '/dedicated.js') response.end('onmessage=()=>{};')
  else response.end('<body>Genuine native console units</body>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const value = '\ud800a\0\\"\udfff😀'
const checks = []
/**
 * Inspect a real native console primitive without changing its author value.
 * @param {import('../dist/native.js').NativeConsoleMessage} message
 */
async function inspectString(message) {
  assert.equal(message.text(), value)
  const args = message.args()
  assert.equal(args.length, 1)
  const handle = args[0]
  assert(handle)
  try {
    assert.equal(await handle.jsonValue(), value)
  } finally {
    await handle.dispose()
  }
}
try {
  const page = createNativePage(source)
  await page.goto(base)
  const main = page.waitForEvent('console', {
    predicate: (message) => message.text().startsWith(value),
    timeout: 5000,
  })
  await page.evaluate((text) => {
    const object = { __suitecutNativeUTF16Scalar: 'd800', native: text }
    Reflect.set(globalThis, 'authorConsoleObject', object)
    console.log(text, object)
  }, value)
  const message = await main
  assert(message.text().startsWith(value + ' '))
  const args = message.args()
  assert.equal(args.length, 2)
  try {
    assert.equal(await args[0]?.jsonValue(), value)
    assert.deepEqual(await args[1]?.jsonValue(), {
      __suitecutNativeUTF16Scalar: 'd800',
      native: value,
    })
    assert.equal(
      await args[1]?.evaluate(
        (object) => object === Reflect.get(globalThis, 'authorConsoleObject'),
      ),
      true,
    )
  } finally {
    await Promise.all(args.map((handle) => handle.dispose()))
  }
  checks.push(
    'genuine main-frame string and real object identity preserve isolated/pair/NUL/backslash/quote units and author tag-looking object',
  )
  const nodeWait = page.waitForEvent('console', {
    predicate: (entry) => entry.text().startsWith(value),
    timeout: 5000,
  })
  await page.evaluate((text) => {
    const element = document.createElement('div')
    element.id = text
    document.body.append(element)
    console.log(text, element)
  }, value)
  const nodeArgs = (await nodeWait).args()
  try {
    assert.equal(await nodeArgs[0]?.jsonValue(), value)
    assert.equal(
      await nodeArgs[1]?.evaluate((node) => (node instanceof Element ? node.id : undefined)),
      value,
    )
  } finally {
    await Promise.all(nodeArgs.map((handle) => handle.dispose()))
  }
  checks.push(
    'genuine DOM argument preserves actual node identity when inspector description itself has isolated units',
  )
  const records = await page.consoleMessages({ filter: 'all' })
  assert(records.some((record) => record.text().startsWith(value + ' ')))
  checks.push('bounded native page journal returns exact UTF16 text and previews')
  const childWait = page.waitForEvent('framenavigated', {
    predicate: (frame) => frame.url() === `http://localhost:${address.port}/child`,
    timeout: 5000,
  })
  await page.setContent(`<iframe src="http://localhost:${address.port}/child"></iframe>`)
  const child = await childWait
  const childConsole = page.waitForEvent('console', {
    predicate: (entry) => entry.text() === value,
    timeout: 5000,
  })
  await child.evaluate((text) => console.log(text), value)
  await inspectString(await childConsole)
  checks.push(
    'genuine cross-origin child-frame console event retains exact frame and primitive ownership',
  )
  await page.goto(base)
  const dedicatedWait = page.waitForEvent('worker', { timeout: 5000 })
  await page.evaluate(() => {
    Reflect.set(globalThis, 'retainedDedicated', new Worker('/dedicated.js'))
  })
  const dedicated = await dedicatedWait
  const dedicatedConsole = page.waitForEvent('console', {
    predicate: (entry) => entry.text() === value,
    timeout: 5000,
  })
  await dedicated.evaluate((text) => console.log(text), value)
  await inspectString(await dedicatedConsole)
  checks.push(
    'genuine dedicated-worker console event adopts string in its original native Runtime session',
  )
  const context = await new NativeBrowserContexts(source).newContext()
  try {
    const contextPage = await context.newPage()
    await contextPage.goto(base)
    const contextConsole = context.waitForEvent('console', { timeout: 5000 })
    await contextPage.evaluate((text) => console.log(text), value)
    await inspectString(await contextConsole)
    checks.push('native context console waiter and public packet preserve exact page scalar')
    const sharedWait = context.waitForSharedWorker({ timeout: 5000 })
    await contextPage.evaluate(() => {
      Reflect.set(globalThis, 'retainedShared', new SharedWorker('/shared.js'))
    })
    const shared = await sharedWait
    const sharedConsole = context.waitForEvent('console', { timeout: 5000 })
    await shared.evaluate((text) => console.log(text), value)
    await inspectString(await sharedConsole)
    checks.push(
      'genuine private shared-worker broker console event and real primitive handle preserve units',
    )
    const serviceWait = context.waitForServiceWorker({ timeout: 5000 })
    await contextPage.evaluate(async () => {
      await navigator.serviceWorker.register('/service.js')
    })
    const service = await serviceWait
    const serviceConsole = context.waitForEvent('console', { timeout: 5000 })
    await service.evaluate((text) => console.log(text), value)
    await inspectString(await serviceConsole)
    checks.push('genuine private service-worker broker console event and handle preserve units')
  } finally {
    await context.close()
  }
  await page.clearConsoleMessages()
  await page.waitForTimeout(20)
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      nativeWorkerPhysical: z.number(),
      nativeWorkerHandles: z.number(),
      pendingConsoleArguments: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const count of Object.values(state)) assert.equal(count, 0)
  const report = {
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
  await new Promise((done) => server.close(done))
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
