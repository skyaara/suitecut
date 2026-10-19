// SPDX-License-Identifier: MIT
// Controlled actual Node exception-boundary proof; no native browser or wire claim.
import assert from 'node:assert/strict'
import process from 'node:process'

import { NativeBrowserContexts } from '../dist/native.js'

const signal = new AbortController().signal
/**
 * Actual controlled event observer.
  @type {(event: import('../dist/native-protocol.js').NativeContextEvent) => void} */
let receive = () => {
  throw new Error('Controlled native event observer is unavailable')
}
/**
 * Controlled native facade values; no browser is created.
  @type {Map<number, import('../dist/native-browser.js').NativeBrowser>} */
const sources = new Map()
const context = new NativeBrowserContexts({
  signal,
  runContext: () => Promise.resolve(undefined),
  onContextEvent: (listener) => {
    receive = listener
    return () => undefined
  },
  pageSource: (id) => {
    if (!sources.has(id))
      sources.set(
        id,
        /** @type {import('../dist/native-browser.js').NativeBrowser} */ (
          /** @type {import('../dist/untrusted.js').UntrustedInput} */ ({
            signal,
            pageId: id,
            runAutomation: () => Promise.resolve(undefined),
          })
        ),
      )
    const source = sources.get(id)
    assert.ok(source)
    return source
  },
}).defaultContext()
const opener = context.pages()[0]
assert.ok(opener)
const page = context.adoptPage(7)
const pageError = new Error('original page listener exception')
const popupError = new Error('original popup listener exception')
const closeError = new Error('original close listener exception')
/** @type {Error[]} */
const errors = []
/** @type {string[]} */
const deliveries = []
/**
 * Complete the real exception receipt gate.
 * @type {() => void}
 */
let resolve = () => {
  throw new Error('Controlled exception receipt is unavailable')
}
const observed = new Promise((done) => {
  resolve = () => done(undefined)
})
process.on('uncaughtException', (error) => {
  assert.ok([pageError, popupError, closeError].includes(error), 'Unexpected real child exception')
  errors.push(error)
  if (errors.length === 3) resolve()
})
context.on('page', (actual) => {
  assert.equal(actual, page)
  deliveries.push('page-throws')
  throw pageError
})
context.on('page', (actual) => {
  assert.equal(actual, page)
  deliveries.push('page-after')
})
opener.on('popup', (actual) => {
  assert.equal(actual, page)
  deliveries.push('popup-throws')
  throw popupError
})
opener.on('popup', (actual) => {
  assert.equal(actual, page)
  deliveries.push('popup-after')
})
page.on('close', (actual) => {
  assert.equal(actual, page)
  deliveries.push('close-throws')
  throw closeError
})
page.on('close', (actual) => {
  assert.equal(actual, page)
  deliveries.push('close-after')
})
/** @type {import('../dist/native-protocol.js').NativeContextEvent} */
const created = { event: 'page-created', contextId: context.id, pageId: 7, openerId: 1 }
receive(created)
assert.equal(await page.opener(), opener)
receive(created)
receive({ event: 'page-closed', contextId: context.id, pageId: 7 })
assert.equal(page.isClosed(), true)
assert.equal(context.pageById(7), page)
assert.equal(context.pages().includes(page), false)
assert.throws(() => receive(created), /already closed/)
await observed
assert.deepEqual(errors, [pageError, popupError, closeError])
assert.deepEqual(deliveries, [
  'page-throws',
  'page-after',
  'popup-throws',
  'popup-after',
  'close-throws',
  'close-after',
])
process.stdout.write(
  JSON.stringify({
    checks: [
      'canonical late Page/popup listeners finish before real original uncaught errors',
      'duplicate announcement remains silent',
      'actual close listeners and closed-book cleanup finish despite original error',
    ],
    exceptionIdentities: true,
    errors: errors.map((error) => error.message),
    deliveries,
  }),
)
