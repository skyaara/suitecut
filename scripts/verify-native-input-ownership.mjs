// SPDX-License-Identifier: MIT
// Actual native editor ownership regression; no DOM value/event substitutes.
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { readFile, mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY ?? resolve('dist'))
/** @type {typeof import('../dist/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The caller selects a locally sealed SDK whose bytes are hashed below.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
const output = resolve(
  process.env.SUITECUT_NATIVE_INPUT_PROOF_DIRECTORY ?? '.suitecut/native-input-ownership',
)
await mkdir(output, { recursive: true })
const inputs = [executable, `${sdk}/native-page.js`]
const hashes = await Promise.all(
  inputs.map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
const checks = []
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html')
  if (request.url === '/frame.html')
    response.end('<!doctype html><input id=child value=untouched><input id=other value=other>')
  else if (request.url === '/root.html')
    response.end(
      '<!doctype html><input id=root value=root><input id=range type=range value=42><input id=constrained type=range min=10 max=34 step=3 value=28><input id=color type=color value=#ff0000><p id=editable contenteditable>Editable</p><iframe id=cross></iframe>',
    )
  else response.writeHead(404).end('Unknown authored fixture')
})
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
let stderr = ''
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
try {
  const context = await browser.newContext()
  const page = await context.newPage()
  await page.goto(`http://127.0.0.1:${address.port}/root.html`)
  const handle = await page.locator('#root').elementHandle()
  assert(handle)
  await handle.fill('root café')
  assert.equal(await page.locator('#root').inputValue(), 'root café')
  await handle.clear()
  assert.equal(await page.locator('#root').inputValue(), '')
  await handle.focus()
  assert.equal(await handle.evaluate((node) => node === document.activeElement), true)
  checks.push('Actual root handle fill/clear and existing native DOM focus')

  await page.evaluate(() => {
    const events = /** @type {Array<{id:string,type:string,trusted:boolean}>} */ ([])
    Reflect.set(globalThis, 'nativeClearEvents', events)
    for (const id of ['range', 'constrained', 'color', 'editable']) {
      const node = document.getElementById(id)
      if (!node) throw new Error('The actual authored clear control must exist')
      for (const type of ['input', 'change'])
        node.addEventListener(type, (event) => events.push({ id, type, trusted: event.isTrusted }))
    }
  })
  await page.locator('#range').clear()
  await page.locator('#constrained').clear()
  const color = await page.locator('#color').elementHandle()
  assert(color)
  await color.clear()
  const editable = await page.locator('#editable').elementHandle()
  assert(editable)
  await editable.clear()
  assert.equal(await page.locator('#range').inputValue(), '50')
  assert.equal(await page.locator('#constrained').inputValue(), '22')
  assert.equal(await page.locator('#color').inputValue(), '#000000')
  assert.equal(await editable.evaluate((node) => /** @type {Element} */ (node).innerHTML), '')
  checks.push(
    'Native clear intent performs actual browser default sanitization and exact editing-host DOM deletion',
  )
  assert.deepEqual(
    await page.evaluate(
      () => /** @type {unknown} */ (Reflect.get(globalThis, 'nativeClearEvents')),
    ),
    ['range', 'constrained', 'color'].flatMap((id) => [
      { id, type: 'input', trusted: false },
      { id, type: 'change', trusted: false },
    ]),
  )
  checks.push('Programmatic non-text reset events preserve their genuine untrusted provenance')
  await color.dispose()
  await editable.dispose()

  await page.locator('#cross').evaluate((node, url) => {
    ;/** @type {HTMLIFrameElement} */ (node).src = url
  }, `http://localhost:${address.port}/frame.html`)
  const child = page.frameLocator('#cross').locator('#child')
  await child.fill('lazy frame')
  const targets = z
    .object({ targetInfos: z.array(z.object({ type: z.string(), url: z.string() })) })
    .parse(await source.sendDevToolsCommand('Target.getTargets'))
  assert(
    targets.targetInfos.some(
      (target) =>
        target.type === 'iframe' && target.url === `http://localhost:${address.port}/frame.html`,
    ),
    'The native browser must report the actual out-of-process iframe target',
  )
  assert.equal(await child.inputValue(), 'lazy frame')
  assert.equal(await page.locator('#root').inputValue(), '')
  checks.push('Actual cross-process lazy frame locator fill preserves root value')

  const childHandle = await child.elementHandle()
  assert(childHandle)
  await childHandle.fill('pinned child')
  assert.equal(await child.inputValue(), 'pinned child')
  await childHandle.focus()
  assert.equal(
    await childHandle.evaluate(
      (node) => node === /** @type {Element} */ (node).ownerDocument.activeElement,
    ),
    true,
  )
  await childHandle.clear()
  assert.equal(await child.inputValue(), '')
  assert.equal(await page.frameLocator('#cross').locator('#other').inputValue(), 'other')
  checks.push('Actual child-frame handle fill/clear/focus retains exact native node')

  await handle.evaluate((node) => {
    const replacement = document.createElement('input')
    replacement.id = 'root'
    replacement.value = 'replacement'
    ;/** @type {Element} */ (node).replaceWith(replacement)
  })
  await assert.rejects(handle.fill('must not retarget', { timeout: 250 }), /not attached|detached/u)
  assert.equal(await page.locator('#root').inputValue(), 'replacement')
  checks.push('Detached retained node rejects without filling a same-selector replacement')

  await page.locator('#cross').evaluate((node) => /** @type {Element} */ (node).remove())
  await assert.rejects(
    childHandle.fill('late child', { timeout: 250 }),
    /detach|context|frame|object|handle/u,
  )
  assert.equal(await page.locator('#root').inputValue(), 'replacement')
  checks.push('Removed child realm rejects its retained editor without touching root')
  await handle.dispose()
  await assert.rejects(
    Promise.resolve().then(() => handle.clear()),
    /disposed/u,
  )
  checks.push('Disposed handle clear rejects before native dispatch')
  await context.close()
  const cleanup = z
    .object({
      otherOperations: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      inputWork: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(cleanup, {
    otherOperations: 0,
    activeHandles: 0,
    pendingHandles: 0,
    inputWork: 0,
  })
  checks.push('Actual helper cleanup after editor context closure')
} finally {
  await source.close()
  server.closeAllConnections()
  await new Promise((accept) => server.close(accept))
  await writeFile(`${output}/native-stderr.log`, stderr)
}
for (const input of hashes)
  assert.equal(
    createHash('sha256')
      .update(await readFile(input.path))
      .digest('hex'),
    input.sha256,
  )
await writeFile(
  `${output}/report.json`,
  JSON.stringify(
    { checks, passed: checks.length, inputsUnchanged: true, inputs: hashes },
    null,
    2,
  ) + '\n',
)
console.log(`Actual native editor ownership: ${checks.length} groups passed`)
