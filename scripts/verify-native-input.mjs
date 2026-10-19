// SPDX-License-Identifier: MIT
/* global document, KeyboardEvent, MouseEvent */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-input')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
try {
  const page = createNativePage(source)
  await page.setContent(
    '<input id="input"><button style="position:absolute;left:200px;top:50px;width:80px;height:30px">click</button>',
  )
  await page.evaluate(() => {
    const inputEvents = []
    document.body.dataset.inputEvents = '[]'
    document.body.dataset.clicks = '0'
    document.body.dataset.doubleClicks = '0'
    document.body.dataset.rightClicks = '0'
    for (const type of [
      'keydown',
      'keyup',
      'keypress',
      'input',
      'mousemove',
      'mousedown',
      'mouseup',
      'wheel',
    ])
      document.addEventListener(type, (event) => {
        inputEvents.push({
          type,
          trusted: event.isTrusted,
          ...(event instanceof KeyboardEvent
            ? { key: event.key, code: event.code, ctrl: event.ctrlKey, shift: event.shiftKey }
            : {}),
          ...(event instanceof MouseEvent
            ? { x: event.clientX, y: event.clientY, buttons: event.buttons }
            : {}),
        })
        document.body.dataset.inputEvents = JSON.stringify(inputEvents)
      })
    document.querySelector('button').addEventListener('click', () => {
      document.body.dataset.clicks = String(Number(document.body.dataset.clicks) + 1)
    })
    document.querySelector('button').addEventListener('dblclick', () => {
      document.body.dataset.doubleClicks = String(Number(document.body.dataset.doubleClicks) + 1)
    })
    document.querySelector('button').addEventListener('contextmenu', (event) => {
      event.preventDefault()
      document.body.dataset.rightClicks = String(Number(document.body.dataset.rightClicks) + 1)
    })
  })
  await page.focus('#input')
  await page.keyboard.type('aB🙂', { delay: 1 })
  assert.equal(await page.inputValue('#input'), 'aB🙂')
  await page.keyboard.press('Control+a')
  await page.keyboard.insertText('日本')
  assert.equal(await page.inputValue('#input'), '日本')
  await page.keyboard.down('Shift')
  await page.keyboard.press('a')
  await page.keyboard.up('Shift')
  assert.equal(await page.inputValue('#input'), '日本A')
  await page.mouse.move(240.5, 65.5, { steps: 4 })
  await page.mouse.dblclick(240.5, 65.5)
  assert.equal(await page.evaluate(() => Number(document.body.dataset.clicks)), 2)
  assert.equal(await page.evaluate(() => Number(document.body.dataset.doubleClicks)), 1)
  await page.mouse.click(240.5, 65.5, { button: 'right' })
  assert.equal(await page.evaluate(() => Number(document.body.dataset.rightClicks)), 1)
  await page.mouse.down()
  await page.mouse.move(245.5, 65.5)
  await page.mouse.up()
  await page.mouse.wheel(0, 20)
  const events = z
    .array(
      z.object({
        type: z.string(),
        key: z.string().optional(),
        code: z.string().optional(),
        ctrl: z.boolean().optional(),
        shift: z.boolean().optional(),
        trusted: z.boolean(),
        buttons: z.number().optional(),
      }),
    )
    .parse(JSON.parse(await page.evaluate(() => document.body.dataset.inputEvents ?? '[]')))
  assert(
    events.every((event) => event.trusted),
    'Input must originate in native browser dispatch',
  )
  assert(events.some((event) => event.type === 'keydown' && event.key === 'A' && event.shift))
  assert(events.some((event) => event.type === 'keydown' && event.key === 'a' && event.ctrl))
  assert(events.some((event) => event.type === 'mousemove' && event.buttons === 1))
  const controller = new AbortController()
  const held = source.runAutomation(
    'input',
    { kind: 'keyboard', action: 'press', key: 'Shift+a', delay: 100000 },
    controller.signal,
  )
  const canceled = assert.rejects(held, /cancel input/)
  const stateBefore = z
    .object({ pressedKeys: z.number().positive() })
    .parse(await source.runAutomation('diagnostics'))
  const queuedChecks = []
  for (let index = 0; index < 1000; ++index) {
    const queuedController = new AbortController()
    const queued = source.runAutomation(
      'input',
      { kind: 'keyboard', action: 'press', key: 'a', delay: 100000 },
      queuedController.signal,
    )
    const check = assert.rejects(queued, /cancel queued/)
    queuedController.abort(new Error('cancel queued'))
    queuedChecks.push(check)
    if (queuedChecks.length >= 32) {
      await Promise.all(queuedChecks)
      queuedChecks.length = 0
    }
  }
  await Promise.all(queuedChecks)
  controller.abort(new Error('cancel input'))
  await canceled
  const state = z
    .object({
      inputWork: z.number(),
      queuedInput: z.number(),
      pressedKeys: z.number(),
      keyModifiers: z.number(),
      mouseButtons: z.number(),
      otherOperations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(state, {
    inputWork: 0,
    queuedInput: 0,
    pressedKeys: 0,
    keyModifiers: 0,
    mouseButtons: 0,
    otherOperations: 0,
  })
  const report = {
    cefVersion: source.cefVersion,
    unicodeTyping: 'passed',
    modifierChords: 'passed',
    mouseDoubleClick: 'passed',
    rightClick: 'passed',
    dragButtons: 'passed',
    trustedEvents: events.length,
    queuedCancellation: 1000,
    heldCancellation: 'passed',
    pressedKeysBeforeCancellation: stateBefore.pressedKeys,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
