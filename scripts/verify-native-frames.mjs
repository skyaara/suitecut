// SPDX-License-Identifier: MIT
/* global document, location */
import assert from 'node:assert/strict'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

/**
 * Read an adopted DOM node's text in the destination renderer world.
 * @param {unknown} value
 */
function nodeText(value) {
  if (
    !value ||
    typeof value !== 'object' ||
    !('nodeType' in value) ||
    value.nodeType !== 1 ||
    !('textContent' in value) ||
    (value.textContent !== null && typeof value.textContent !== 'string')
  )
    throw new Error('Expected an adopted DOM element')
  return value.textContent
}

const directory = resolve('.suitecut/native-frames')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    request.url === '/child'
      ? '<p>child document</p><button onclick="document.body.dataset.clicked=String(Number(document.body.dataset.clicked||0)+1)">click</button><input>'
      : '<p>main document</p>',
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
let nativeDiagnostics = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic: (message) => {
    if (process.env.SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS) console.error(message)
    if (nativeDiagnostics.length < 1024 * 1024)
      nativeDiagnostics += message.slice(0, 1024 * 1024 - nativeDiagnostics.length)
  },
}).catch((error) => {
  server.close()
  throw error
})
try {
  const page = createNativePage(source)
  const navigations = z.array(z.object({ name: z.string(), url: z.string() })).parse([])
  /**
   * Record the frame's current navigation metadata.
   * @param {import('../dist/native.js').NativeFrame} frame
   */
  const listener = (frame) => {
    navigations.push({ name: frame.name(), url: frame.url() })
  }
  page.on('framenavigated', listener)
  const prefix = `http://127.0.0.1:${address.port}`
  await page.goto(prefix)
  assert.equal(
    await page.mainFrame().evaluate(() => document.querySelector('p').textContent),
    'main document',
  )
  const attached = page.waitForEvent('frameattached')
  await page.evaluate(
    (url) =>
      new Promise((resolve, reject) => {
        const frame = document.createElement('iframe')
        frame.name = frame.id = 'child'
        frame.src = url
        frame.onload = () => resolve()
        frame.onerror = reject
        document.body.append(frame)
      }),
    prefix + '/child',
  )
  await attached
  assert.equal(page.frames().length, 2)
  const child = page.frame({ name: 'child' })
  assert(child)
  assert.equal(child.parentFrame(), page.mainFrame())
  assert.deepEqual(page.mainFrame().childFrames(), [child])
  assert.equal(
    await child.evaluate(() => document.querySelector('p').textContent),
    'child document',
  )
  await page.frameLocator('iframe').locator('button').click()
  assert.equal(await child.evaluate(() => document.body.dataset.clicked), '1')
  await page.frameLocator('iframe').locator('input').fill('same-process input')
  assert.equal(
    await child.evaluate(() => document.querySelector('input').value),
    'same-process input',
  )
  const handle = await child.evaluateHandle(() => ({ value: 42 }))
  assert.equal(
    await child.evaluate(
      ({ handle }) => {
        if (!handle || typeof handle !== 'object' || !('value' in handle))
          throw new Error('Missing remote handle value')
        return handle.value
      },
      { handle },
    ),
    42,
  )
  await assert.rejects(
    page.evaluate((handle) => handle, handle),
    /different frame/,
  )
  await handle.dispose()
  const childElement = await child.$('p')
  assert(childElement)
  assert.equal(await page.evaluate(nodeText, childElement), 'child document')
  const adoptedHandle = await page.evaluateHandle((element) => element, childElement)
  assert.equal(await adoptedHandle.evaluate(nodeText), 'child document')
  await adoptedHandle.dispose()
  await childElement.dispose()
  const pending = child.evaluate(() => new Promise(() => undefined))
  const detachedWork = assert.rejects(pending, /detach|context|navigation/)
  // An evaluation in the same frame confirms the pending operation was dispatched.
  assert.equal(await child.evaluate(() => 42), 42)
  const detached = page.waitForEvent('framedetached')
  await page.evaluate(() => document.querySelector('iframe').remove())
  assert.equal(await detached, child)
  await detachedWork
  assert(child.isDetached())
  assert.equal(child.parentFrame(), null)
  await assert.rejects(
    child.evaluate(() => 42),
    /detach/,
  )
  assert.equal(page.frames().length, 1)
  assert(navigations.some((frame) => frame.name === 'child' && frame.url === prefix + '/child'))
  const crossProcessUrl = prefix.replace('127.0.0.1', 'localhost') + '/child'
  console.log('Verifying cross-process child:', crossProcessUrl)
  await page.evaluate(
    (url) =>
      new Promise((resolve) => {
        const frame = document.createElement('iframe')
        frame.name = frame.id = 'cross-process'
        frame.src = url
        frame.onload = resolve
        document.body.append(frame)
      }),
    crossProcessUrl,
  )
  const crossProcess = page.frame('cross-process')
  console.log('Cross-process document loaded:', source.frameInfos())
  assert(crossProcess)
  assert.equal(crossProcess.parentFrame(), page.mainFrame())
  assert.equal(await crossProcess.evaluate(() => location.href), crossProcessUrl)
  assert.equal(
    await crossProcess.evaluate(() => document.querySelector('p').textContent),
    'child document',
  )
  await page.frameLocator('iframe').locator('button').click()
  assert.equal(await crossProcess.evaluate(() => document.body.dataset.clicked), '1')
  await page.frameLocator('iframe').locator('input').fill('cross-process input')
  assert.equal(
    await crossProcess.evaluate(() => document.querySelector('input').value),
    'cross-process input',
  )
  const crossHandle = await crossProcess.evaluateHandle(() => ({ answer: 42 }))
  assert.deepEqual(await crossHandle.jsonValue(), { answer: 42 })
  await crossHandle.dispose()
  const crossElement = await crossProcess.$('p')
  assert(crossElement)
  await assert.rejects(
    page.evaluate(nodeText, crossElement),
    /Unable to adopt element handle from a different document/,
  )
  await crossElement.dispose()
  console.log('Cross-process evaluation and handles passed; verifying a process swap')
  await page.evaluate(
    (url) =>
      new Promise((resolve) => {
        const frame = document.querySelector('iframe')
        frame.onload = resolve
        frame.src = url
      }),
    prefix + '/child',
  )
  assert.equal(page.frame('cross-process'), crossProcess)
  assert.equal(await crossProcess.evaluate(() => location.href), prefix + '/child')
  const detachedByNavigation = page.waitForEvent('framedetached')
  await page.goto(prefix)
  assert.equal(await detachedByNavigation, crossProcess)
  assert(crossProcess.isDetached())
  assert.equal(page.frames().length, 1)
  const sameDocument = page.waitForEvent('framenavigated')
  await page.goto(prefix + '/#anchor')
  assert.equal(await sameDocument, page.mainFrame())
  assert.equal(page.url(), prefix + '/#anchor')
  page.off('framenavigated', listener)
  const report = {
    cefVersion: source.cefVersion,
    platform: process.platform,
    mainFrame: 'passed',
    sameProcessChild: 'passed',
    frameEvents: 'passed',
    frameScopedHandles: 'passed',
    crossFrameHandleRejection: 'passed',
    sameOriginElementAdoption: 'passed',
    crossOriginElementRejection: 'passed',
    detachCancellation: 'passed',
    navigations,
    crossProcessChild: 'passed',
    processSwapIdentity: 'passed',
    parentNavigationDetach: 'passed',
    sameDocumentNavigation: 'passed',
    sameProcessFrameInput: 'passed',
    crossProcessFrameInput: 'passed',
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, nativeDiagnostics)
  server.closeAllConnections()
  server.close()
}
