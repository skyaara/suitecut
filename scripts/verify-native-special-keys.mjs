// SPDX-License-Identifier: MIT
// Actual native reserved-key interpretation, modifier ownership and physical ACKs.
/* global document, KeyboardEvent, Element, XMLHttpRequest */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_SPECIAL_KEYS_PROOF_DIRECTORY ?? '.suitecut/native-special-keys',
)
await mkdir(directory, { recursive: true })
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Exact sealed local module is hashed below.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
const inputs = await Promise.all(
  [executable, `${sdk}/native-page.js`].map(async (path) => ({
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
  if (request.url?.startsWith('/held')) {
    releaseHeld = () => response.end('released')
    announceHeld?.()
  } else response.end('<!doctype html><input id=a><input id=b>')
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
let stderr = ''
const source = await native.launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 640,
  height: 480,
  onDiagnostic: (line) => {
    stderr += line.slice(0, Math.max(0, 1048576 - stderr.length))
  },
})
const browser = new native.NativeBrowserContexts(source),
  checks = []
const eventSchema = z.array(
  z.object({
    type: z.string(),
    key: z.string(),
    code: z.string(),
    location: z.number(),
    shift: z.boolean(),
    ctrl: z.boolean(),
    trusted: z.literal(true),
    target: z.string(),
  }),
)
const physicalSchema = z.object({
  otherOperations: z.number(),
  inputWork: z.number(),
  pendingSendKeysPhysicalCommands: z.number(),
  retainedSendKeysOwners: z.number(),
})
try {
  const context = await browser.newContext(),
    page = await context.newPage()
  const reset = async () => {
    await page.goto(prefix)
    await page.evaluate(() => {
      /** @type {Array<{type:string,key:string,code:string,location:number,shift:boolean,ctrl:boolean,trusted:boolean,target:string}>} */
      const journal = []
      Reflect.set(globalThis, 'actualKeyEvents', journal)
      for (const type of ['keydown', 'keyup', 'keypress', 'input'])
        document.addEventListener(
          type,
          (event) => {
            const key = event instanceof KeyboardEvent ? event : undefined
            journal.push({
              type,
              key: key?.key ?? '',
              code: key?.code ?? '',
              location: key?.location ?? 0,
              shift: key?.shiftKey ?? false,
              ctrl: key?.ctrlKey ?? false,
              trusted: event.isTrusted,
              target: event.target instanceof Element ? event.target.id : '',
            })
          },
          true,
        )
    })
  }
  const events = async () =>
    eventSchema.parse(
      await page.evaluate(
        () => /** @type {unknown} */ (Reflect.get(globalThis, 'actualKeyEvents')),
      ),
    )
  await reset()
  await page.locator('#a').sendKeys('\uE008A!\uE000b')
  assert.equal(await page.locator('#a').inputValue(), 'A!b')
  let journal = await events()
  assert.equal(
    journal.filter((event) => event.type === 'keydown' && event.code === 'ShiftLeft').length,
    1,
  )
  assert.equal(
    journal.filter((event) => event.type === 'keyup' && event.code === 'ShiftLeft').length,
    1,
  )
  assert(journal.some((event) => event.type === 'keydown' && event.key === 'A' && event.shift))
  assert(journal.some((event) => event.type === 'keydown' && event.key === 'b' && !event.shift))
  checks.push('Explicit Shift stays held until NULL; real trusted text and key events agree')
  await reset()
  await page.locator('#a').fill('abcdef')
  await page.locator('#a').sendKeys('\uE009a\uE000Z')
  assert.equal(await page.locator('#a').inputValue(), 'Z')
  await page.keyboard.type('x')
  assert.equal(await page.locator('#a').inputValue(), 'Zx')
  journal = await events()
  assert(journal.some((event) => event.key === 'a' && event.ctrl))
  assert(journal.some((event) => event.type === 'keyup' && event.code === 'ControlLeft'))
  checks.push('Real Control+A/NULL/uppercase replacement releases modifiers before ordinary typing')
  await reset()
  await page.locator('#a').fill('abcd')
  await page.locator('#a').sendKeys('\uE011\uE014\uE014\uE003X')
  assert.equal(await page.locator('#a').inputValue(), 'aXcd')
  journal = await events()
  for (const key of ['Home', 'ArrowRight', 'Backspace'])
    assert(journal.some((event) => event.type === 'keydown' && event.key === key))
  checks.push('Native navigation and Backspace alter the genuine caret and value')
  await reset()
  await page.locator('#a').sendKeys('a\uE004b')
  assert.equal(await page.locator('#a').inputValue(), 'a')
  assert.equal(await page.locator('#b').inputValue(), 'b')
  checks.push('Native Tab moves actual focus within one original sendKeys operation')
  await reset()
  await page.locator('#a').sendKeys('\uE01B\uE028\uE01C\uE025\uE01D')
  assert.equal(await page.locator('#a').inputValue(), '1.2+3')
  journal = await events()
  for (const code of ['Numpad1', 'NumpadDecimal', 'Numpad2', 'NumpadAdd', 'Numpad3'])
    assert(
      journal.some(
        (event) => event.type === 'keydown' && event.code === code && event.location === 3,
      ),
    )
  await page.locator('#a').sendKeys('\uE057\uE05A\uE05D')
  assert.equal(await page.locator('#a').inputValue(), '12+3')
  checks.push(
    'Numeric and navigation numpad keys keep actual location/code and distinct text semantics',
  )
  await reset()
  await page.locator('#a').sendKeys('\uE050A\uE000\uE032')
  journal = await events()
  assert(
    journal.some(
      (event) => event.type === 'keydown' && event.code === 'ShiftRight' && event.location === 2,
    ),
  )
  assert(journal.some((event) => event.type === 'keydown' && event.code === 'F2' && !event.shift))
  await page.locator('#a').fill('')
  await page.locator('#a').type('\uE003')
  assert.equal(await page.locator('#a').inputValue(), '\uE003')
  checks.push(
    'Right modifiers/function keys are native; ordinary type keeps private-use text literal',
  )
  await reset()
  await page.locator('#a').sendKeys('\uE03Dv')
  await page.locator('#a').fill('')
  await page.locator('#a').sendKeys('a')
  assert.equal(await page.locator('#a').inputValue(), 'a')
  journal = await events()
  assert(journal.some((event) => event.type === 'keyup' && event.code === 'MetaLeft'))
  assert(journal.some((event) => event.type === 'keypress' && event.key === 'a'))
  checks.push('Awaited real Meta shortcut releases before a subsequent ordinary keypress')
  for (const mode of ['deadline', 'abort']) {
    await reset()
    await page.evaluate((url) => {
      document.addEventListener(
        'keyup',
        (event) => {
          if (event.key === 'Shift') {
            const request = new XMLHttpRequest()
            request.open('GET', url + '-cleanup', false)
            request.send()
          }
        },
        { once: true },
      )
      document.addEventListener(
        'keydown',
        (event) => {
          if (event.key === 'Shift') {
            const request = new XMLHttpRequest()
            request.open('GET', url, false)
            request.send()
          }
        },
        { once: true },
      )
    }, `${prefix}/held`)
    const held = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    const controller = new AbortController(),
      cause = new Error('Actual native special-key cancellation')
    const first =
      mode === 'deadline'
        ? page.locator('#a').sendKeys('\uE008A', { timeout: 150 })
        : page.runAutomation(
            'locator',
            { selector: '#a', action: 'sendKeys', value: '\uE008A', timeoutMs: 2000 },
            controller.signal,
          )
    const rejected = assert.rejects(
      first,
      mode === 'deadline' ? /Timeout|timed out/iu : (error) => error === cause,
    )
    await held
    if (mode === 'abort') controller.abort(cause)
    await rejected
    const retained = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.equal(retained.pendingSendKeysPhysicalCommands, 1)
    assert.equal(retained.retainedSendKeysOwners, 1)
    assert.equal(retained.inputWork, 1)
    const second = page.locator('#b').sendKeys('next', { timeout: 5000 })
    void second.catch(() => undefined)
    const queued = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.equal(queued.inputWork, 2)
    assert(releaseHeld)
    const cleanupHeld = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    releaseHeld()
    await cleanupHeld
    const cleanupRetained = physicalSchema.parse(await page.runAutomation('diagnostics'))
    assert.equal(cleanupRetained.pendingSendKeysPhysicalCommands, 1)
    assert.equal(cleanupRetained.retainedSendKeysOwners, 1)
    assert.equal(cleanupRetained.inputWork, 2)
    releaseHeld()
    await second
    assert.equal(await page.locator('#a').inputValue(), '')
    assert.equal(await page.locator('#b').inputValue(), 'next')
    journal = await events()
    assert(journal.some((event) => event.type === 'keyup' && event.code === 'ShiftLeft'))
    assert(
      journal.some((event) => event.target === 'b' && event.type === 'keydown' && !event.shift),
    )
    assert.deepEqual(physicalSchema.parse(await page.runAutomation('diagnostics')), {
      otherOperations: 0,
      inputWork: 0,
      pendingSendKeysPhysicalCommands: 0,
      retainedSendKeysOwners: 0,
    })
    checks.push(
      `Real blocked Shift keydown ${mode} retains physical owner through held keydown AND cleanup-keyup ACKs, blocks next owner and never types expired text`,
    )
  }
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
console.log(`Actual native special keys: ${checks.length} groups passed`)
