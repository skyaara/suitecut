// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { test } from './helpers.mjs'

export const artifacts = [
  test('artifacts.screenshot-layout', ['smoke', 'artifacts'], async (h) => {
    const { page } = await h.page()
    await page.setContent(
      '<style>html,body{margin:0;background:rgb(10,20,30)}body{height:1800px}</style><h1>Layout artifact</h1>',
    )
    const png = await page.screenshot()
    assert.equal(png.subarray(0, 8).toString('hex'), '89504e470d0a1a0a')
    assert.equal(png.readUInt32BE(16), 960)
    assert.equal(png.readUInt32BE(20), 640)
    await writeFile(join(h.output, 'viewport.png'), png)
    const full = await page.screenshot({ fullPage: true })
    assert(full.readUInt32BE(20) >= 1800)
    await writeFile(join(h.output, 'full.png'), full)
    await page.setViewportSize({ width: 640, height: 480 })
    assert.deepEqual(await page.evaluate('({width:innerWidth,height:innerHeight})'), {
      width: 640,
      height: 480,
    })
    const clipped = await page.screenshot({ clip: { x: 0, y: 0, width: 100, height: 100 } })
    assert.equal(clipped.readUInt32BE(16), 100)
    assert.equal(clipped.readUInt32BE(20), 100)
    h.check(
      'Screenshots contain valid PNG dimensions for actual viewport, full page and clip after layout resize',
    )
  }),
  test('artifacts.trace-workflow', ['artifacts', 'journey'], async (h) => {
    const { page, context } = await h.page()
    await context.tracing.start({ screenshots: true, snapshots: true, sources: true })
    await page.goto(h.fixture.origin + '/editor')
    await page.locator('#subject').fill('Trace regression')
    await page.locator('#save').click()
    const path = join(h.output, 'workflow.zip')
    await context.tracing.stop({ path })
    const bytes = await readFile(path)
    assert.equal(bytes.subarray(0, 4).toString('hex'), '504b0304')
    assert(bytes.length > 1000)
    h.check('A real input workflow finalizes a nonempty native trace archive')
  }),
]
