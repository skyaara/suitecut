// SPDX-License-Identifier: MIT
/* global document, SharedWorker */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = resolve('.suitecut/native-devtools-scalar-ownership')
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
const server = createServer((request, response) => {
  response.setHeader('content-type', request.url === '/shared.js' ? 'text/javascript' : 'text/html')
  response.end(
    request.url === '/shared.js'
      ? 'onconnect = event => { globalThis.port = event.ports[0]; port.start(); };'
      : '<body>native scalar ownership</body>',
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address === 'object')
try {
  const page = createNativePage(source)
  await page.setContent('<input id=file type=file>')
  await page.evaluate(() => {
    const input = document.querySelector('#file')
    if (!input) throw new Error('Missing actual file input')
    input.setAttribute('aria-label', '\ud800')
  })
  const raw = z.object({ result: z.object({ value: z.string() }) }).parse(
    await source.runAutomation('queryEvaluate', {
      query: [{ engine: 'css', selector: '#file' }],
      all: true,
      dom: true,
      accessibilitySnapshot: true,
      function: 'function(nodes,metadata,forest){return JSON.stringify(forest)}',
      timeoutMs: 2000,
    }),
  )
  const forest = z
    .array(z.object({ role: z.string(), name: z.string() }))
    .parse(JSON.parse(raw.result.value))
  assert.equal(forest[0]?.name, '\ud800')
  assert.equal(await page.locator('#file').ariaSnapshot(), '- button ' + JSON.stringify('\ud800'))
  await assert.rejects(
    source.runAutomation('queryEvaluate', {
      query: [{ engine: 'css', selector: '#file' }],
      all: true,
      dom: true,
      function: 'function(){return "\\ud800"}',
      timeoutMs: 2000,
    }),
    /raw UTF16 scalar is unsupported/u,
  )
  const pageState = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      pendingAXCaptures: z.number(),
      pendingAXPhysicalCommands: z.number(),
      pendingAXCaptureBytes: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(pageState)) assert.equal(value, 0)
  await page.setContent('<h1>Recovered</h1>')
  assert.equal(await page.locator('h1').ariaSnapshot(), '- heading "Recovered" [level=1]')
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const workerWait = context.waitForSharedWorker({ timeout: 5000 })
  const workerPage = await context.newPage()
  await workerPage.goto(`http://127.0.0.1:${address.port}/`)
  await workerPage.evaluate(() => {
    Reflect.set(globalThis, 'retainedWorker', new SharedWorker('/shared.js'))
  })
  const worker = await workerWait
  const handle = await worker.evaluateHandle(() => ({ value: 7 }))
  try {
    const isolated = await worker.evaluateHandle(() => '\ud800a\0\\"\udfff😀')
    try {
      assert.equal(await isolated.jsonValue(), '\ud800a\0\\"\udfff😀')
      assert.match(isolated.toString(), /\\ud800/u)
    } finally {
      await isolated.dispose()
    }
    const authorObject = await worker.evaluateHandle(() => ({
      __suitecutNativeUTF16Scalar: 'd800',
      native: '\ud800',
    }))
    try {
      assert.deepEqual(await authorObject.jsonValue(), {
        __suitecutNativeUTF16Scalar: 'd800',
        native: '\ud800',
      })
    } finally {
      await authorObject.dispose()
    }
    assert.deepEqual(await handle.jsonValue(), { value: 7 })
  } finally {
    await handle.dispose()
  }
  assert.equal(await worker.evaluate(() => 42), 42)
  const brokerState = z
    .object({
      nativeWorkerBrokerContexts: z.array(
        z.object({ contextId: z.number(), physical: z.number(), handles: z.number() }),
      ),
    })
    .parse(await source.runAutomation('diagnostics'))
  const actual = brokerState.nativeWorkerBrokerContexts.find(
    (entry) => entry.contextId === context.id,
  )
  assert(actual)
  assert.equal(actual.physical, 0)
  assert.equal(actual.handles, 0)
  await context.close()
  const report = {
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks: [
      'actual raw AX and semantic UA preserve exact native isolated-unit caption without internal sidecars in public output',
      'unrelated non-opt-in raw scalar reply stays explicitly unsupported and retires native ownership',
      'actual private shared-worker native handle retains exact isolated/pair/NUL/backslash/quote units without disturbing existing handle',
      'normal commands recover with physical and handle ownership zero',
    ],
    pageState,
    brokerState,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await new Promise((done) => server.close(done))
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
