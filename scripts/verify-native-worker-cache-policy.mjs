// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { NativeWorkerControlSchema } from '../dist/native-worker-protocol.js'
import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_CACHE_DIRECTORY ?? '.suitecut/native-worker-cache-policy',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const receipts = z
  .array(z.object({ policy: z.string(), phase: z.string(), value: z.unknown() }))
  .parse([])
const server = createServer((request, response) => {
  response.setHeader(
    'content-type',
    request.url === '/worker.js' ? 'application/javascript' : 'text/html',
  )
  response.end(
    request.url === '/worker.js'
      ? 'globalThis.actualCachePolicyValue=23;postMessage(23)'
      : '<body>actual cache policy navigation</body>',
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
for (const policy of ['omitted', 'preserved', 'disabled']) {
  let close = () => Promise.resolve()
  try {
    const source = await launchNativeBrowser({
      executablePath: executable,
      width: 320,
      height: 180,
      headless: true,
      ...(policy === 'omitted' ? {} : { backForwardCache: policy !== 'disabled' }),
      onDiagnostic: (value) => diagnostics.push(value),
    })
    close = () => source.close()
    const page = createNativePage(source)
    await page.goto(origin + '/before')
    const created = page.waitForEvent('worker', { timeout: 5000 })
    created.catch(() => undefined)
    await page.evaluate(
      'new Promise(resolve=>{globalThis.actualCachePolicyWorker=new Worker("/worker.js");actualCachePolicyWorker.onmessage=e=>resolve(e.data)})',
    )
    const worker = await created
    assert.equal(await worker.evaluate('actualCachePolicyValue'), 23)
    const original = NativeWorkerControlSchema.parse(worker.info.control)
    await page.goto(origin + '/after')
    if (policy === 'disabled') {
      assert.equal(
        worker.isClosed(),
        true,
        'Explicit disabled cache policy must deliver actual target close before navigation completion',
      )
      const targets = z
        .object({ targetInfos: z.array(z.object({ targetId: z.string() })) })
        .parse(await source.sendDevToolsCommand('Target.getTargets'))
      assert.equal(
        targets.targetInfos.some((value) => value.targetId === worker.info.id),
        false,
      )
      receipts.push({
        policy,
        phase: 'actual target absent after native navigation',
        value: targets,
      })
    } else {
      assert.equal(
        worker.isClosed(),
        false,
        'Preserved native cache policy must not invent worker execution death',
      )
      const target = await source.sendDevToolsCommand('Target.getTargetInfo', {
        targetId: worker.info.id,
      })
      assert.equal(
        z.object({ targetInfo: z.object({ targetId: z.string() }) }).parse(target).targetInfo
          .targetId,
        worker.info.id,
      )
      receipts.push({
        policy,
        phase: 'actual retained author target after native navigation',
        value: target,
      })
      const attached = await source.sendDevToolsCommand('Target.attachToTarget', {
        targetId: worker.info.id,
        flatten: true,
      })
      z.object({ sessionId: z.string().min(1) }).parse(attached)
      receipts.push({ policy, phase: 'actual same-target attach ACK', value: attached })
      await source.runAutomation('worker', {
        worker: worker.info.id,
        action: 'evaluate',
        expression: '1',
        timeoutMs: 5000,
      })
      const current = NativeWorkerControlSchema.parse(worker.info.control)
      assert.equal(current.owner, original.owner)
      assert.notEqual(current.generation, original.generation)
      assert.equal(await worker.evaluate('actualCachePolicyValue'), 23)
      receipts.push({
        policy,
        phase: 'actual recovered control and preserved author state23',
        value: current,
      })
    }
    checks.push(
      `${policy}: genuine native cache policy controls author lifetime without inferred death`,
    )
    await source.close()
    await source.processClosed
    assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
    receipts.push({
      policy,
      phase: 'actual helper process and stdio closure',
      value: source.processExit?.(),
    })
  } catch (error) {
    failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
  } finally {
    await close().catch((error) => failures.push(String(error)))
  }
  if (failures.length) break
}
server.closeAllConnections()
await new Promise((done) => server.close(() => done(undefined)))
assert.equal(
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  binarySHA256,
)
if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
await writeFile(
  resolve(directory, 'report.json'),
  JSON.stringify(
    {
      binarySHA256,
      checks,
      failures,
      receipts,
      scope:
        'Explicit pinned native launch policy. Omitted/true retain existing BFCache/history behavior; false disables native BFCache. No default or navigation-implied worker close.',
    },
    null,
    2,
  ),
)
assert.deepEqual(failures, [])
assert.equal(checks.length, 3)
