// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeAPIRequestContext } from '../dist/native.js'
import { toError } from '../dist/untrusted.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-http-response-capacity',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const limit = 128 * 1048576
const schema = z.object({
  httpRetainedBodyCapacity: z.number().int().nonnegative(),
  httpRetainedBodyBytes: z.number().int().nonnegative(),
  httpActiveBodyBytes: z.number().int().nonnegative(),
  httpTransfers: z.number().int().nonnegative(),
  httpActive: z.number().int().nonnegative(),
  httpResponses: z.number().int().nonnegative(),
  httpContexts: z.number().int().nonnegative(),
  httpProfilePhysical: z.number().int().nonnegative(),
  httpScopeCommands: z.number().int().nonnegative(),
  httpCookieVisits: z.number().int().nonnegative(),
  httpCookieCallbacks: z.number().int().nonnegative(),
})
/** @type {string[]} */
const checks = []
/** @type {import('../src/untrusted.js').UntrustedInput[]} */
const samples = []
/** @type {Error[]} */
const errors = []
/** @type {import('../dist/native-http.js').NativeAPIResponse[]} */
const retained = []
// A fixed actual source buffer and real socket backpressure bound the server's
// own memory; the client does not retrieve/cache these bodies in JavaScript.
const chunk = Buffer.alloc(32771, 73)
const amount = 20 * 1048576 + 31
assert(amount * 6 < limit)
const server = createServer((request, response) => {
  if (request.url === '/warm') {
    response.end()
    return
  }
  response.writeHead(200, { 'Content-Length': amount, 'Content-Type': 'application/octet-stream' })
  let remaining = amount
  const pump = () => {
    while (remaining && !response.destroyed) {
      const next = Math.min(remaining, chunk.length)
      remaining -= next
      if (!response.write(chunk.subarray(0, next))) {
        response.once('drain', pump)
        return
      }
    }
    if (!remaining && !response.destroyed) response.end()
  }
  pump()
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/** @type {import('../dist/native-browser.js').NativeBrowser | undefined} */
let source
/** @type {import('../dist/native-http.js').NativeAPIRequestContext | undefined} */
let api
try {
  source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    width: 64,
    height: 64,
    onDiagnostic: diagnostics.observe,
  })
  api = await NativeAPIRequestContext.create(source)
  const warm = await api.get(origin + '/warm')
  await warm.dispose()
  let rejected = false
  for (let index = 0; index < 6; ++index) {
    try {
      retained.push(await api.get(origin + `/body/${index}`, { timeout: 10000 }))
    } catch (error) {
      assert(error instanceof Error)
      assert.match(error.message, /Native API response body budget exceeded/u)
      rejected = true
      break
    }
    const state = schema.parse(await source.runAutomation('diagnostics'))
    assert.equal(state.httpRetainedBodyBytes, amount * (index + 1))
    assert.equal(state.httpResponses, index + 1)
    assert(state.httpRetainedBodyCapacity >= state.httpRetainedBodyBytes)
    assert(state.httpRetainedBodyCapacity <= limit)
    samples.push(state)
  }
  assert(retained.length > 0 && retained.length < 6)
  checks.push(
    'genuine irregularly chunked responses retain their measured vector capacities under the native shared bound',
  )
  assert(
    rejected,
    'simultaneous old/replacement capacity must reject before all six logical bodies fit below128MiB',
  )
  const limited = schema.parse(await source.runAutomation('diagnostics'))
  assert.equal(limited.httpRetainedBodyBytes, amount * retained.length)
  assert.equal(limited.httpResponses, retained.length)
  assert.equal(limited.httpActiveBodyBytes, 0)
  assert.equal(limited.httpTransfers, 0)
  assert(limited.httpRetainedBodyCapacity <= limit)
  samples.push(limited)
  checks.push(
    'real request rejects on physical capacity although all six requested logical bodies fit, releasing its partial body',
  )

  for (const response of retained.splice(0)) await response.dispose()
  const emptied = schema.parse(await source.runAutomation('diagnostics'))
  assert.equal(emptied.httpRetainedBodyBytes, 0)
  assert.equal(emptied.httpRetainedBodyCapacity, 0)
  assert.equal(emptied.httpResponses, 0)
  samples.push(emptied)
  retained.push(await api.get(origin + '/body/after-dispose', { timeout: 10000 }))
  const resumed = schema.parse(await source.runAutomation('diagnostics'))
  assert.equal(resumed.httpRetainedBodyBytes, amount)
  assert.equal(resumed.httpResponses, 1)
  assert(resumed.httpRetainedBodyCapacity <= limit)
  samples.push(resumed)
  for (const response of retained.splice(0)) await response.dispose()
  await api.dispose()
  const final = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(final)) assert.equal(value, 0, name)
  samples.push(final)
  checks.push(
    'actual response disposal restores admission and final disposal releases all tracked vector capacity',
  )
} catch (error) {
  errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
} finally {
  for (const response of retained) {
    try {
      await response.dispose()
    } catch (error) {
      errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
    }
  }
  if (api) {
    try {
      await api.dispose()
    } catch (error) {
      errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
    }
  }
  server.closeAllConnections()
  try {
    await new Promise((resolve, reject) =>
      server.close((error) => (error ? reject(error) : resolve(undefined))),
    )
  } catch (error) {
    errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
  }
  if (source) {
    try {
      await finishNativeProof(source, diagnostics, directory, errors)
    } catch (error) {
      errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
    }
    try {
      assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
    } catch (error) {
      errors.push(toError(/** @type {import('../dist/untrusted.js').UntrustedInput} */ (error)))
    }
  }
}
const report = {
  completed: errors.length === 0,
  checks,
  samples,
  amount,
  sharedCapacityBound: limit,
  nativeStderr: await diagnostics.summary(),
  exit: source?.processExit?.(),
  errors: errors.map((error) => ({ message: error.message, stack: error.stack })),
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
if (errors.length) throw new AggregateError(errors, 'Actual native response capacity proof failed')
