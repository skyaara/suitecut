// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { restoreNativeStorageState } from '../dist/native-storage-state.js'
import {
  launchNativeBrowser,
  NativeAPIRequestContext,
  NativeBrowserContexts,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-api-origins',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((_request, response) => {
  response.setHeader('Set-Cookie', 'live=actual; Path=/')
  response.setHeader('Content-Type', 'text/html')
  response.end('<!doctype html><body>Actual origin storage</body>')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 160,
  height: 100,
  onDiagnostic: diagnostics.observe,
})
const ownership = z.object({
  httpAPIOriginsAllocatedBytes: z.number().int().nonnegative(),
  httpAPIOriginsEntries: z.number().int().nonnegative(),
  httpAPIOriginsPendingEntries: z.number().int().nonnegative(),
  httpContexts: z.number().int().nonnegative(),
  httpProfilePhysical: z.number().int().nonnegative(),
  httpScopeCommands: z.number().int().nonnegative(),
  storageOperations: z.number().int().nonnegative(),
  storageClosingOperations: z.number().int().nonnegative(),
  storagePhysicalLeases: z.number().int().nonnegative(),
  storageRetainedBytes: z.number().int().nonnegative(),
  storageUploads: z.number().int().nonnegative(),
  storageUploadBytes: z.number().int().nonnegative(),
})
const pageOwnership = ownership.pick({
  httpAPIOriginsAllocatedBytes: true,
  httpAPIOriginsEntries: true,
  httpAPIOriginsPendingEntries: true,
  httpContexts: true,
  httpProfilePhysical: true,
  httpScopeCommands: true,
})
const storageOwnership = ownership.pick({
  storageOperations: true,
  storageClosingOperations: true,
  storagePhysicalLeases: true,
  storageRetainedBytes: true,
  storageUploads: true,
  storageUploadBytes: true,
})
/** Read each real diagnostic channel without defaulting missing native fields. */
async function state() {
  const page = pageOwnership.parse(await source.runAutomation('diagnostics'))
  assert(typeof source.runContext === 'function')
  const storage = storageOwnership.parse(await source.runContext('list'))
  return { ...page, ...storage }
}
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const errors = []
/** @type {import('../dist/native-http.js').NativeAPIRequestContext[]} */ const apis = []
/** @type {number[]} */ const rawContexts = []
/** @type {import('../dist/native-context.js').NativeBrowserContext|undefined} */ let browserContext
/** @type {unknown[]} */ const samples = []
/** @type {z.output<typeof ownership>|undefined} */ let finalOwnership
/**
 * Observe real native ownership without substituting a physical timer.
 * @param {(state:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const deadline = performance.now() + 5000
  while (true) {
    const observed = await state()
    if (predicate(observed)) return observed
    assert(performance.now() < deadline, 'Actual origin snapshot ownership did not settle')
  }
}
/**
 * Accepted constructor storage with deliberately non-lexical OPFS order.
 * @returns {import('../dist/native-storage-state.js').NativeStorageState}
 */
function initialState() {
  return {
    cookies: [],
    origins: [
      {
        origin: 'https://constructor.example',
        localStorage: [{ name: 'accepted', value: 'constructor' }],
        opfs: [
          { path: 'hello.txt', type: 'file', base64: 'SGVsbG8=' },
          { path: 'empty', type: 'directory' },
        ],
        indexedDB: [
          {
            name: 'db',
            version: 5,
            stores: [
              {
                name: 'records',
                keyPath: 'id',
                autoIncrement: false,
                indexes: [],
                records: [{ value: { id: 'foo', name: 'John Doe' } }],
              },
            ],
          },
        ],
      },
      { origin: 'https://second.example', localStorage: [{ name: 'second', value: 'snapshot' }] },
    ],
  }
}
try {
  const initial = initialState(),
    api = await NativeAPIRequestContext.create(source, { storageState: initial })
  apis.push(api)
  const response = await api.get(origin)
  await response.dispose()
  const full = await api.storageState({ indexedDB: true, opfs: true })
  assert.deepEqual(full.origins, initial.origins)
  assert.deepEqual(full.cookies, await api.cookies())
  assert(full.cookies.some((cookie) => cookie.name === 'live' && cookie.value === 'actual'))
  const first = await state()
  assert.equal(first.httpAPIOriginsEntries, 2)
  assert.equal(first.httpAPIOriginsPendingEntries, 0)
  assert(first.httpAPIOriginsAllocatedBytes > 0)
  samples.push(first)
  checks.push(
    'Genuine successful constructor restore ACK commits origin/OPFS/IDB order; HTTP response updates only the live native cookie jar',
  )
  for (const indexedDB of [false, true])
    for (const opfs of [false, true]) {
      const exported = await api.storageState({ indexedDB, opfs })
      assert.deepEqual(
        exported.origins,
        initial.origins.map((item) => {
          const { indexedDB: db, opfs: files, ...basic } = item
          return {
            ...basic,
            ...(indexedDB && db ? { indexedDB: db } : {}),
            ...(opfs && files ? { opfs: files } : {}),
          }
        }),
      )
    }
  const path = resolve(directory, 'actual-api-state.json')
  assert.deepEqual(await api.storageState({ indexedDB: true, opfs: true, path }), full)
  assert.deepEqual(JSON.parse(await readFile(path, 'utf8')), full)
  checks.push(
    'Each native optional-field projection retains accepted values and order; the saved public file contains the actual native result',
  )
  initial.origins.reverse()
  initial.origins[0]?.localStorage.push({
    name: 'late caller mutation',
    value: 'must not become native state',
  })
  assert.deepEqual((await api.storageState({ indexedDB: true, opfs: true })).origins, full.origins)
  const empty = await NativeAPIRequestContext.create(source)
  apis.push(empty)
  await empty.get(origin).then((response) => response.dispose())
  assert.deepEqual((await empty.storageState({ indexedDB: true, opfs: true })).origins, [])
  checks.push(
    'Native ownership is independent of caller object mutation, and API contexts without constructor origins export an empty snapshot',
  )
  const utf = initialState()
  utf.origins.splice(1)
  const utfOrigin = utf.origins[0]
  assert(utfOrigin)
  utfOrigin.localStorage = [{ name: 'isolated\ud800', value: 'actual\udfff\u0000' }]
  const db = utfOrigin.indexedDB?.[0]
  assert(db)
  const store = db.stores[0]
  assert(store)
  store.records = [{ value: { id: 'raw', value: '\ud800' } }]
  const unicode = await NativeAPIRequestContext.create(source, { storageState: utf })
  apis.push(unicode)
  assert.deepEqual(
    (await unicode.storageState({ indexedDB: true, opfs: true })).origins,
    utf.origins,
  )
  assert.deepEqual((await unicode.storageState()).origins, [
    { origin: utfOrigin.origin, localStorage: utfOrigin.localStorage },
  ])
  checks.push(
    'Accepted raw stateJSON retains isolated UTF16 in localStorage and IndexedDB while optional top-level fields are projected natively',
  )
  const contexts = new NativeBrowserContexts({
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
  browserContext = await contexts.newContext()
  const page = await browserContext.newPage()
  await page.goto(origin)
  await page.evaluate('localStorage.setItem("real","before")')
  assert.equal(
    (await browserContext.request.storageState()).origins[0]?.localStorage[0]?.value,
    'before',
  )
  await page.evaluate('localStorage.setItem("real","after")')
  assert.equal(
    (await browserContext.request.storageState()).origins[0]?.localStorage[0]?.value,
    'after',
  )
  await browserContext.close()
  browserContext = undefined
  checks.push(
    'Associated browser API contexts continue exporting genuine mutable browser origin storage rather than an independent API snapshot',
  )
  for (const held of apis.splice(0)) await held.dispose()
  await waitState((state) => Object.values(state).every((value) => value === 0))
  const malformed = initialState()
  const badStore = malformed.origins[0]?.indexedDB?.[0]?.stores[0]
  assert(badStore)
  badStore.records = [{ valueEncoded: { v: 'invalid-native-value' } }]
  await assert.rejects(
    NativeAPIRequestContext.create(source, { storageState: malformed }),
    /Invalid native IndexedDB primitive/u,
  )
  const afterFailure = await waitState((state) =>
    Object.values(state).every((value) => value === 0),
  )
  samples.push(afterFailure)
  checks.push(
    'Real IndexedDB restore failure never commits the provisional snapshot and physical profile close releases every retained native origin byte',
  )
  // Original operation cancellation is applied to an actual native restore,
  // not a simulated sender. Multiple real origins keep the restore observable.
  const slow = {
    cookies: [],
    origins: Array.from({ length: 16 }, (_, i) => ({
      origin: `https://cancel-${i}.example`,
      localStorage: [{ name: 'actual', value: 'x'.repeat(300000) }],
    })),
  }
  const info = z
    .object({
      apiContextId: z.number().int().positive(),
      browserContextId: z.number().int().positive(),
    })
    .parse(
      await source.runAutomation('http', {
        action: 'create',
        scope: 'independent',
        initializeStorage: true,
      }),
    )
  rawContexts.push(info.apiContextId)
  const abort = new AbortController(),
    reason = new Error('Actual original origins restore cancellation')
  /** @type {(() => void)|undefined} */ let restoreSubmitted
  const submitted = new Promise((resolve) => {
    restoreSubmitted = () => resolve(undefined)
  })
  const restoring = restoreNativeStorageState(
    {
      runContext: (storageAction, params) => {
        const pending = source.runAutomation(
          'http',
          { action: 'initializeStorage', apiContextId: info.apiContextId, storageAction, params },
          storageAction === 'setStorageState' ? abort.signal : undefined,
        )
        if (storageAction === 'setStorageState') restoreSubmitted?.()
        return pending
      },
    },
    info.browserContextId,
    slow,
  )
  /**
   * Observe original cancellation without exposing an unhandled promise.
   * @param {unknown} error
   */
  const failed = (error) => ({ completed: false, error })
  const outcome = restoring.then(() => ({ completed: true, error: undefined }), failed)
  // Uploading 4.8 MB is a distinct operation. Begin the observation budget
  // only after the real restore command is sent, preserving the physical check.
  await Promise.race([
    submitted,
    outcome.then(() => {
      throw new Error('Actual restore settled before admission was observed')
    }),
  ])
  const held = await waitState(
    (state) => state.httpAPIOriginsPendingEntries === 16 && state.storagePhysicalLeases > 0,
  )
  samples.push(held)
  abort.abort(reason)
  const result = await outcome
  assert.equal(result.completed, false)
  assert.equal(result.error, reason)
  await source.runAutomation('http', { action: 'dispose', apiContextId: info.apiContextId })
  rawContexts.splice(rawContexts.indexOf(info.apiContextId), 1)
  const final = await waitState((state) => Object.values(state).every((value) => value === 0))
  samples.push(final)
  checks.push(
    'Exact caller abort retires its actual pending restore owner without committing; disposal waits real native utility/profile settlement and returns retained bytes to zero',
  )
  // Deterministic expiry-before-commit is covered by the native component;
  // this verifier makes no short-timeout speed assumption about CEF startup.
  assert.equal(checks.length, 7)
  finalOwnership = final
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        checks,
        error: error instanceof Error ? error.stack : String(error),
        samples,
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  for (const api of apis) {
    try {
      await api.dispose()
    } catch (error) {
      errors.push(error)
    }
  }
  for (const apiContextId of rawContexts) {
    try {
      await source.runAutomation('http', { action: 'dispose', apiContextId })
    } catch (error) {
      errors.push(error)
    }
  }
  if (browserContext) {
    try {
      await browserContext.close()
    } catch (error) {
      errors.push(error)
    }
  }
  server.closeAllConnections()
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } catch (error) {
    errors.push(error)
  }
  try {
    assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  } catch (error) {
    errors.push(error)
  }
}
await writeFile(
  resolve(directory, 'report.json'),
  JSON.stringify(
    {
      completed: errors.length === 0,
      checks,
      samples,
      finalOwnership,
      exit: source.processExit?.(),
      nativeStderr: await diagnostics.summary(),
      errors: errors.map((error) => ({
        message: error instanceof Error ? error.message : String(error),
        stack: error instanceof Error ? error.stack : undefined,
      })),
    },
    null,
    2,
  ) + '\n',
)
if (errors.length) throw new AggregateError(errors, 'Actual native API origins proof failed')
