// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setImmediate as yieldTurn } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativePage,
  NativeResponse,
} from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-network-gc')
await mkdir(directory, { recursive: true })
const sha256 = () =>
  readFile(executable).then((bytes) => createHash('sha256').update(bytes).digest('hex'))
const executableSha256 = await sha256(),
  started = new Date().toISOString(),
  checks = [],
  stderr = []
const collect = z.function({ input: [], output: z.void() }).parse(Reflect.get(globalThis, 'gc'))
const server = createServer((request, response) => {
  response.setHeader('Content-Type', request.url === '/body' ? 'text/plain' : 'text/html')
  response.end(
    request.url === '/body'
      ? 'x'.repeat(4 * 1024 * 1024)
      : '<!doctype html><body>native GC proof</body>',
  )
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
let source, failure
async function inspect(candidate) {
  const page = z.instanceof(NativePage).parse(candidate)
  let captured
  const listener = (response) => {
    const actual = z.instanceof(NativeResponse).parse(response)
    if (actual.url() === `${origin}/body`) captured = actual
  }
  page.on('response', listener)
  assert.equal(
    await page.evaluate(
      `fetch('/body').then(response=>response.arrayBuffer()).then(bytes=>bytes.byteLength)`,
    ),
    4 * 1024 * 1024,
  )
  const response = z.instanceof(NativeResponse).parse(captured)
  const body = await response.body()
  assert.equal(body.length, 4 * 1024 * 1024)
  assert.equal(body[0], 120)
  assert.equal(body.at(-1), 120)
  page.off('response', listener)
  return { request: response.request(), response: new WeakRef(response), body: new WeakRef(body) }
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (message) => stderr.push(message),
  })
  const page = createNativePage(source)
  await page.goto(`${origin}/page`)
  const held = await inspect(page)
  for (let attempt = 0; attempt < 30; attempt++) {
    await yieldTurn()
    collect()
    await yieldTurn()
    if (!held.response.deref() && !held.body.deref()) break
  }
  assert.equal(held.response.deref(), undefined)
  assert.equal(held.body.deref(), undefined)
  assert.equal(held.request.knownResponse(), undefined)
  checks.push(
    'real native Request remains held while its Response and cached 4 MiB body are garbage collected',
  )
  const replay = await held.request.response()
  assert.ok(replay)
  assert.equal(replay.request(), held.request)
  assert.equal(replay.status(), 200)
  assert.equal((await replay.body()).length, 4 * 1024 * 1024)
  checks.push(
    'response metadata recreates with genuine request identity and native CEF body ownership remains readable',
  )
} catch (error) {
  failure = error instanceof Error ? error : new Error(String(error))
} finally {
  if (source) await source.close().catch(() => undefined)
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  assert.equal(
    await sha256(),
    executableSha256,
    'Native executable changed during immutable GC proof',
  )
  await writeFile(resolve(directory, 'stderr.log'), stderr.join('\n'))
  const report = {
    ok: !failure,
    started,
    ended: new Date().toISOString(),
    executableSha256,
    checks,
    failure: failure?.stack,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
  if (failure) process.exitCode = 1
}
