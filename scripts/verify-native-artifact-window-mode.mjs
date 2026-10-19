// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import image from 'sharp'
import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-artifact-window-mode',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executablePath))
  .digest('hex')
const source = await launchNativeBrowser({
  executablePath,
  width: 200,
  height: 120,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
/** @type {unknown[]} */
const behaviorErrors = []
try {
  const page = createNativePage(source)
  await page.setContent(
    '<!doctype html><style>html,body{margin:0;background:blue}body{height:600px}</style>',
  )
  for (const params of [
    { fromSurface: false },
    {
      fromSurface: false,
      captureBeyondViewport: true,
      clip: { x: 0, y: 0, width: 200, height: 600, scale: 1 },
    },
  ]) {
    await assert.rejects(
      page.sendDevToolsCommand('Page.captureScreenshot', params),
      /fromSurface:false requires a platform window; SuiteCut CEF offscreen pages support surface capture only/u,
    )
    assert.equal(
      await page.evaluate('6*7'),
      42,
      'Unsupported window capture must leave the actual helper alive',
    )
  }
  for (const params of [{ format: 'png' }, { format: 'png', fromSurface: true }]) {
    const result = z
      .object({ data: z.string() })
      .parse(await page.sendDevToolsCommand('Page.captureScreenshot', params))
    const { data, info } = await image(Buffer.from(result.data, 'base64'))
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true })
    assert.deepEqual([info.width, info.height], [200, 120])
    assert.deepEqual(
      [...data.subarray((119 * info.width + 180) * 4, (119 * info.width + 180) * 4 + 4)],
      [0, 0, 255, 255],
    )
  }
  const full = await page.screenshot({
    fullPage: true,
    scale: 'css',
    path: resolve(directory, 'actual-fullpage.png'),
  })
  const { data, info } = await image(full).ensureAlpha().raw().toBuffer({ resolveWithObject: true })
  assert.deepEqual([info.width, info.height], [200, 600])
  assert.deepEqual(
    [...data.subarray((599 * info.width + 180) * 4, (599 * info.width + 180) * 4 + 4)],
    [0, 0, 255, 255],
  )
  const state = z
    .object({
      otherOperations: z.number(),
      pendingDevTools: z.number(),
      artifacts: z.number(),
      artifactPhysicalCapture: z.number(),
      artifactDocumentJobs: z.number(),
      artifactDocumentContexts: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const count of Object.values(state)) assert.equal(count, 0)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        groups: 3,
        unsupportedWindowModes: 2,
        genuineSurfaceModes: 2,
        publicFullpageActualPixels: true,
        finalOwnership: state,
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, binarySHA256, error: String(error) }, null, 2) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, behaviorErrors)
}
console.info(
  'Native unsupported OSR window snapshots reject; genuine default/explicit surface and public fullpage pixels pass',
)
