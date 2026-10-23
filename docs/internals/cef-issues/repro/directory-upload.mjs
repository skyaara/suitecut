// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Standalone upstream CDP diagnostic intentionally has no SDK/schema dependencies; protocol replies are printed as evidence, not consumed by production code. */
// Node >=22. No packages, SuiteCut SDK, or Playwright are required.
// Run against a dedicated disposable CEF page with remote debugging enabled.
import console from 'node:console'
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, basename } from 'node:path'
import process from 'node:process'
import { setTimeout, clearTimeout } from 'node:timers'

const endpoint = process.argv[2] ?? 'http://127.0.0.1:9222'
const pages = await (await globalThis.fetch(`${endpoint}/json/list`)).json()
const target = pages.find((page) => page.type === 'page' && page.webSocketDebuggerUrl)
if (!target) throw new Error('No debuggable page. Start a dedicated CEF sample page first.')
const socket = new WebSocket(target.webSocketDebuggerUrl)
await new Promise((resolve, reject) => {
  const timer = setTimeout(() => reject(new Error('CDP connect timed out')), 5000)
  socket.addEventListener(
    'open',
    () => {
      clearTimeout(timer)
      resolve()
    },
    { once: true },
  )
  socket.addEventListener(
    'error',
    () => {
      clearTimeout(timer)
      reject(new Error('CDP connect failed'))
    },
    { once: true },
  )
})
let next = 0
const pending = new Map()
socket.addEventListener('message', ({ data }) => {
  const reply = JSON.parse(String(data))
  const item = pending.get(reply.id)
  if (!item) return
  pending.delete(reply.id)
  clearTimeout(item.timer)
  if (reply.error) item.reject(new Error(JSON.stringify(reply.error)))
  else item.resolve(reply.result)
})
function send(method, params = {}) {
  const id = ++next
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id)
      reject(new Error(`${method} timed out`))
    }, 5000)
    pending.set(id, { resolve, reject, timer })
    socket.send(JSON.stringify({ id, method, params }))
  })
}
const directory = await mkdtemp(join(tmpdir(), 'cef-folder-'))
try {
  await mkdir(join(directory, 'nested'))
  await writeFile(join(directory, 'first.txt'), 'first content')
  await writeFile(join(directory, 'nested', 'second.txt'), 'nested content')
  const results = []
  for (const folder of [false, true]) {
    await send('Runtime.evaluate', {
      expression: `document.body.innerHTML = '<input id="upload" type="file" ${folder ? 'webkitdirectory' : ''}>'`,
    })
    const { root } = await send('DOM.getDocument')
    const { nodeId } = await send('DOM.querySelector', { nodeId: root.nodeId, selector: '#upload' })
    const protocolResult = await send('DOM.setFileInputFiles', {
      nodeId,
      files: [folder ? directory : join(directory, 'first.txt')],
    })
    // Await actual browser-side enumeration before inspecting the original input.
    const observed = await send('Runtime.evaluate', {
      expression: `new Promise(resolve => setTimeout(async () => resolve(await Promise.all(Array.from(document.querySelector('#upload').files, async f => ({ name: f.name, relativePath: f.webkitRelativePath, text: await f.text() })))), 1000))`,
      awaitPromise: true,
      returnByValue: true,
    })
    if (observed.exceptionDetails) throw new Error(JSON.stringify(observed.exceptionDetails))
    results.push({ folder, protocolResult, files: observed.result.value })
  }
  const expected = [
    { name: 'first.txt', relativePath: `${basename(directory)}/first.txt`, text: 'first content' },
    {
      name: 'second.txt',
      relativePath: `${basename(directory)}/nested/second.txt`,
      text: 'nested content',
    },
  ]
  console.log(
    JSON.stringify(
      { target: target.id, version: await send('Browser.getVersion'), expected, results },
      null,
      2,
    ),
  )
  const regular = results[0].files
  const folder = results[1].files
  if (regular.length !== 1 || regular[0].text !== 'first content')
    throw new Error('Regular-file control failed')
  if (
    folder.length !== expected.length ||
    !expected.every((e) => folder.some((f) => JSON.stringify(f) === JSON.stringify(e)))
  ) {
    process.exitCode = 1
  }
} finally {
  socket.close()
  for (const item of pending.values()) {
    clearTimeout(item.timer)
    item.reject(new Error('Probe closed'))
  }
  pending.clear()
  await rm(directory, { recursive: true, force: true })
}
