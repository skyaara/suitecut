// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const binarySHA256 = await hash()
let receive = () => undefined
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html')
  receive()
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address !== 'string')
let nativeStderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (message) => {
    nativeStderr += message
  },
})
const manager = new NativeBrowserContexts(source)
const checks = []
try {
  const context = await manager.newContext()
  const page = await context.newPage()
  const requestObserved = new Promise((done) => {
    receive = done
  })
  const navigation = page.goto(`http://127.0.0.1:${address.port}/held`, { timeout: 0 })
  // Chromium can report the physical request abort before the page-close event.
  const actualFailure = assert.rejects(
    navigation,
    (error) =>
      error instanceof Error &&
      ['genuine navigation teardown', 'net::ERR_ABORTED'].includes(error.message),
  )
  await requestObserved
  await context.close({ reason: 'genuine navigation teardown' })
  await actualFailure
  checks.push('awaited held navigation retains its actual closure rejection')

  const saturated = await manager.newContext()
  const waitingPage = await saturated.newPage()
  // Admit waits after the actual renderer is ready, rather than filling the
  // SDK's initial-page queue and closing it before native dispatch.
  assert.equal(await waitingPage.evaluate(() => 0), 0)
  const outcomes = Array.from({ length: 64 }, () => waitingPage.waitForNavigation({ timeout: 0 }))
  const settled = Promise.allSettled(outcomes)
  await saturated.close({ reason: 'native cleanup at ordinary command capacity' })
  const results = await settled
  assert.equal(results.length, 64)
  for (const result of results) {
    assert.equal(result.status, 'rejected')
    if (result.status === 'rejected')
      assert.equal(String(result.reason), 'Error: native cleanup at ordinary command capacity')
  }
  checks.push('context cleanup completes while all 64 ordinary navigation slots are pending')
  const diagnostics = z
    .object({ otherOperations: z.literal(0), navigations: z.literal(0) })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(nativeStderr, '')
  assert.equal(await hash(), binarySHA256)
  await source.close()
  const processExit = source.processExit?.()
  assert.deepEqual(processExit, { code: 0, signal: null })
  assert.equal(nativeStderr, '')
  checks.push('native process physically exits with code zero after navigation cancellation')
  const directory = resolve('.suitecut/native-navigation-close')
  await mkdir(directory, { recursive: true })
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: source.cefVersion,
        checks,
        diagnostics,
        nativeStderr,
        processExit,
      },
      null,
      2,
    ),
  )
  console.log(`Native navigation close verified: ${directory}/report.json`)
} finally {
  try {
    await source.close()
  } finally {
    server.closeAllConnections()
    await new Promise((done) => server.close(done))
  }
}
