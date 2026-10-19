// SPDX-License-Identifier: MIT
/* global document, innerWidth, innerHeight */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout, clearTimeout } from 'node:timers'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

/**
 * Read the inserted DOM element's actual native tag name.
 * @param {unknown} value
 */
function elementName(value) {
  if (
    !value ||
    typeof value !== 'object' ||
    !('localName' in value) ||
    typeof value.localName !== 'string'
  )
    throw new Error('Expected a native DOM element')
  return value.localName
}

const directory = resolve('.suitecut/native-viewport')
await mkdir(directory, { recursive: true })
const reports = []
for (const pixelFormat of ['bgra', 'i420']) {
  let diagnostics = ''
  const source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    width: 320,
    height: 180,
    pixelFormat,
    headless: true,
    onDiagnostic: (message) => {
      if (diagnostics.length < 1048576)
        diagnostics += message.slice(0, 1048576 - diagnostics.length)
    },
  })
  try {
    const page = createNativePage(source)
    await page.setContent('<button onclick="document.body.dataset.clicked=\'yes\'">click</button>')
    const style = await page.addStyleTag({ content: 'body { background: red; }' })
    assert.equal(await style.evaluate(elementName), 'style')
    await style.dispose()
    const script = await page.addScriptTag({ content: 'window.nativeScriptValue=42' })
    assert.equal(await page.evaluate('nativeScriptValue'), 42)
    await script.dispose()
    const sizes = [
      { width: 500, height: 240 },
      { width: 123, height: 321 },
      { width: 1, height: 1 },
      { width: 320, height: 180 },
    ]
    for (const size of sizes) {
      await page.setViewportSize(size)
      assert.deepEqual(page.viewportSize(), size)
      assert.deepEqual(
        await page.evaluate(() => ({ width: innerWidth, height: innerHeight })),
        size,
      )
      await assert.rejects(page.setViewportSize({ width: 0, height: 1 }))
    }
    await page.click('button')
    assert.equal(await page.evaluate(() => document.body.dataset.clicked), 'yes')
    let remove = () => undefined
    const frame = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        remove()
        reject(new Error('Resized capture did not repaint'))
      }, 5000)
      remove = source.onFrame((value) => {
        // A red central pixel proves a current portrait viewport, while the
        // black left edge proves aspect-ratio preservation in the fixed canvas.
        const red =
          pixelFormat === 'i420'
            ? value.data[90 * 320 + 160] >= 60 && value.data[90 * 320 + 160] <= 65
            : value.data[(90 * 320 + 160) * 4 + 2] >= 250 &&
              value.data[(90 * 320 + 160) * 4] <= 5 &&
              value.data[(90 * 320 + 160) * 4 + 1] <= 5
        const black =
          pixelFormat === 'i420' ? value.data[90 * 320] === 16 : value.data[90 * 320 * 4 + 2] === 0
        if (!red || !black) return
        clearTimeout(timeout)
        remove()
        resolve(value)
      })
    })
    await page.setViewportSize({ width: 123, height: 321 })
    const captured = z
      .object({ width: z.number(), height: z.number(), data: z.instanceof(Buffer) })
      .parse(await frame)
    assert.equal(captured.width, 320)
    assert.equal(captured.height, 180)
    assert.equal(captured.data.length, pixelFormat === 'i420' ? (320 * 180 * 3) / 2 : 320 * 180 * 4)
    const state = await source.runAutomation('diagnostics')
    assert.equal(state.activeHandles, 0)
    assert.equal(state.pendingHandles, 0)
    assert.equal(state.otherOperations, 0)
    reports.push({
      pixelFormat,
      viewportSizes: sizes,
      portraitCapture: 'passed',
      cefVersion: source.cefVersion,
      state,
    })
  } finally {
    await source.close()
    await writeFile(`${directory}/${pixelFormat}-stderr.log`, diagnostics)
  }
}
await writeFile(`${directory}/report.json`, JSON.stringify(reports, null, 2) + '\n')
console.log(JSON.stringify(reports, null, 2))
