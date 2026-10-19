// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setImmediate, setTimeout } from 'node:timers'

import * as z from 'zod'

import { nativeURLPredicatePhysicalCount } from '../dist/native-url-predicate.js'
import { createNativePage, expectNative, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const executableHash = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const sdkPath = resolve('dist/native-url-predicate.js')
const sdkHash = createHash('sha256')
  .update(await readFile(sdkPath))
  .digest('hex')
const directory = resolve('.suitecut/native-url-predicates')
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
try {
  const page = createNativePage(source)
  const protocol = 'about:'
  await expectNative(page).toHaveURL((url) => url.protocol === protocol, { timeout: 1 })
  await expectNative(page).not.toHaveURL((url) => url.protocol === 'https:', { timeout: 1 })
  let observations = 0
  await expectNative(page).toHaveURL(() => ++observations === 3, { timeout: 1000 })
  assert.equal(observations, 3)
  await assert.rejects(
    expectNative(page).toHaveURL(
      () => {
        throw new Error('actual host rejection')
      },
      { timeout: 1000 },
    ),
    /actual host rejection/,
  )
  await Promise.all(
    Array.from({ length: 64 }, () =>
      expectNative(page).toHaveURL((url) => url.protocol === protocol, { timeout: 1000 }),
    ),
  )
  let navigationObservations = 0
  const navigationRounds = 24
  for (let round = 0; round < navigationRounds; ++round) {
    await page.goto('data:text/html,<div>A</div>')
    const assertion = expectNative(page).toHaveURL(
      (url) => {
        ++navigationObservations
        return url.href === 'data:text/html,<div>B</div>'
      },
      { timeout: 1000 },
    )
    const navigation = new Promise((resolve, reject) => {
      setTimeout(() => {
        void page.goto('data:text/html,<div>B</div>').then(resolve, reject)
      }, 500)
    })
    await Promise.all([assertion, navigation])
  }
  const finish = z.array(z.function({ input: [z.boolean()], output: z.void() })).parse([])
  const pending = Array.from({ length: 64 }, () =>
    expectNative(page).toHaveURL(
      () =>
        new Promise((resolve) => {
          finish.push(resolve)
        }),
      { timeout: 20 },
    ),
  )
  const results = await Promise.allSettled(pending)
  assert.equal(results.filter((result) => result.status === 'rejected').length, 64)
  assert.equal(finish.length, 64)
  assert.equal(nativeURLPredicatePhysicalCount(source), 64)
  await assert.rejects(
    expectNative(page).toHaveURL(() => true, { timeout: 20 }),
    /physical callback capacity/,
  )
  const first = finish.shift()
  assert(first)
  first(false)
  await new Promise((resolve) => setImmediate(resolve))
  assert.equal(nativeURLPredicatePhysicalCount(source), 63)
  await expectNative(page).toHaveURL(() => true, { timeout: 1000 })
  for (const done of finish) done(false)
  await new Promise((resolve) => setImmediate(resolve))
  assert.equal(nativeURLPredicatePhysicalCount(source), 0)
  assert.equal(await page.evaluate(() => 42), 42)
  const diagnostics = z
    .record(z.string(), z.number())
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.pendingURLPredicates, 0)
  assert.equal(diagnostics.pendingFirstObservations, 0)
  assert.equal(diagnostics.otherOperations, 0)
  assert.equal(diagnostics.pendingDevTools, 0)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    executableHash,
    'Executable changed during proof',
  )
  assert.equal(
    createHash('sha256')
      .update(await readFile(sdkPath))
      .digest('hex'),
    sdkHash,
    'SDK changed during proof',
  )
  const report = {
    cefVersion: source.cefVersion,
    executable,
    executableHash,
    sdkHash,
    closure: 'passed',
    tinyTimeout: 'passed',
    nativeRetries: observations,
    negative: 'passed',
    hostError: 'passed',
    saturatedAcknowledgments: 64,
    mainFrameReplacementRounds: navigationRounds,
    navigationObservations,
    physicalCallbacksAfterNativeCancel: 64,
    physicalRecovery: 'passed',
    diagnostics,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
