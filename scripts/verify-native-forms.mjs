// SPDX-License-Identifier: MIT
/* global document, HTMLInputElement, HTMLSelectElement */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-forms')
await mkdir(directory, { recursive: true })
await rm(join(directory, 'report.json'), { force: true })
const files = await mkdtemp(join(tmpdir(), 'suitecut-forms-verifier-'))
const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const cefSHA256 = createHash('sha256')
  .update(await readFile(join(dirname(executable), 'libcef.so')))
  .digest('hex')
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 640,
  height: 360,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
try {
  const page = createNativePage(source)
  await page.setContent(
    '<input type="checkbox" id="box"><label for="box">box label</label><input type="radio" id="radio"><input type="checkbox" id="prevent"><select id="single"><option value="a">Alpha</option><option value="b">Beta</option></select><select id="multi" multiple><option value="a">Alpha</option><option value="b">Beta</option><option value="b">Beta duplicate</option><option value="c">Gamma</option></select><input id="file" type="file" multiple><label for="file">file label</label><div id="wrong">wrong target</div>',
  )
  await page.evaluate(() => {
    document.body.dataset.clicks = '0'
    document.body.dataset.events = '[]'
    document.querySelector('#box')?.addEventListener('click', (event) => {
      document.body.dataset.clicks = String(Number(document.body.dataset.clicks) + 1)
      document.body.dataset.trusted = String(event.isTrusted)
    })
    document.querySelector('#prevent')?.addEventListener('click', (event) => event.preventDefault())
    for (const type of ['input', 'change'])
      document.querySelector('#file')?.addEventListener(type, (event) => {
        const previous = document.body.dataset.events ?? '[]'
        document.body.dataset.events =
          previous.slice(0, -1) +
          (previous === '[]' ? '' : ',') +
          JSON.stringify({ type, trusted: event.isTrusted }) +
          ']'
      })
  })
  console.log('Native forms phase: checkbox')
  await page.locator('#box').check()
  await page.locator('#box').check()
  assert.equal(await page.evaluate(() => document.body.dataset.clicks), '1')
  assert.equal(await page.evaluate(() => document.body.dataset.trusted), 'true')
  await page.locator('label[for=box]').uncheck()
  assert.equal(
    await page
      .locator('#box')
      .evaluate((element) => element instanceof HTMLInputElement && element.checked),
    false,
  )
  await page.locator('#radio').check()
  await assert.rejects(page.locator('#radio').uncheck(), /Cannot uncheck radio button/u)
  await assert.rejects(page.locator('#prevent').check(), /did not change the checked state/u)
  await assert.rejects(page.locator('#wrong').check(), /Not a checkbox or radio/u)
  console.log('Native forms phase: select')
  assert.deepEqual(await page.locator('#single').selectOption({ label: 'Beta' }), ['b'])
  assert.deepEqual(await page.locator('#multi').selectOption(['b', 'c']), ['b', 'b', 'c'])
  assert.deepEqual(
    await page.locator('#multi').selectOption([{ value: 'b', label: 'Beta' }, { index: 3 }]),
    ['b', 'c'],
  )
  assert.deepEqual(await page.locator('#multi').selectOption(null), [])
  const lateSelection = page.locator('#single').selectOption('late')
  await page.evaluate(() => {
    const element = document.querySelector('#single')
    if (!(element instanceof HTMLSelectElement)) throw new Error('Missing test select')
    const option = document.createElement('option')
    option.value = 'late'
    option.textContent = 'Late option'
    element.append(option)
  })
  assert.deepEqual(await lateSelection, ['late'])
  const option = await page.$('#single option[value=a]')
  assert(option)
  assert.deepEqual(await page.locator('#single').selectOption(option), ['a'])
  await option.dispose()
  await assert.rejects(page.locator('#wrong').selectOption('a'), /not a <select>/u)
  console.log('Native forms phase: path-upload')
  const path = join(files, 'path.txt')
  await writeFile(path, 'native path content')
  await page.locator('label[for=file]').setInputFiles(path)
  const readFiles = () =>
    page.locator('#file').evaluate(async (element) => {
      if (!(element instanceof HTMLInputElement)) throw new Error('Missing test input')
      return await Promise.all(
        Array.from(element.files ?? [], async (file) => ({
          name: file.name,
          type: file.type,
          size: file.size,
          text: await file.text(),
        })),
      )
    })
  assert.deepEqual(await readFiles(), [
    { name: 'path.txt', type: 'text/plain', size: 19, text: 'native path content' },
  ])
  console.log('Native forms phase: payload-upload')
  const large = Buffer.from(Array.from({ length: 100_000 }, (_, index) => 97 + (index % 26)))
  await page.locator('#file').setInputFiles([
    { name: 'same.txt', mimeType: 'application/x-suitecut-native', buffer: large },
    { name: 'same.txt', mimeType: 'text/plain', buffer: Buffer.from('second') },
    { name: 'empty.txt', mimeType: '', buffer: Buffer.alloc(0) },
    {
      name: 'bytes.bin',
      mimeType: 'application/octet-stream',
      buffer: Buffer.from([0, 255, 128, 13, 10]),
    },
  ])
  const uploaded = await readFiles()
  assert.deepEqual(
    uploaded.map(({ name, type, size }) => ({ name, type, size })),
    [
      { name: 'same.txt', type: 'application/x-suitecut-native', size: 100_000 },
      { name: 'same.txt', type: 'text/plain', size: 6 },
      { name: 'empty.txt', type: '', size: 0 },
      { name: 'bytes.bin', type: 'application/octet-stream', size: 5 },
    ],
  )
  assert.equal(uploaded[0]?.text, large.toString())
  assert.equal(uploaded[1]?.text, 'second')
  assert.deepEqual(
    await page.locator('#file').evaluate(async (element) => {
      if (!(element instanceof HTMLInputElement)) throw new Error('Missing test input')
      const file = element.files?.item(3)
      if (!file) throw new Error('Missing binary upload')
      return Array.from(new Uint8Array(await file.arrayBuffer()))
    }),
    [0, 255, 128, 13, 10],
  )
  const token = z.string().parse(
    await page.runAutomation('form', {
      action: 'uploadBegin',
      files: [{ name: 'abort.bin', mimeType: '', size: 1 }],
    }),
  )
  await assert.rejects(
    page.runAutomation('form', {
      action: 'uploadAppend',
      uploadToken: token,
      fileIndex: 0,
      base64: Buffer.from('too long').toString('base64'),
    }),
    /declared file size/u,
  )
  await page.runAutomation('form', { action: 'uploadAbort', uploadToken: token })
  console.log('Native forms phase: clear')
  await page.locator('#file').setInputFiles([])
  assert.equal((await readFiles()).length, 0)
  console.log('Native forms phase: fixed-handle')
  const handle = await page.$('#box')
  assert(handle)
  await handle.check()
  await handle.uncheck()
  await handle.dispose()
  await page.locator('#box').check({ trial: true })
  assert.equal(
    await page
      .locator('#box')
      .evaluate((element) => element instanceof HTMLInputElement && element.checked),
    false,
  )
  console.log('Native forms phase: iframe')
  await page.evaluate(async () => {
    const iframe = document.createElement('iframe')
    iframe.id = 'form-child'
    iframe.style.cssText = 'position:fixed;left:0;top:180px;width:500px;height:140px'
    iframe.srcdoc =
      '<input type="checkbox" id="child-box"><select id="child-select"><option value="one">One</option><option value="two">Two</option></select>'
    await new Promise((resolve) => {
      iframe.onload = resolve
      document.body.append(iframe)
    })
  })
  await page.frameLocator('#form-child').locator('#child-box').check()
  assert.deepEqual(
    await page.frameLocator('#form-child').locator('#child-select').selectOption('two'),
    ['two'],
  )
  await mkdir(join(files, 'nested'))
  await writeFile(join(files, 'nested', 'child.txt'), 'native directory child')
  await page.evaluate(() => {
    const input = document.createElement('input')
    input.id = 'folder'
    input.type = 'file'
    input.webkitdirectory = true
    document.body.append(input)
  })
  console.log('Native forms phase: directory controlled error')
  await assert.rejects(
    page.locator('#folder').setInputFiles(files),
    /offscreen directory picker confirmation/u,
  )
  assert.equal(
    await page
      .locator('#folder')
      .evaluate((element) => (element instanceof HTMLInputElement ? element.files?.length : -1)),
    0,
  )
  await page.locator('#folder').setInputFiles([])
  console.log('Native forms phase: cancellation')
  const cancellation = new AbortController()
  const blocked = page.runAutomation(
    'form',
    {
      selector: '#single',
      action: 'selectOption',
      options: [{ value: 'missing-cancel' }],
      timeout: 10000,
    },
    cancellation.signal,
  )
  await page.evaluate(() => document.readyState)
  cancellation.abort(new Error('form-verifier-cancellation'))
  await assert.rejects(blocked, /form-verifier-cancellation/u)
  console.log('Native forms phase: staging-churn')
  for (let index = 0; index < 1000; ++index) {
    const token = z.string().parse(
      await page.runAutomation('form', {
        action: 'uploadBegin',
        files: [{ name: 'churn.bin', mimeType: '', size: 1024 }],
      }),
    )
    await page.runAutomation('form', {
      action: 'uploadAppend',
      uploadToken: token,
      fileIndex: 0,
      base64: Buffer.from([0, 255, 128]).toString('base64'),
    })
    await page.runAutomation('form', { action: 'uploadAbort', uploadToken: token })
  }
  const observedState = await page.runAutomation('diagnostics')
  await writeFile(join(directory, 'final-diagnostics.json'), JSON.stringify(observedState, null, 2))
  const nativeState = z
    .object({
      pendingUploadGroups: z.literal(0),
      pendingUploadBytes: z.literal(0),
      temporaryUploadBytes: z.literal(0),
      temporaryUploadInputs: z.literal(0),
      formClicks: z.literal(0),
      activeHandles: z.literal(0),
    })
    .passthrough()
    .parse(observedState)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
    'Native executable changed during form proof',
  )
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefSHA256,
        cefVersion: source.cefVersion,
        directoryUploadVerified: false,
        directoryLimitation:
          'Offscreen native-picker confirmation requires a native dialog host; separate genuine directory CDP protocol proof records actual results.',
        checks: [
          'trusted-checkbox-click',
          'idempotent-check',
          'label-retarget',
          'radio',
          'prevent-default',
          'single-select',
          'multiple-select',
          'duplicate-values',
          'combined-option-descriptors',
          'null-clear',
          'missing-option-retry',
          'path-upload',
          'payload-upload-100k-chunking',
          'custom-mime',
          'duplicate-upload-names',
          'zero-byte-file',
          'upload-size-rejection',
          'explicit-abort',
          'input-clear',
          'fixed-element-handle',
          'trial-no-mutation',
          'iframe-checkbox-select',
          'directory-controlled-error',
          'cancel-pending-form',
          '1000-native-upload-stage-append-abort-cycles',
        ],
        nativeState,
      },
      null,
      2,
    ),
  )
  console.log(`Native forms verified: ${join(directory, 'report.json')}`)
} finally {
  await writeFile(join(directory, 'diagnostics.log'), diagnostics)
  await source.close()
  await rm(files, { recursive: true, force: true })
}
