// SPDX-License-Identifier: MIT
/* global document, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-element-geometry')
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const observations = []
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html')
  if (request.url === '/frame')
    response.end(
      '<style>body{margin:0}</style><div id=box style="position:absolute;left:12px;top:15px;width:80px;height:30px;background:blue"></div>',
    )
  else
    response.end(
      `<style>body{margin:0}</style><iframe id=child name=cross src="http://localhost:${port}/frame" style="position:fixed;left:50px;top:70px;width:200px;height:140px;border:3px solid black"></iframe>`,
    )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', resolve))
const address = server.address()
assert(address && typeof address !== 'string')
const port = address.port
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 640,
  height: 360,
  headless: true,
})
const close = async () => {
  await source.close()
  await new Promise((resolve, reject) =>
    server.close((error) => (error ? reject(error) : resolve())),
  )
}
try {
  const page = createNativePage(source)
  /**
   * Verify the observed root viewport box for a genuine element handle.
   * @param {string} mode
   * @param {import('../dist/native.js').NativeElementHandle} handle
   * @param {{x: number, y: number, width: number, height: number}} expected
   */
  const check = async (mode, handle, expected) => {
    const actual = await handle.boundingBox()
    assert(actual, `${mode} should have a visible bounding box`)
    const keys = z.enum(['x', 'y', 'width', 'height']).options
    for (const key of keys)
      assert(
        Math.abs(actual[key] - expected[key]) < 0.25,
        `${mode} ${key}: expected ${expected[key]}, observed ${actual[key]}`,
      )
    observations.push({ mode, actual, expected })
    await handle.dispose()
  }
  await page.setContent(
    '<div id=box style="position:absolute;left:20px;top:30px;width:100px;height:40px;box-sizing:border-box;border:5px solid black;padding:10px"></div>',
  )
  const main = await page.$('#box')
  assert(main)
  await check('main', main, { x: 20, y: 30, width: 100, height: 40 })
  await page.setContent(
    '<div style="height:2000px"></div><div id=box style="position:absolute;left:20px;top:500px;width:100px;height:40px"></div>',
  )
  await page.evaluate(async () => {
    window.scrollTo(0, 450)
    await new Promise((resolve) => window.requestAnimationFrame(resolve))
  })
  const scrolled = await page.$('#box')
  assert(scrolled)
  await check('main-scrolled', scrolled, { x: 20, y: 50, width: 100, height: 40 })
  await page.setContent(
    '<style>body{margin:0}</style><iframe id=child name=same style="position:fixed;left:50px;top:70px;width:200px;height:140px;border:3px solid black"></iframe>',
  )
  await page.evaluate(async () => {
    window.scrollTo(0, 0)
    const frame = document.querySelector('#child')
    if (!(frame instanceof HTMLIFrameElement)) throw new Error('Missing frame')
    await new Promise((resolve) => {
      frame.onload = resolve
      frame.srcdoc =
        '<style>body{margin:0}</style><div id=box style="position:absolute;left:12px;top:15px;width:80px;height:30px"></div>'
    })
  })
  const same = await page.frameLocator('#child').locator('#box').elementHandle()
  assert(same)
  await check('same-process-frame', same, { x: 65, y: 88, width: 80, height: 30 })
  await page.goto(`http://127.0.0.1:${port}/`)
  const other = page.frameLocator('#child').locator('#box')
  await other.waitFor({ state: 'attached' })
  const cross = await other.elementHandle()
  assert(cross)
  await check('cross-origin-frame', cross, { x: 65, y: 88, width: 80, height: 30 })
  const diagnostics = await page.runAutomation('diagnostics')
  await writeFile(join(directory, 'diagnostics.json'), JSON.stringify(diagnostics, null, 2))
  z.object({ sessions: z.number().int().positive(), activeHandles: z.literal(0) })
    .passthrough()
    .parse(diagnostics)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      { completed: true, binarySHA256, cefVersion: source.cefVersion, observations, diagnostics },
      null,
      2,
    ),
  )
  console.log(`Native element geometry verified: ${join(directory, 'report.json')}`)
} finally {
  await close()
}
