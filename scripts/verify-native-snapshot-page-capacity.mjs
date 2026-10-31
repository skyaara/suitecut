// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

// Each open page holds a shared init-script snapshot region. Pages left open
// (popups a site never closes, many tabs) used to exhaust a 64-region budget,
// after which every new page was closed during snapshot admission.
const PAGES = 100
const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const dir = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-snapshot-page-capacity',
)
await mkdir(dir, { recursive: true })
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
let stderr = ''
/**
 * Real snapshot receivers held open at once, and the init script on the last.
 * @type {{result:string,binarySHA256:string,openPages:number,initializedPages:number,receiverPeers?:number,physicalRegions?:number,error?:string,nativeShutdown?:string,nativeStderrBytes?:number}}
 */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executablePath))
    .digest('hex'),
  openPages: 0,
  initializedPages: 0,
}
try {
  source = await launchNativeBrowser({
    executablePath,
    headless: true,
    onDiagnostic: (m) => (stderr += m),
  })
  const context = createNativePage(source).context()
  await context.addInitScript('globalThis.snapshotCapacityInit=7')
  const pages = []
  for (let i = 0; i < PAGES; i++) {
    const page = await context.newPage()
    await page.goto(`data:text/html,<title>page ${i}</title>`)
    pages.push(page)
    report.openPages = pages.length
  }
  for (const page of pages) {
    assert.equal(await page.evaluate('globalThis.snapshotCapacityInit'), 7)
    report.initializedPages++
  }
  assert(typeof source.runContext === 'function')
  const diagnostics = z
    .object({ nativeSnapshotReceiverPeers: z.number(), nativeSnapshotPhysicalRegions: z.number() })
    .parse(await source.runContext('list'))
  report.receiverPeers = diagnostics.nativeSnapshotReceiverPeers
  report.physicalRegions = diagnostics.nativeSnapshotPhysicalRegions
  assert(report.receiverPeers >= PAGES)
  assert(report.physicalRegions >= PAGES)
  report.result = 'pass'
} catch (error) {
  report.error = String(error)
  throw error
} finally {
  await source?.close()
  report.nativeShutdown = 'clean'
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(dir + '/report.json', JSON.stringify(report, null, 2))
  await writeFile(dir + '/native-stderr.log', stderr)
}
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(JSON.stringify(report))
