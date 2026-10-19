// SPDX-License-Identifier: MIT
/* global confirm, prompt */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeDialog,
  NativeFileChooser,
} from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-managed-event-predicates')
await mkdir(directory, { recursive: true })
const paths = [
  executable,
  'dist/native-event-predicate.js',
  'dist/native-dialog.js',
  'dist/native-filechooser.js',
]
const hashes = await Promise.all(
  paths.map(async (path) =>
    createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  ),
)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic(message) {
    stderr = (stderr + message).slice(-1048576)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  const dialogs = z.array(z.instanceof(NativeDialog)).parse([])
  const accepted = page.waitForEvent('dialog', {
    timeout: 1000,
    predicate: async (dialog) => {
      dialogs.push(dialog)
      if (dialog.message() === 'first') {
        await dialog.dismiss()
        return false
      }
      return true
    },
  })
  assert.equal(await page.evaluate(() => confirm('first')), false)
  const answer = page.evaluate(() => prompt('accepted', 'actual default'))
  const dialog = await accepted
  assert.equal(dialog, dialogs[1])
  assert.equal(dialog.page(), page)
  assert.equal(dialog.defaultValue(), 'actual default')
  const closed = page.waitForEvent('dialogclosed', (value) => value.message() === 'accepted')
  await dialog.accept('actual reply')
  assert.equal(await answer, 'actual reply')
  assert.equal((await closed).message(), 'accepted')
  checks.push(
    'actual CEF dialogs reject/accept host predicates under one native journal wait, retained dialog identity and closed lifecycle',
  )
  await page.setContent('<input id="first" type="file"><input id="accepted" type="file" multiple>')
  const choosers = z.array(z.instanceof(NativeFileChooser)).parse([])
  const chosen = page.waitForEvent('filechooser', {
    timeout: 1000,
    predicate: async (chooser) => {
      choosers.push(chooser)
      return (await chooser.element().getAttribute('id')) === 'accepted'
    },
  })
  await page.locator('#first').click()
  await page.locator('#accepted').click()
  const chooser = await chosen
  assert.equal(chooser, choosers[1])
  assert.equal(chooser.page(), page)
  assert.equal(chooser.isMultiple(), true)
  await chooser.setFiles({
    name: 'actual.txt',
    mimeType: 'text/plain',
    buffer: Buffer.from('actual chooser bytes'),
  })
  assert.equal(
    await page.locator('#accepted').evaluate((input) => {
      if (!(input instanceof HTMLInputElement)) throw new Error('Expected actual file input')
      const file = input.files?.[0]
      if (!file) throw new Error('Actual chooser upload missing')
      return file.text()
    }),
    'actual chooser bytes',
  )
  await Promise.all(choosers.map((value) => value.element().dispose()))
  checks.push(
    'actual CEF file chooser predicates adopt native node identity and accepted chooser uploads real File bytes',
  )
  const diagnostics = z
    .record(z.string(), z.number())
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.activeHandles, 0)
  assert.equal(diagnostics.pendingEventPredicates, 0)
  assert.equal(diagnostics.eventPredicateBytes, 0)
  assert.equal(diagnostics.dialogs, 0)
  assert.equal(diagnostics.otherOperations, 0)
  assert.equal(diagnostics.pendingDevTools, 0)
  for (let index = 0; index < paths.length; ++index)
    assert.equal(
      createHash('sha256')
        .update(await readFile(paths[index]))
        .digest('hex'),
      hashes[index],
      'Managed event proof runtime changed',
    )
  const report = {
    status: 'passed',
    checks,
    cefVersion: source.cefVersion,
    paths,
    hashes,
    diagnostics,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
