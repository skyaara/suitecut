// SPDX-License-Identifier: MIT
/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript verification needs JSDoc types. */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const binarySHA256 = await hash()
let nativeStderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (message) => {
    nativeStderr += message
  },
})
const page = createNativePage(source)
const text = `prefix\ud800middle\udfff\ud83d\ude80suffix`
const checks = []
try {
  await page.goto('data:text/html,<title>UTF16 values</title>')
  const produced = await page.evaluate(
    () => `prefix${String.fromCharCode(0xd800)}middle${String.fromCharCode(0xdfff)}🚀suffix`,
  )
  assert.equal(produced, text)
  checks.push('actual isolated JavaScript UTF16 units survive native return-by-value')
  const value = { [text]: text, pattern: new RegExp(text), error: new Error(text) }
  value.error.name = text
  value.error.stack = text
  const roundTrip = z
    .object({ pattern: z.instanceof(RegExp), error: z.instanceof(Error) })
    .passthrough()
    .parse(await page.evaluate((input) => input, value))
  assert.equal(roundTrip[text], text)
  assert.equal(roundTrip.pattern.source, value.pattern.source)
  assert.equal(roundTrip.error.name, text)
  assert.equal(roundTrip.error.message, text)
  assert.equal(roundTrip.error.stack, text)
  checks.push('actual object keys, RegExp and Error metadata keep every original UTF16 unit')
  const handle = await page.evaluateHandle((input) => ({ [input]: input }), text)
  const held = z.record(z.string(), z.string()).parse(await handle.jsonValue())
  assert.equal(held[text], text)
  await handle.dispose()
  checks.push('genuine retained handle JSON uses the same exact codec')
  let received = ''
  const binding = await page.exposeFunction('utf16Echo', (input) => {
    received = z.string().parse(input)
    return received
  })
  const bound = await page.evaluate(async (input) => {
    /** @type {unknown} */
    const callback = Reflect.get(globalThis, 'utf16Echo')
    if (typeof callback !== 'function') throw new Error('Native binding is missing')
    /** @type {(value: string) => Promise<unknown>} */
    const invoke = callback
    return await invoke(input)
  }, text)
  assert.equal(received, text)
  assert.equal(bound, text)
  checks.push('real renderer-to-host binding arguments and results preserve isolated units')
  await binding.dispose()
  const diagnostics = z
    .object({
      activeHandles: z.literal(0),
      pendingHandles: z.literal(0),
      evaluationGroups: z.literal(0),
      otherOperations: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(nativeStderr, '')
  assert.equal(await hash(), binarySHA256)
  const directory = resolve('.suitecut/native-values-utf16')
  await mkdir(directory, { recursive: true })
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: source.cefVersion,
        checks,
        diagnostics,
        nativeStderr,
      },
      null,
      2,
    ),
  )
  console.log(`Native UTF16 values verified: ${directory}/report.json`)
} finally {
  await source.close()
}
