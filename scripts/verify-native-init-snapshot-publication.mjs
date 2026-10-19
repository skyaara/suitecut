// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-init-snapshot-publication',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 360,
  height: 240,
  onDiagnostic: diagnostics.observe,
})
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(
    `<!doctype html><button>Actual</button><script>globalThis.firstAuthorSnapshot=globalThis.acceptedSnapshotVersion</script>${request.url === '/parent' ? '<iframe id=child src=/child></iframe>' : ''}`,
  )
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
const Counter = z.number().int().nonnegative()
const Version = z
  .string()
  .regex(/^\d+$/u)
  .transform((value) => BigInt(value))
const Snapshot = z.object({
  nativeSnapshotPublications: z.array(
    z.object({ contextId: Counter, version: Version, bytes: Counter }),
  ),
  nativeSnapshotPeers: z.array(
    z.object({
      pageId: Counter,
      contextId: Counter,
      browserId: Counter,
      rendererEpoch: Version,
      channelFrame: z.string(),
      routeFrame: z.string(),
      submittedVersion: Version,
      acknowledgedVersion: Version,
      ready: z.boolean(),
    }),
  ),
  nativeSnapshotInstallAcks: Version,
  nativeSnapshotReleaseAcks: Version,
  nativeSnapshotPhysicalRegions: Counter,
  nativeSnapshotPhysicalBytes: Counter,
  nativeSnapshotWaiters: Counter,
  nativeSnapshotPublicationContexts: Counter,
  nativeSnapshotPublicationBodies: Counter,
  nativeSnapshotPublicationBytes: Counter,
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/** @type {unknown[]} */
const errors = []
const browser = new NativeBrowserContexts({
  signal: source.processSignal ?? source.signal,
  runContext: (action, params, signal) => {
    assert(typeof source.runContext === 'function')
    return source.runContext(action, params, signal)
  },
  pageSource: (id) => {
    assert(typeof source.pageSource === 'function')
    return source.pageSource(id)
  },
  onContextEvent: (listener) => {
    assert(typeof source.onContextEvent === 'function')
    return source.onContextEvent(listener)
  },
})
const read = async () => {
  assert(typeof source.runContext === 'function')
  return Snapshot.parse(await source.runContext('list'))
}
/**
 * Waits for measured native receipts without inferring physical retirement.
 * @param {(value:z.output<typeof Snapshot>)=>boolean} predicate - Actual native state condition.
 */
const until = async (predicate) => {
  const deadline = performance.now() + 10000
  for (;;) {
    const actual = await read()
    if (predicate(actual)) {
      observations.push(actual)
      return actual
    }
    assert(
      performance.now() < deadline,
      'Actual native snapshot receipts did not settle within checker budget',
    )
    await setImmediate()
  }
}
/**
 * Reads the actual aggregate publication version for one context.
 * @param {z.output<typeof Snapshot>} value - Native diagnostic state.
 * @param {number} id - Actual context identity.
 */
const version = (value, id) => {
  const current = value.nativeSnapshotPublications.find((item) => item.contextId === id)
  assert(current)
  return current.version
}
/**
 * Checks actual install ACK versions for currently admitted receivers.
 * @param {z.output<typeof Snapshot>} value - Native diagnostic state.
 * @param {number} context - Actual context identity.
 */
const current = (value, context) => {
  const peers = value.nativeSnapshotPeers.filter((peer) => peer.contextId === context && peer.ready)
  return (
    peers.length > 0 && peers.every((peer) => peer.acknowledgedVersion >= version(value, context))
  )
}
try {
  const root = await until((value) => current(value, 1))
  assert(root.nativeSnapshotInstallAcks > 0n && root.nativeSnapshotPhysicalRegions > 0)
  checks.push(
    'Actual renderer admission and cross-process mapped-region install ACK precede measured current-version readiness',
  )
  const context = await browser.newContext({ locale: 'fr-FR' })
  const page = await context.newPage()
  const first = await until((value) => current(value, context.id))
  const initial = version(first, context.id)
  await context.addInitScript('globalThis.acceptedSnapshotVersion=1;void 0')
  const script = await read()
  observations.push(script)
  assert.equal(version(script, context.id), initial + 1n)
  assert(current(script, context.id))
  await page.goto(prefix + '/parent')
  assert.equal(await page.evaluate('globalThis.firstAuthorSnapshot'), 1)
  assert.equal(
    await page
      .frameLocator('#child')
      .locator('button')
      .evaluate(() => /** @type {unknown} */ (Reflect.get(globalThis, 'firstAuthorSnapshot'))),
    1,
  )
  checks.push(
    'Awaited native script mutation has actual current ACK; unchanged synchronous bootstrap serves first-author main and sibling state',
  )
  const binding = await context.exposeFunction(
    'actualSnapshotSum',
    (...args) => z.number().parse(args[0]) + z.number().parse(args[1]),
  )
  const bound = await read()
  observations.push(bound)
  assert.equal(version(bound, context.id), initial + 2n)
  assert(current(bound, context.id))
  assert.equal(await page.evaluate('globalThis.actualSnapshotSum(6,7)'), 13)
  checks.push(
    'Real binding registration advances the same native publication order and remains callable through the existing native binding controller',
  )
  await context.credentials.install()
  const credential = await read()
  observations.push(credential)
  assert.equal(version(credential, context.id), initial + 3n)
  assert(current(credential, context.id))
  assert.deepEqual(await context.credentials.get(), [])
  assert.equal(version(await read(), context.id), initial + 3n)
  checks.push(
    'Native credential readiness commits in the same aggregate order; an actual read does not create an invented publication',
  )
  const beforeNavigation = credential.nativeSnapshotPeers.find(
    (peer) => peer.pageId === page.source.pageId,
  )
  assert(beforeNavigation)
  await page.goto(prefix + '/next')
  const navigated = await until((value) => current(value, context.id))
  const afterNavigation = navigated.nativeSnapshotPeers.find(
    (peer) => peer.pageId === page.source.pageId,
  )
  assert(afterNavigation)
  assert.equal(afterNavigation.rendererEpoch, beforeNavigation.rendererEpoch)
  assert.equal(version(navigated, context.id), initial + 3n)
  assert.equal(await page.evaluate('globalThis.firstAuthorSnapshot'), 1)
  checks.push(
    'Same-renderer navigation preserves browser cache incarnation and aggregate version while genuine document bootstrap still uses its existing fallback',
  )
  const second = await context.newPage()
  await until(
    (value) =>
      value.nativeSnapshotPeers.filter((peer) => peer.contextId === context.id && peer.ready)
        .length === 2 && current(value, context.id),
  )
  await context.addInitScript('globalThis.acceptedSnapshotVersion=2;void 0')
  const two = await read()
  observations.push(two)
  assert.equal(version(two, context.id), initial + 4n)
  assert(current(two, context.id))
  await second.goto(prefix + '/second')
  assert.equal(await second.evaluate('globalThis.firstAuthorSnapshot'), 2)
  checks.push(
    'A later accepted mutation waits for both actually admitted native browser receivers, without inferring their state from independent counters',
  )
  await binding.dispose()
  const disposed = await read()
  observations.push(disposed)
  assert.equal(version(disposed, context.id), initial + 5n)
  assert(current(disposed, context.id))
  const settled = await until(
    (value) =>
      value.nativeSnapshotPhysicalRegions ===
        value.nativeSnapshotPeers.filter((peer) => peer.ready).length &&
      value.nativeSnapshotWaiters === 0,
  )
  assert(settled.nativeSnapshotReleaseAcks > root.nativeSnapshotReleaseAcks)
  checks.push(
    'Binding disposal advances authoritative state; superseded mapped regions retire only after actual receiver release receipts',
  )
  await context.close()
  const closed = await until(
    (value) =>
      !value.nativeSnapshotPeers.some((peer) => peer.contextId === context.id) &&
      value.nativeSnapshotPublicationContexts === 1 &&
      value.nativeSnapshotPhysicalRegions === 1 &&
      value.nativeSnapshotWaiters === 0,
  )
  assert.equal(await createNativePage(source).evaluate('6*7'), 42)
  checks.push(
    'Actual context/browser closure removes only owned cache receipts and preserves the healthy root',
  )
  assert.equal(checks.length, 8)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        scope:
          'Actual shared publication/ACK only; synchronous bootstrap fallback retained, no pristine child capability claim',
        checks,
        observations,
        closed,
      },
      (_key, value) =>
        typeof value === 'bigint' ? value.toString() : /** @type {unknown} */ (value),
      2,
    ) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, checks, error: String(error), observations },
      (_key, value) =>
        typeof value === 'bigint' ? value.toString() : /** @type {unknown} */ (value),
      2,
    ) + '\n',
  )
  throw error
} finally {
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native shared snapshot publication8 passed')
