// SPDX-License-Identifier: MIT
// Actual native standards caret/frame/input ownership and physical cancellation.
/* global document, XMLHttpRequest */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_SEND_KEYS_PROOF_DIRECTORY ?? '.suitecut/native-send-keys',
)
await mkdir(directory, { recursive: true })
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller chooses an exact locally sealed module hashed below.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
const inputs = await Promise.all(
  [
    executable,
    `${sdk}/native-page.js`,
    ...z
      .array(z.string())
      .parse(JSON.parse(process.env.SUITECUT_NATIVE_EXECUTABLE_DEPENDENCIES ?? '[]')),
  ].map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
/** @type {(()=>void)|undefined} */
let announceHeld
/** @type {(()=>void)|undefined} */
let releaseHeld
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  if (request.url === '/held') {
    releaseHeld = () => response.end('released')
    announceHeld?.()
  } else if (request.url === '/transparent' || request.url === '/below-scroll-container') {
    const input =
      request.url === '/transparent'
        ? '<input id=target style="opacity:0">'
        : '<div style="overflow-y:scroll;scroll-behavior:smooth;height:200px"><input id=target value=foo style="margin-top:2000vh"></div>'
    response.end(
      input +
        '<script>window.nativeKeyEvents=[];for(const type of ["keydown","input","keyup"])document.addEventListener(type,event=>nativeKeyEvents.push({type,target:event.target.id,trusted:event.isTrusted}),true);</script>',
    )
  } else
    response.end(
      '<!doctype html><input id=a value=a><input id=b value=b><iframe id=child></iframe>',
    )
})
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
let stderr = ''
const checks = []
const source = await native.launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 640,
  height: 480,
  onDiagnostic(line) {
    stderr += line.slice(0, Math.max(0, 1048576 - stderr.length))
  },
})
const browser = new native.NativeBrowserContexts(source)
const physicalSchema = z.object({
  otherOperations: z.number(),
  inputWork: z.number(),
  pendingSendKeysPhysicalCommands: z.number(),
  retainedSendKeysOwners: z.number(),
})
try {
  const context = await browser.newContext()
  const page = await context.newPage()
  for (const fixture of ['transparent', 'below-scroll-container']) {
    await page.goto(`${prefix}/${fixture}`)
    const target = page.locator('#target')
    await target.sendKeys(fixture === 'transparent' ? 'foo' : ' bar', { timeout: 1000 })
    assert.equal(await target.inputValue(), fixture === 'transparent' ? 'foo' : 'foo bar')
    const events = z
      .array(z.object({ type: z.string(), target: z.literal('target'), trusted: z.literal(true) }))
      .parse(
        await page.evaluate(
          () => /** @type {unknown} */ (Reflect.get(globalThis, 'nativeKeyEvents')),
        ),
      )
    assert(events.some((event) => event.type === 'keydown'))
    assert(events.some((event) => event.type === 'input'))
    assert(events.some((event) => event.type === 'keyup'))
    if (fixture === 'below-scroll-container')
      assert(
        await target.evaluate((node) => {
          const rect = /** @type {Element} */ (node).getBoundingClientRect()
          return rect.top >= 0 && rect.top < document.documentElement.clientHeight
        }),
      )
    checks.push(
      `New HTTP ${fixture} document receives genuine trusted keyboard events without screenshot/readback`,
    )
  }
  await page.goto(prefix)
  const a = await page.locator('#a').elementHandle()
  assert(a)
  await a.sendKeys('first', { timeout: 1000 })
  assert.equal(await a.evaluate((node) => /** @type {HTMLInputElement} */ (node).value), 'afirst')
  await a.evaluate((node) => /** @type {HTMLInputElement} */ (node).setSelectionRange(0, 0))
  await a.sendKeys('start')
  assert.equal(
    await a.evaluate((node) => /** @type {HTMLInputElement} */ (node).value),
    'startafirst',
  )
  checks.push('Real pinned input appends only on focus acquisition and preserves an existing caret')
  await page.locator('#child').evaluate((node, url) => {
    ;/** @type {HTMLIFrameElement} */ (node).src = url
  }, `http://localhost:${address.port}/child`)
  const child = page.frameLocator('#child').locator('#a')
  await child.waitFor()
  const targets = z
    .object({ targetInfos: z.array(z.object({ type: z.string(), url: z.string() })) })
    .parse(await source.sendDevToolsCommand('Target.getTargets'))
  assert(
    targets.targetInfos.some(
      (target) =>
        target.type === 'iframe' && target.url === `http://localhost:${address.port}/child`,
    ),
  )
  await child.sendKeys('child')
  assert.equal(await child.inputValue(), 'achild')
  assert.equal(await page.locator('#a').inputValue(), 'startafirst')
  checks.push('Actual OOPIF lazy frame target receives native send-keys without changing root')
  const pinned = await child.elementHandle()
  assert(pinned)
  await pinned.evaluate((node) => {
    const replacement = document.createElement('input')
    replacement.id = 'a'
    replacement.value = 'replacement'
    ;/** @type {Element} */ (node).replaceWith(replacement)
  })
  await assert.rejects(pinned.sendKeys('late', { timeout: 300 }), /detach|attached/iu)
  assert.equal(await child.inputValue(), 'replacement')
  await pinned.dispose()
  checks.push('A retained removed node rejects without retargeting a same-selector replacement')
  for (const mode of ['deadline', 'abort']) {
    await page.goto(prefix)
    const held = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    const blocker = page.evaluate((url) => {
      const request = new XMLHttpRequest()
      request.open('GET', url, false)
      request.send()
      return request.responseText
    }, `${prefix}/held`)
    await held
    const controller = new AbortController()
    const cause = new Error('actual native send-keys aborted')
    const start = performance.now()
    const first =
      mode === 'deadline'
        ? page.locator('#a').sendKeys('expired', { timeout: 150 })
        : page.runAutomation(
            'locator',
            { selector: '#a', action: 'sendKeys', value: 'expired', timeoutMs: 2000 },
            controller.signal,
          )
    const rejected = assert.rejects(
      first,
      mode === 'deadline' ? /timed out|Timeout/iu : (error) => error === cause,
    )
    if (mode === 'abort') {
      // The native diagnostic proves actual submission before client cancellation.
      for (let attempt = 0; attempt < 100; attempt++) {
        const state = physicalSchema.parse(await page.runAutomation('diagnostics'))
        if (state.pendingSendKeysPhysicalCommands === 1) break
        await setImmediate()
      }
      controller.abort(cause)
    }
    await rejected
    assert(performance.now() - start < 700)
    const retained = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.equal(retained.pendingSendKeysPhysicalCommands, 1)
    assert.equal(retained.retainedSendKeysOwners, 1)
    assert.equal(retained.inputWork, 1)
    const second = page.locator('#b').sendKeys('next', { timeout: 2000 })
    const queued = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.equal(queued.inputWork, 2)
    assert.equal(queued.pendingSendKeysPhysicalCommands, 1)
    assert(releaseHeld)
    releaseHeld()
    assert.equal(await blocker, 'released')
    await second
    assert.equal(await page.locator('#a').inputValue(), 'a')
    assert.equal(await page.locator('#b').inputValue(), 'bnext')
    const settled = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.deepEqual(settled, {
      otherOperations: 0,
      inputWork: 0,
      pendingSendKeysPhysicalCommands: 0,
      retainedSendKeysOwners: 0,
    })
    checks.push(
      `Real blocked renderer ${mode} keeps physical owner until ACK, serializes the next target and never types expired text`,
    )
  }
  await a.dispose()
  await context.close()
} finally {
  releaseHeld?.()
  await source.close()
  server.closeAllConnections()
  await new Promise((accept) => server.close(accept))
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
for (const input of inputs)
  assert.equal(
    createHash('sha256')
      .update(await readFile(input.path))
      .digest('hex'),
    input.sha256,
  )
await writeFile(
  `${directory}/report.json`,
  JSON.stringify({ passed: checks.length, checks, inputs, inputsUnchanged: true }, null, 2) + '\n',
)
console.log(`Actual native send-keys: ${checks.length} groups passed`)
