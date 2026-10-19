// SPDX-License-Identifier: MIT
/* global document, HTMLInputElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-chooser-drag')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const cefSHA256 = createHash('sha256')
  .update(await readFile(join(dirname(executable), 'libcef.so')))
  .digest('hex')
const files = await mkdtemp(join(tmpdir(), 'suitecut-chooser-proof-'))
const path = join(files, 'native.txt')
await writeFile(path, 'genuine chooser file')
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 640,
  height: 360,
  headless: true,
})
const checks = []
try {
  const page = createNativePage(source)
  console.log('Native chooser/drag phase: single chooser')
  await page.setContent('<input id=file type=file>')
  const [chooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.locator('#file').click(),
  ])
  assert.equal(chooser.page(), page)
  assert.equal(chooser.isMultiple(), false)
  assert.equal(await chooser.element().getAttribute('id'), 'file')
  await chooser.setFiles(path)
  assert.deepEqual(
    await chooser.element().evaluate(async (element) => {
      if (!(element instanceof HTMLInputElement)) throw new Error('Missing native chooser element')
      return await Promise.all(
        Array.from(element.files ?? [], async (file) => ({
          name: file.name,
          text: await file.text(),
        })),
      )
    }),
    [{ name: 'native.txt', text: 'genuine chooser file' }],
  )
  await assert.rejects(chooser.setFiles([path, path]), /Non-multiple/u)
  await chooser.setFiles([])
  assert.equal(
    await chooser
      .element()
      .evaluate((element) => (element instanceof HTMLInputElement ? element.files?.length : -1)),
    0,
  )
  await chooser.element().dispose()
  checks.push('genuine-chooser-identity', 'single-file-content', 'multiple-rejection', 'clear')
  console.log('Native chooser/drag phase: event timeout')
  await assert.rejects(page.waitForEvent('filechooser', { timeout: 40 }), /Timeout|timed out/u)
  checks.push('native-chooser-event-timeout')
  console.log('Native chooser/drag phase: detached input and concurrent waits')
  const firstWait = page.waitForEvent('filechooser')
  const secondWait = page.waitForEvent('filechooser')
  const chosenBytes = page.evaluate(async () => {
    const input = document.createElement('input')
    input.type = 'file'
    input.click()
    await new Promise((resolve) => {
      input.oninput = resolve
    })
    const file = input.files?.item(0)
    if (!file) throw new Error('Detached chooser did not receive a real file')
    return await file.text()
  })
  const [firstDetached, secondDetached] = await Promise.all([firstWait, secondWait])
  assert.equal(firstDetached, secondDetached)
  await firstDetached.setFiles(path)
  assert.equal(await chosenBytes, 'genuine chooser file')
  await firstDetached.element().dispose()
  checks.push('detached-genuine-input-upload', 'concurrent-waits-same-chooser')
  console.log('Native chooser/drag phase: multiple chooser')
  await page.setContent('<input id=file type=file multiple>')
  const [multiple] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.locator('#file').click(),
  ])
  assert.equal(multiple.isMultiple(), true)
  await multiple.setFiles([path, path])
  assert.equal(
    await multiple
      .element()
      .evaluate((element) => (element instanceof HTMLInputElement ? element.files?.length : -1)),
    2,
  )
  await multiple.element().dispose()
  checks.push('real-multiple-mode', 'multiple-file-assignment')
  console.log('Native chooser/drag phase: trusted HTML5 drag')
  await page.setContent(
    '<div id=drag draggable=true style="position:fixed;left:10px;top:30px;width:120px;height:70px;background:blue">drag</div><div id=drop style="position:fixed;left:310px;top:30px;width:120px;height:70px;background:gray">drop</div>',
  )
  await page.evaluate(() => {
    const source = document.querySelector('#drag'),
      target = document.querySelector('#drop')
    if (!source || !target) throw new Error('Missing real drag fixture')
    source.addEventListener('dragstart', (event) => {
      if (!(event instanceof DragEvent) || !event.dataTransfer)
        throw new Error('Missing native drag data')
      event.dataTransfer.setData('text/plain', 'page-authored genuine bytes')
      document.body.dataset.dragstartTrusted = String(event.isTrusted)
    })
    for (const type of ['dragenter', 'dragover'])
      target.addEventListener(type, (event) => {
        event.preventDefault()
        document.body.dataset.overTrusted = String(event.isTrusted)
      })
    target.addEventListener('drop', (event) => {
      event.preventDefault()
      if (!(event instanceof DragEvent) || !event.dataTransfer)
        throw new Error('Missing native dropped data')
      document.body.dataset.dropText = event.dataTransfer.getData('text/plain')
      document.body.dataset.dropTrusted = String(event.isTrusted)
    })
  })
  await page.locator('#drag').dragTo(page.locator('#drop'), { trial: true })
  assert.equal(await page.evaluate(() => document.body.dataset.dropText), undefined)
  await page.locator('#drag').dragTo(page.locator('#drop'))
  assert.deepEqual(
    await page.evaluate(() => ({
      start: document.body.dataset.dragstartTrusted,
      over: document.body.dataset.overTrusted,
      drop: document.body.dataset.dropTrusted,
      text: document.body.dataset.dropText,
    })),
    { start: 'true', over: 'true', drop: 'true', text: 'page-authored genuine bytes' },
  )
  checks.push(
    'trial-no-drop',
    'trusted-native-dragstart',
    'genuine-drag-data',
    'trusted-native-dragenter-over-drop',
  )
  console.log('Native chooser/drag phase: trusted manual mouse drag')
  await page.evaluate(() => {
    delete document.body.dataset.dropText
  })
  await page.locator('#drag').hover()
  await page.mouse.down()
  await page.locator('#drop').hover()
  await page.mouse.up()
  assert.equal(
    await page.evaluate(() => document.body.dataset.dropText),
    'page-authored genuine bytes',
  )
  checks.push('manual-mouse-hover-native-drag')
  console.log('Native chooser/drag phase: native drag cancellation')
  await page.evaluate(() => {
    delete document.body.dataset.dropText
  })
  await page.locator('#drag').hover()
  await page.mouse.down()
  await page.locator('#drop').hover()
  await page.keyboard.press('Escape')
  await page.mouse.up()
  assert.equal(await page.evaluate(() => document.body.dataset.dropText), undefined)
  checks.push('manual-escape-cancel-no-drop')
  const diagnostics = await page.runAutomation('diagnostics')
  await writeFile(join(directory, 'diagnostics.json'), JSON.stringify(diagnostics, null, 2))
  z.object({
    activeHandles: z.literal(0),
    mouseButtons: z.literal(0),
    formClicks: z.literal(0),
    pendingUploadGroups: z.literal(0),
    pendingUploadBytes: z.literal(0),
  })
    .passthrough()
    .parse(diagnostics)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
    'Native output changed during proof',
  )
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefSHA256,
        cefVersion: source.cefVersion,
        checks,
        diagnostics,
      },
      null,
      2,
    ),
  )
  console.log(`Native chooser/drag verified: ${join(directory, 'report.json')}`)
} finally {
  await source.close()
  await rm(files, { recursive: true, force: true })
}
