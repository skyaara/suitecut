/* global alert, confirm, prompt */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable, 'SUITECUT_NATIVE_EXECUTABLE is required')
const directory = resolve('.suitecut/native-dialogs')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const binaryHash = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
let diagnostics = ''
let phase = 'launch'
let source
let failure
const checks = []
try {
  source = await launchNativeBrowser({
    executablePath: resolve(executable),
    headless: true,
    width: 320,
    height: 240,
    onDiagnostic: (text) => {
      diagnostics = (diagnostics + text).slice(-1048576)
    },
  })
  const page = createNativePage(source)
  await page.setContent('<button>Dialog</button>')
  phase = 'default dismissal'
  assert.equal(await page.evaluate(() => confirm('default confirm')), false)
  assert.equal(await page.evaluate(() => prompt('default prompt', 'preset')), null)
  checks.push(phase)
  phase = 'native event and alert accept'
  const alertEvent = page.waitForEvent('dialog')
  const alertCompletion = page.evaluate(() => {
    alert('Hello dialog')
    return 'continued'
  })
  const alertDialog = await alertEvent
  assert.equal(alertDialog.type(), 'alert')
  assert.equal(alertDialog.message(), 'Hello dialog')
  assert.equal(alertDialog.page(), page)
  await alertDialog.accept()
  assert.equal(await alertCompletion, 'continued')
  await assert.rejects(alertDialog.dismiss(), /already handled/u)
  checks.push(phase)
  phase = 'unicode prompt and exact callback result'
  const promptEvent = page.waitForEvent('dialog')
  const promptCompletion = page.evaluate(() => prompt('Name?', 'Original'))
  const promptDialog = await promptEvent
  assert.equal(promptDialog.type(), 'prompt')
  assert.equal(promptDialog.defaultValue(), 'Original')
  await promptDialog.accept('Zoë 🦊\nsecond line')
  assert.equal(await promptCompletion, 'Zoë 🦊\nsecond line')
  checks.push(phase)
  phase = 'repeated once listeners'
  for (let index = 0; index < 100; index++) {
    let observed = 0
    const answered = new Promise((resolve, reject) => {
      page.once('dialog', (dialog) => {
        observed++
        dialog.accept().then(resolve, reject)
      })
    })
    assert.equal(await page.evaluate(() => confirm('repeat')), true)
    await answered
    assert.equal(observed, 1)
  }
  assert.equal(await page.evaluate(() => confirm('after once removal')), false)
  checks.push(phase)
  phase = 'native waiter timeout and capacity recovery'
  await assert.rejects(page.waitForEvent('dialog', { timeout: 20 }), /timed out/u)
  const state = await source.runAutomation('diagnostics')
  assert.equal(state.dialogs, 0)
  assert.equal(state.otherOperations, 0)
  checks.push(phase)
  phase = 'graceful close'
  await source.close()
  assert.equal(source.processExit().code, 0)
  assert.equal(source.processExit().signal, null)
  assert(!diagnostics.includes('stack smashing'), 'subprocess stack corruption diagnostic')
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binaryHash,
  )
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify(
      { status: 'passed', checks, binaryHash, cefVersion: source.cefVersion, state },
      null,
      2,
    ),
  )
} catch (error) {
  failure = error
  await writeFile(
    `${directory}/failure.json`,
    JSON.stringify(
      {
        status: 'failed',
        phase,
        checks,
        binaryHash,
        error: error instanceof Error ? error.stack : String(error),
      },
      null,
      2,
    ),
  )
} finally {
  if (source)
    await source.close().catch((error) => {
      failure ??= error
    })
  await writeFile(`${directory}/stderr.log`, diagnostics)
}
if (failure)
  throw failure instanceof Error ? failure : new Error('Native dialog verification failed')
console.log(`Native dialogs passed ${checks.length} checks including 100 live confirm cycles`)
