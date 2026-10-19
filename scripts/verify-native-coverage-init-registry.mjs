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

const executablePath = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const dir = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-coverage-init-registry',
)
await mkdir(dir, { recursive: true })
/**
 * Actual helper endpoint, retained for unconditional cleanup.
 * @type {import('../dist/native.js').NativeBrowser | undefined}
 */
let source
let stderr = ''
/**
 * Real registry source ownership and process cleanup evidence.
 * @type {{result:string,binarySHA256:string,groups:string[],error?:string,nativeShutdown?:string,nativeStderrBytes?:number}}
 */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executablePath))
    .digest('hex'),
  groups: [],
}
try {
  source = await launchNativeBrowser({
    executablePath,
    headless: true,
    onDiagnostic: (m) => (stderr += m),
  })
  const context = createNativePage(source).context()
  await context.addInitScript('globalThis.actualAnonymousInit=42')
  const page = await context.newPage()
  await page.coverage.startJSCoverage()
  await page.goto('data:text/html,<script>function author(){return 1;}author()</script>')
  const entries = await page.coverage.stopJSCoverage()
  assert.equal(await page.evaluate('globalThis.actualAnonymousInit'), 42)
  assert.equal(entries.length, 1)
  assert(!entries.some((e) => e.source.includes('actualAnonymousInit')))
  report.groups.push('actual registered unnamed author init script remains anonymous by default')
  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  await page.goto('data:text/html,<body>real</body>')
  const anonymous = await page.coverage.stopJSCoverage()
  const init = anonymous.find((e) => e.source === 'globalThis.actualAnonymousInit=42')
  assert(init)
  assert.equal(init.url, '')
  report.groups.push('actual registered unnamed author init source preserved with opt-in')
  await context.addInitScript(
    'globalThis.namedAuthorInit=43;\n//# sourceURL=suitecut:init-script:author-chosen',
  )
  await page.coverage.startJSCoverage()
  await page.goto('data:text/html,<body>named</body>')
  const named = await page.coverage.stopJSCoverage()
  assert(
    named.some(
      (e) => e.url === 'suitecut:init-script:author-chosen' && e.source.includes('namedAuthorInit'),
    ),
  )
  report.groups.push(
    'author directive with native-looking init name remains genuine author coverage',
  )
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
