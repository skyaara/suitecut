// SPDX-License-Identifier: MIT
// Real release ACK/frame-detach and original-deadline regression.
/* global window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
const directory = resolve(
  process.env.SUITECUT_NATIVE_CLICK_PROOF_DIRECTORY ?? '.suitecut/native-click-detach',
)
await mkdir(directory, { recursive: true })
/** @type {typeof import('../dist/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects a locally hashed immutable SDK.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
const inputs = await Promise.all(
  [executable, `${sdk}/native-page.js`].map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
const checks = []
let stderr = ''
const source = await native.launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 640,
  height: 480,
  onDiagnostic(line) {
    stderr += line.slice(0, Math.max(0, 1048576 - stderr.length))
  },
})
const browser = new native.NativeBrowserContexts(source)
try {
  const context = await browser.newContext()
  const page = await context.newPage()
  for (const type of ['click', 'mousedown']) {
    await page.setContent('<iframe></iframe>')
    await page.locator('iframe').evaluate((node, eventType) => {
      ;/** @type {HTMLIFrameElement} */ (node).srcdoc =
        `<button id=remove>Remove ancestor</button><script>document.querySelector('button').addEventListener('${eventType}',event=>{parent.lastNativeRemoval={type:event.type,trusted:event.isTrusted};frameElement.remove();});</script>`
    }, type)
    const button = page.frameLocator('iframe').locator('#remove')
    await button.waitFor()
    if (type === 'click') await button.click({ timeout: 2000 })
    else await assert.rejects(button.click({ timeout: 2000 }), /detach|context|frame|attached/iu)
    assert.deepEqual(
      await page.evaluate(() => /** @type {unknown} */ (Reflect.get(window, 'lastNativeRemoval'))),
      { type, trusted: true },
    )
    assert.equal(await page.locator('iframe').count(), 0)
    checks.push(
      type === 'click'
        ? 'Final-release handler removes ancestor after genuine trusted click; native action succeeds'
        : 'Removal during genuine mousedown rejects before final-release completion',
    )
  }
  await page.setContent('<button>Delay release</button>')
  const start = performance.now()
  await assert.rejects(
    page.locator('button').click({ delay: 1000, timeout: 150 }),
    /timed out|Timeout/iu,
  )
  const elapsed = performance.now() - start
  assert(elapsed < 700, `Original150ms deadline must stop delayed release; observed ${elapsed}`)
  checks.push(
    'Original native deadline rejects during held press without waiting for configured1000ms delay',
  )
  await context.close()
  const cleanup = z
    .object({
      otherOperations: z.number(),
      inputWork: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(cleanup, {
    otherOperations: 0,
    inputWork: 0,
    activeHandles: 0,
    pendingHandles: 0,
  })
  checks.push('Native context closure releases physical input owners and handles')
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
for (const input of inputs)
  assert.equal(
    createHash('sha256')
      .update(await readFile(input.path))
      .digest('hex'),
    input.sha256,
  )
await writeFile(
  `${directory}/report.json`,
  JSON.stringify({ passed: checks.length, checks, inputs, inputsUnchanged: true }, null, 2) + '\n',
)
console.log(`Native click-detach: ${checks.length} groups passed`)
