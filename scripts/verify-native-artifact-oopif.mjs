// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import image from 'sharp'
import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

const directory = resolve('.suitecut/native-artifact-oopif')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const files = [
  executablePath,
  resolve('dist/native-page.js'),
  resolve('dist/native-artifacts.js'),
  resolve('native/browser/artifact_controls.h'),
  resolve('native/browser/artifact_geometry.inc'),
  resolve('native/browser/artifact_masks.inc'),
]
const provenance = z.record(z.string(), z.string()).parse({})
for (const file of files)
  provenance[file] = createHash('sha256')
    .update(await readFile(file))
    .digest('hex')
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    request.url === '/frame'
      ? '<style>html,body{margin:0;background:white}#target{position:absolute;left:12px;top:15px;width:40px;height:20px;background:green}</style><div id="target"></div>'
      : '<style>html,body{margin:0;background:blue}</style>',
  )
})
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  width: 200,
  height: 120,
  deviceScaleFactor: 2,
  headless: true,
}).catch((error) => {
  server.close()
  throw error
})
/**
 * Decode a real artifact pixel independently of native metadata.
 * @param {Buffer} encoded
 * @param {number} x
 * @param {number} y
 */
async function pixel(encoded, x, y) {
  const { data, info } = await image(encoded)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  return [...data.subarray((y * info.width + x) * 4, (y * info.width + x) * 4 + 4)]
}
try {
  const page = createNativePage(source)
  await page.goto(`http://127.0.0.1:${address.port}/`)
  await page.setContent(
    `<style>html,body{margin:0;background:blue}iframe{position:absolute;left:30px;top:20px;width:120px;height:70px;border:3px solid black;transform:scale(1.25);transform-origin:0 0}</style><iframe src="http://localhost:${address.port}/frame"></iframe>`,
  )
  const target = page.frameLocator('iframe').locator('#target')
  const encoded = await target.screenshot({ scale: 'css', path: `${directory}/locator.png` })
  const metadata = await image(encoded).metadata()
  assert.equal(metadata.width, 50)
  assert.equal(metadata.height, 25)
  assert.deepEqual(await pixel(encoded, 25, 12), [0, 128, 0, 255])
  const handle = await target.elementHandle()
  assert(handle)
  const fixed = await handle.screenshot({ scale: 'css', path: `${directory}/fixed-handle.png` })
  assert.deepEqual(await pixel(fixed, 25, 12), [0, 128, 0, 255])
  await handle.dispose()
  const masked = await page.screenshot({
    scale: 'css',
    mask: [target],
    path: `${directory}/masked.png`,
  })
  assert.deepEqual(await pixel(masked, 60, 50), [255, 0, 255, 255])
  assert.deepEqual(await pixel(await page.screenshot({ scale: 'css' }), 60, 50), [0, 128, 0, 255])
  const state = z
    .object({ sessions: z.number(), activeHandles: z.number(), artifacts: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert(state.sessions > 0, 'Proof must use an actual renderer OOPIF session')
  assert.equal(state.activeHandles, 0)
  assert.equal(state.artifacts, 0)
  for (const file of files)
    assert.equal(
      createHash('sha256')
        .update(await readFile(file))
        .digest('hex'),
      provenance[file],
    )
  await writeFile(
    `${directory}/report.json`,
    `${JSON.stringify(
      {
        provenance,
        cefVersion: source.cefVersion,
        transformedOOPIFLocatorPixelsAndDimensions: 'passed',
        transformedOOPIFFixedElementHandlePixels: 'passed',
        transformedOOPIFMaskPixelsAndRestoration: 'passed',
        registries: state,
      },
      null,
      2,
    )}\n`,
  )
  console.log('Native transformed OOPIF locator, fixed handle and mask pixels verified')
} catch (error) {
  await writeFile(
    `${directory}/failure.json`,
    `${JSON.stringify({ error: String(error), provenance }, null, 2)}\n`,
  )
  throw error
} finally {
  await source.close()
  server.close()
}
