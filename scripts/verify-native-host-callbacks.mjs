// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setImmediate } from 'node:timers'

import * as z from 'zod'

import { nativeActionHookPhysicalCount } from '../dist/native-action-hooks.js'
import { nativeEventPredicatePhysicalCount } from '../dist/native-event-predicate.js'
import { createNativePage, launchNativeBrowser, NativeConsoleMessage } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-host-callbacks')
await mkdir(directory, { recursive: true })
const paths = [executable, 'dist/native-action-hooks.js', 'dist/native-event-predicate.js']
const hashes = await Promise.all(
  paths.map(async (path) =>
    createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  ),
)
const checks = []
let stderr = ''
const launch = () =>
  launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic(message) {
      stderr = (stderr + message).slice(-1048576)
    },
  })
const turn = () => new Promise((resolve) => setImmediate(resolve))
let source = await launch()
try {
  let page = createNativePage(source)
  await page.setContent('<button>Actual native action</button>')
  const finishActions = z.array(z.function({ input: [], output: z.void() })).parse([])
  const actions = Array.from({ length: 64 }, () =>
    page.click('button', {
      timeout: 1000,
      beforeStable: () => new Promise((resolve) => finishActions.push(resolve)),
    }),
  )
  assert.equal(
    (await Promise.allSettled(actions)).filter((value) => value.status === 'rejected').length,
    64,
  )
  assert.equal(finishActions.length, 64)
  assert.equal(nativeActionHookPhysicalCount(source), 64)
  assert.equal(
    z.object({ pausedActions: z.literal(0) }).parse(await source.runAutomation('diagnostics'))
      .pausedActions,
    0,
  )
  const original = source
  await source.close()
  source = await launch()
  page = createNativePage(source)
  await page.setContent('<button>New process</button>')
  let invoked = false
  await assert.rejects(
    page.click('button', {
      timeout: 1000,
      beforeStable: () => {
        invoked = true
      },
    }),
    /physical callback capacity/,
  )
  assert.equal(invoked, false)
  finishActions.shift()?.()
  await turn()
  assert.equal(nativeActionHookPhysicalCount(original), 63)
  await page.click('button', { beforeStable: () => undefined })
  for (const finish of finishActions) finish()
  await turn()
  assert.equal(nativeActionHookPhysicalCount(original), 0)
  checks.push(
    '64 actual action callbacks retain global quota across cancellation and native close/relaunch; physical settlement recovers capacity',
  )

  const observed = z.array(z.instanceof(NativeConsoleMessage)).parse([])
  const event = page.waitForEvent('console', {
    timeout: 1000,
    predicate: async (message) => {
      observed.push(message)
      await turn()
      return message.text() === 'accepted'
    },
  })
  await page.evaluate(() => {
    console.log('first')
    console.log('accepted')
  })
  const accepted = await event
  assert.deepEqual(
    observed.map((message) => message.text()),
    ['first', 'accepted'],
  )
  assert.equal(accepted, observed[1])
  assert.equal(accepted.page(), page)
  checks.push(
    'native journal retains buffered rejected/accepted events under one host predicate deadline and returns actual accepted identity',
  )
  const finishEvents = z.array(z.function({ input: [z.boolean()], output: z.void() })).parse([])
  for (let batch = 0; batch < 2; ++batch) {
    const waiting = Array.from({ length: 32 }, () =>
      page.waitForEvent('console', {
        timeout: 1000,
        predicate: () => new Promise((resolve) => finishEvents.push(resolve)),
      }),
    )
    await page.evaluate(() => console.log('physical pending'))
    assert.equal(
      (await Promise.allSettled(waiting)).filter((value) => value.status === 'rejected').length,
      32,
    )
  }
  assert.equal(finishEvents.length, 64)
  assert.equal(nativeEventPredicatePhysicalCount(source), 64)
  const blocked = page.waitForEvent('console', { timeout: 1000, predicate: () => true })
  const rejected = assert.rejects(blocked, /physical host callback capacity/)
  await page.evaluate(() => console.log('blocked'))
  await rejected
  finishEvents.shift()?.(false)
  await turn()
  const recovery = page.waitForEvent('console', (message) => message.text() === 'recovered')
  await page.evaluate(() => console.log('recovered'))
  assert.equal((await recovery).text(), 'recovered')
  for (const finish of finishEvents) finish(false)
  await turn()
  assert.equal(nativeEventPredicatePhysicalCount(source), 0)
  const diagnostics = z
    .record(z.string(), z.number())
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.pendingEventPredicates, 0)
  assert.equal(diagnostics.eventPredicateBytes, 0)
  assert.equal(diagnostics.pausedActions, 0)
  assert.equal(diagnostics.otherOperations, 0)
  checks.push(
    '64 actual event callbacks retain physical quota beyond native timeout; settlement and native registry cleanup recover capacity',
  )
  for (let index = 0; index < paths.length; ++index)
    assert.equal(
      createHash('sha256')
        .update(await readFile(paths[index]))
        .digest('hex'),
      hashes[index],
      'Proof runtime changed',
    )
  const report = {
    status: 'passed',
    checks,
    cefVersion: source.cefVersion,
    paths,
    hashes,
    diagnostics,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
