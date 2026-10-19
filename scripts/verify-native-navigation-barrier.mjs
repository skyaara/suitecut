// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeTimeoutError } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
let releaseResponse = () => undefined
let receivedRequest = () => undefined
const server = createServer((request, response) => {
  if (request.url === '/held') {
    releaseResponse = () => {
      response.end('<title>committed</title><p>real navigation</p>')
    }
    receivedRequest()
  } else response.end('<a id=link href=/held>navigate</a><a id=hash href=#fragment>fragment</a>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 480,
  height: 320,
})
const checks = []
try {
  const page = createNativePage(source)
  await page.goto(origin)
  let observed = new Promise((done) => {
    receivedRequest = done
  })
  let settled = false
  const click = page
    .locator('#link')
    .click()
    .then(() => {
      settled = true
    })
  await observed
  const waiting = z
    .object({
      actionNavigations: z.number().positive(),
      actionNavigationIntents: z.number().positive(),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(
    settled,
    false,
    'Native click must retain navigation ownership before response commit',
  )
  releaseResponse()
  await click
  assert.equal(page.url(), `${origin}/held`)
  assert.equal(await page.title(), 'committed')
  checks.push('native click waits for actual response commit under its original deadline')

  await page.goto(origin)
  observed = new Promise((done) => {
    receivedRequest = done
  })
  const noWait = page.locator('#link').click({ noWaitAfter: true })
  await observed
  await noWait
  const bypass = z
    .object({ actionNavigations: z.literal(0) })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  releaseResponse()
  await page.waitForLoadState()
  checks.push('noWaitAfter completes real input while HTTP response is still held')

  await page.goto(origin)
  await page.locator('#hash').click()
  assert.equal(page.url(), `${origin}/#fragment`)
  checks.push('same-document click barrier observes actual fragment navigation')
  let count = 0
  server.on('request', (request) => {
    if (request.url === '/') ++count
  })
  await page.goto(origin)
  const reload = await page.reload()
  assert.equal(reload?.status(), 200)
  assert.equal(count, 2)
  checks.push('native reload performs a new real document request')

  page.setDefaultTimeout(1)
  await assert.rejects(page.locator('#absent').click(), NativeTimeoutError)
  page.setDefaultTimeout(0)
  const attached = page.locator('#later').waitFor({ state: 'attached' })
  await page.evaluate(() => {
    const element = document.createElement('button')
    element.id = 'later'
    document.body.append(element)
  })
  await attached
  checks.push('default timeout 1 produces TimeoutError; 0 retains a cancellable native wait')
  const order = []
  const last = () => order.push('last')
  const first = () => order.push('first')
  page.addListener('console', last)
  page.prependListener('console', first)
  const logged = page.waitForEvent('console', (message) => message.text() === 'ordered')
  await page.evaluate(() => console.log('ordered'))
  await logged
  assert.deepEqual(order, ['first', 'last'])
  page.removeListener('console', first)
  page.removeListener('console', last)
  checks.push('native event objects preserve prepend/add/remove listener order')
  const diagnostics = z
    .object({
      actionNavigations: z.literal(0),
      actionNavigationIntents: z.literal(0),
      pendingEventPredicates: z.literal(0),
      eventPredicateBytes: z.literal(0),
      inputWork: z.literal(0),
      mouseButtons: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  const directory = resolve('.suitecut/native-navigation-barrier')
  await mkdir(directory, { recursive: true })
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: source.cefVersion,
        checks,
        waiting,
        bypass,
        diagnostics,
      },
      null,
      2,
    ),
  )
  console.log(`Native navigation barrier verified: ${directory}/report.json`)
} finally {
  releaseResponse()
  await source.close()
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
