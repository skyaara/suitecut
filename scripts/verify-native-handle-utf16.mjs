// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeJSHandle } from '../dist/native.js'

const directory = resolve('.suitecut/native-handle-utf16')
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
const value = '\ud800a\0\\"\udfff😀'
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent('<body>native handle metadata</body>')
  const string = await page.evaluateHandle((text) => text, value)
  try {
    assert.equal(string.toString(), value)
    assert.equal(await string.jsonValue(), value)
  } finally {
    await string.dispose()
  }
  checks.push(
    'genuine main JSHandle primitive preview and retained jsonValue preserve exact isolated/pair/NUL/backslash/quote units',
  )
  const authorObject = await page.evaluateHandle(() => ({
    __suitecutNativeUTF16Scalar: 'd800',
    value: '\ud800',
  }))
  try {
    assert.deepEqual(await authorObject.jsonValue(), {
      __suitecutNativeUTF16Scalar: 'd800',
      value: '\ud800',
    })
    const property = await authorObject.getProperty('value')
    try {
      assert.equal(property.toString(), '\ud800')
      assert.equal(await property.jsonValue(), '\ud800')
    } finally {
      await property.dispose()
    }
  } finally {
    await authorObject.dispose()
  }
  const truthy = await page.waitForFunction((text) => text, '\ud800', {
    timeout: 2000,
    polling: 10,
  })
  try {
    assert.equal(await truthy.jsonValue(), '\ud800')
  } finally {
    await truthy.dispose()
  }
  checks.push('native waitForFunction recognizes actual nonempty isolated-unit string truthiness')
  const node = await page.evaluateHandle((text) => {
    const element = document.createElement('span')
    element.textContent = text
    element.setAttribute('data-value', text)
    document.body.append(element)
    return element
  }, value)
  try {
    const html = await node.evaluate((element) =>
      element instanceof Element ? element.outerHTML : undefined,
    )
    assert.equal(node.toString(), `JSHandle@${html}`)
    assert.equal(
      await node.evaluate((element) =>
        element instanceof Element ? element.textContent : undefined,
      ),
      value,
    )
  } finally {
    await node.dispose()
  }
  checks.push(
    'genuine DOM.getOuterHTML preview equals original retained element markup with exact scalar units',
  )
  const large = 'x'.repeat(8191) + '\ud800' + 'tail'
  const bounded = await page.evaluateHandle((text) => text, large)
  try {
    assert.equal(bounded.toString(), 'x'.repeat(8191))
    assert.equal(await bounded.jsonValue(), large)
  } finally {
    await bounded.dispose()
  }
  checks.push(
    'bounded native preview cannot split an isolated-unit WTF8 triplet or alter actual retained string',
  )
  /**
   * Read the actual retained native binding argument.
   * @param {import('../src/native-bindings.js').NativeBindingSource} _source
   * @param {import('../src/untrusted.js').UntrustedInput} argument
   */
  const inspectArgument = async (_source, argument) => {
    assert(argument instanceof NativeJSHandle)
    try {
      return await argument.jsonValue()
    } finally {
      await argument.dispose()
    }
  }
  await page.exposeBinding('exactMetadataArgument', inspectArgument, { handle: true })
  assert.equal(await page.evaluate(`exactMetadataArgument(${JSON.stringify(value)})`), value)
  checks.push(
    'genuine handle-mode binding summary and callback argument retain exact string without author object reinterpretation',
  )
  await page.waitForTimeout(20)
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      bindingCalls: z.number(),
      bindingArgumentControls: z.number(),
      bindingArgumentBytes: z.number(),
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
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
