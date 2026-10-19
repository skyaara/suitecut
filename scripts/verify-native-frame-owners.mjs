// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = '.suitecut/native-frame-owners'
await mkdir(directory, { recursive: true })
let stderr = ''
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  if (request.url === '/child')
    response.end(
      '<input placeholder="Account"><img alt="Actual image"><b title="Actual title">Title</b><section id="html"><i>Native markup</i></section>',
    )
  else
    response.end(
      '<iframe id="left" name="duplicate" src="/child"></iframe><iframe id="right" name="duplicate" src="/child"></iframe>',
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
const checks = []
try {
  const page = createNativePage(source)
  await page.goto(base)
  await page.frameLocator('#left').getByPlaceholder('Account').waitFor({ timeout: 2000 })
  const frames = page.frames().filter((frame) => frame.parentFrame())
  assert.equal(frames.length, 2)
  const owners = []
  let rightId = ''
  for (const frame of frames) {
    const owner = await frame.frameElement()
    const id = await owner.getAttribute('id')
    owners.push(id)
    if (id === 'right') rightId = frame.identity() ?? ''
    assert.equal(await owner.evaluate((node, id) => node === document.getElementById(id), id), true)
    await owner.dispose()
  }
  assert.deepEqual(owners.sort(), ['left', 'right'])
  checks.push(
    'duplicate names and URLs resolve to genuine exact owning backend nodes in the actual parent frame',
  )
  const frameLocator = page.frameLocator('#right')
  assert.equal(await frameLocator.getByPlaceholder(/count$/).count(), 1)
  assert.equal(await frameLocator.getByAltText('Actual image', { exact: true }).count(), 1)
  assert.equal(await frameLocator.getByTitle('actual title').count(), 1)
  assert.equal(await frameLocator.locator('#html').innerHTML(), '<i>Native markup</i>')
  checks.push(
    'frame locator attribute query APIs and locator innerHTML retain the actual native frame scope',
  )
  const right = frames.find((frame) => frame.identity() === rightId)
  assert(right)
  const rightOwner = await right.frameElement()
  await rightOwner.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual owner missing')
    node.setAttribute('data-exact', 'owner\ud800\0\\"\udfff😀')
  })
  assert.equal(await rightOwner.getAttribute('data-exact'), 'owner\ud800\0\\"\udfff😀')
  await rightOwner.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual owner missing')
    const clone = node.cloneNode(true)
    if (!(clone instanceof Element)) throw new Error('Actual clone missing')
    clone.id = 'replacement'
    node.replaceWith(clone)
  })
  assert.equal(
    await rightOwner.evaluate((node) => node instanceof Element && node.isConnected),
    false,
  )
  assert.equal(await rightOwner.getAttribute('id'), 'right')
  await rightOwner.dispose()
  await assert.rejects(right.frameElement(), /detached/i)
  checks.push(
    'returned owner handles retain exact UTF16 attributes and the original detached node after genuine DOM replacement',
  )
  await page
    .frameLocator('#left')
    .locator('body')
    .evaluate((node) => {
      const child = document.createElement('iframe')
      child.id = 'nested'
      child.srcdoc = '<p>Nested child</p>'
      node.appendChild(child)
    })
  await page
    .frameLocator('#left')
    .frameLocator('#nested')
    .getByText('Nested child')
    .waitFor({ timeout: 2000 })
  const nested = page.frames().find((frame) => frame.parentFrame()?.parentFrame())
  assert(nested)
  const nestedOwner = await nested.frameElement()
  assert.equal(
    await nestedOwner.evaluate((node) => node === document.querySelector('#nested')),
    true,
  )
  await nestedOwner.dispose()
  checks.push(
    'nested owners deliver genuine retained handles bound to the immediate parent execution context',
  )
  await page.locator('#left').evaluate((node, url) => {
    if (!(node instanceof Element)) throw new Error('Actual iframe missing')
    node.setAttribute('src', url)
  }, `http://localhost:${address.port}/child`)
  await page.frameLocator('#left').getByPlaceholder('Account').waitFor({ timeout: 2000 })
  const cross = page
    .frames()
    .find((frame) => frame.url().startsWith(`http://localhost:${address.port}/child`))
  assert(cross)
  const nativeSession = z
    .object({ sessions: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert(nativeSession.sessions > 0)
  const crossOwner = await cross.frameElement()
  assert.equal(await crossOwner.evaluate((node) => node === document.querySelector('#left')), true)
  await crossOwner.dispose()
  checks.push(
    'actual cross-origin OOPIF ownership is resolved in its parent session with genuine parent-world node adoption',
  )
  await assert.rejects(page.mainFrame().frameElement(), /main frame|detached/i)
  await assert.rejects(
    source.runAutomation('handle', {
      action: 'frameElement',
      nativeFrameOwnerChild: 'not-an-authorized-child',
    }),
    /internal field/,
  )
  checks.push(
    'main-frame and public internal-field injection fail explicitly without fabricated owner handles',
  )
  const began = new AbortController(),
    projection = createNativePage(source, began.signal)
  const projected = projection.frames().find((frame) => frame.identity() === cross.identity())
  assert(projected)
  const holdStarted = new AbortController()
  const started = new Promise((resolve) =>
    holdStarted.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
  )
  const bridge = await page.exposeFunction('frameOwnerHoldStarted', () => holdStarted.abort())
  const busy = page.evaluate(
    '(()=>{globalThis.frameOwnerHoldStarted();const end=Date.now()+700;while(Date.now()<end){};})()',
  )
  await started
  const pending = projected.frameElement()
  const rejected = assert.rejects(pending, /Actual owner cancellation/)
  let held
  for (let attempt = 0; attempt < 100; attempt++) {
    held = z
      .object({ nativeSelectorWaitPhysical: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (held.nativeSelectorWaitPhysical) break
    await delay(1)
  }
  assert(held && held.nativeSelectorWaitPhysical > 0)
  began.abort(new Error('Actual owner cancellation'))
  await rejected
  const afterAbort = z
    .object({
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      activeHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  assert.equal(afterAbort.activeHandles, 0)
  await busy
  await bridge.dispose()
  for (let attempt = 0; attempt < 200; attempt++) {
    const state = z
      .object({ nativeSelectorWaits: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (!state.nativeSelectorWaits) break
    await delay(5)
  }
  checks.push(
    'caller cancellation preserves bounded actual pending DevTools/group ownership through renderer-held ACKs and delivers no late native owner handle',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      nativeHandleDisposals: z.number(),
      nativeHandleDisposalPhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  checks.push(
    'actual successful, detached, rejected and canceled owner lifetimes retire all tracked native handles and physical ownership',
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
