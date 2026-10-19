// SPDX-License-Identifier: MIT
/* global document, window, HTMLIFrameElement, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeElementHandle,
} from '../dist/native.js'

const directory = '.suitecut/native-element-owner-frame'
await mkdir(directory, { recursive: true })
let stderr = ''
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(
    request.url === '/child'
      ? '<p>Actual child</p>'
      : request.url === '/popup'
        ? '<p>Actual popup</p>'
        : '<div id="target">Actual target</div><iframe id="left" name="duplicate" src="/child"></iframe><iframe id="right" name="duplicate" src="/child"></iframe>',
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const checks = [],
  handles = []
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeOwnerFrameDocuments: z.number(),
  nativeOwnerFrameGroups: z.number(),
  nativeOwnerFramePhysical: z.number(),
  nativeOwnerFrameProcessPhysical: z.number(),
})
try {
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext(),
    page = await context.newPage()
  await page.goto(base)
  await page.frameLocator('#left').getByText('Actual child').waitFor({ timeout: 2000 })
  const target = await page.$('#target')
  assert(target)
  handles.push(target)
  assert.equal(await target.ownerFrame(), page.mainFrame())
  const documentHandle = (await page.evaluateHandle(() => document)).asElement()
  assert(documentHandle)
  handles.push(documentHandle)
  assert.equal(await documentHandle.ownerFrame(), page.mainFrame())
  checks.push('actual element and Document owner identities map to the canonical native main frame')
  const consolePromise = page.waitForEvent('console')
  await page.evaluate('console.log(document.body)')
  const message = await consolePromise
  const consoleNode = message.args()[0]
  assert(consoleNode instanceof NativeElementHandle)
  handles.push(consoleNode)
  assert.equal(await consoleNode.ownerFrame(), page.mainFrame())
  checks.push(
    'actual retained console node arguments materialize through the native handle owner before genuine document resolution',
  )
  const children = page.frames().filter((frame) => frame.parentFrame())
  assert.equal(children.length, 2)
  let left
  for (const frame of children) {
    const owner = await frame.frameElement()
    if ((await owner.getAttribute('id')) === 'left') left = frame
    await owner.dispose()
  }
  assert(left)
  const childBody = (await left.evaluateHandle(() => document.body)).asElement()
  assert(childBody)
  handles.push(childBody)
  assert.equal(await childBody.ownerFrame(), left)
  const childDocument = (await left.evaluateHandle(() => document)).asElement()
  assert(childDocument)
  handles.push(childDocument)
  assert.equal(await childDocument.ownerFrame(), left)
  checks.push(
    'actual same-origin child body and Document use genuine owner document identity despite duplicate frame names and URLs',
  )
  const crossRealm = (
    await page.evaluateHandle(() => {
      const frame = document.querySelector('#left')
      if (!(frame instanceof HTMLIFrameElement) || !frame.contentWindow)
        throw new Error('Actual child window missing')
      return frame.contentWindow.document.body
    })
  ).asElement()
  assert(crossRealm)
  handles.push(crossRealm)
  assert.equal(await crossRealm.ownerFrame(), left)
  const iframe = await page.$('#left')
  assert(iframe)
  handles.push(iframe)
  assert.equal(await iframe.ownerFrame(), page.mainFrame())
  checks.push(
    'a child node retained in the parent execution realm belongs to its real child while the actual iframe element belongs to the parent',
  )
  const detached = (
    await page.evaluateHandle(() => {
      const node = document.createElement('div')
      node.id = 'actual\ud800\0\\"'
      return node
    })
  ).asElement()
  assert(detached)
  handles.push(detached)
  assert.equal(await detached.ownerFrame(), page.mainFrame())
  const unattached = (
    await page.evaluateHandle(() =>
      document.implementation.createHTMLDocument('Unattached actual document'),
    )
  ).asElement()
  assert(unattached)
  handles.push(unattached)
  assert.equal(await unattached.ownerFrame(), null)
  checks.push(
    'real detached nodes retain their actual document owner, and a genuine unattached HTMLDocument has no owning frame',
  )
  const fresh = (
    await page.evaluateHandle(() => {
      const frame = document.createElement('iframe')
      frame.id = 'fresh'
      document.body.appendChild(frame)
      return frame.contentDocument.body
    })
  ).asElement()
  assert(fresh)
  handles.push(fresh)
  const freshFrame = await fresh.ownerFrame()
  assert(freshFrame)
  assert.equal(freshFrame.parentFrame(), page.mainFrame())
  const freshElement = await freshFrame.frameElement()
  assert.equal(await freshElement.getAttribute('id'), 'fresh')
  await freshElement.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual iframe owner missing')
    node.remove()
  })
  await freshElement.dispose()
  assert.equal(await fresh.ownerFrame(), null)
  checks.push(
    'immediate real iframe creation waits for actual main/utility contexts and removal leaves the genuinely retained parent-realm child document without an owning frame',
  )
  const right = children.find((frame) => frame !== left)
  assert(right)
  const crossURL = `http://localhost:${address.port}/child`
  await Promise.all([
    right.waitForURL(crossURL, { timeout: 2000, waitUntil: 'load' }),
    page.locator('#right').evaluate((node, url) => node.setAttribute('src', url), crossURL),
  ])
  await page.frameLocator('#right').getByText('Actual child').waitFor({ timeout: 2000 })
  const cross = page
    .frames()
    .find((frame) => frame.url() === `http://localhost:${address.port}/child`)
  assert(cross)
  const crossBody = (await cross.evaluateHandle(() => document.body)).asElement()
  assert(crossBody)
  handles.push(crossBody)
  assert.equal(await crossBody.ownerFrame(), cross)
  assert(
    z
      .object({ sessions: z.number().positive() })
      .parse(await page.source.runAutomation('diagnostics')),
  )
  checks.push(
    'actual cross-process child ownership compares native backend identity only within its genuine V8 isolate and session',
  )
  const popupPromise = page.waitForEvent('popup')
  await page.evaluate((url) => {
    Reflect.set(globalThis, 'actualOwnerPopup', window.open(url))
  }, base + '/popup')
  const popup = await popupPromise
  await popup.waitForLoadState('domcontentloaded')
  await page.evaluate(
    "globalThis.actualOwnerPopup.document.body.appendChild(document.querySelector('#target'));undefined",
  )
  assert.equal(await target.ownerFrame(), popup.mainFrame())
  assert.equal(
    await target.evaluate((node) => {
      if (!(node instanceof Element)) throw new Error('Actual adopted element missing')
      return node.ownerDocument.URL
    }),
    popup.url(),
  )
  checks.push(
    'genuine DOM adoption into a same-origin popup returns that canonical popup frame without changing the retained original node',
  )
  for (const handle of handles.splice(0)) await handle.dispose()
  const started = new AbortController(),
    began = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const bridge = await page.exposeFunction('actualOwnerHoldStarted', () => started.abort())
  const abort = new AbortController(),
    projection = createNativePage(page.source, abort.signal)
  const projected = await projection.$('iframe')
  assert(projected)
  const busy = page.evaluate(
    '(()=>{globalThis.actualOwnerHoldStarted();const until=Date.now()+600;while(Date.now()<until){};})()',
  )
  await began
  const pending = projected.ownerFrame(),
    rejected = assert.rejects(pending, /Actual owner document cancellation/)
  for (let attempt = 0; attempt < 100; attempt++) {
    const state = schema.parse(await page.source.runAutomation('diagnostics'))
    if (state.nativeSelectorWaitPhysical) break
    await delay(1)
  }
  abort.abort(new Error('Actual owner document cancellation'))
  await rejected
  const afterAbort = schema.parse(await page.source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  await busy
  await page.source.runAutomation('handle', { action: 'dispose', handle: projected.id })
  await bridge.dispose()
  checks.push(
    'logical cancellation returns promptly while a genuine renderer-held preparation remains physically admitted until its actual ACK; the retained handle is cleaned through its live physical owner',
  )
  for (let attempt = 0; attempt < 200; attempt++) {
    const state = schema.parse(await page.source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const state = schema.parse(await page.source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  await context.close()
  checks.push(
    'all actual handles, private peer documents, object groups and process-wide physical peer ownership retire after success, no-owner result, cross-page adoption and cancellation',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ checks, state, afterAbort }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native owner document diagnostics must remain empty')
