#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Local application journeys through public SDK APIs. No videos or external writes.
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument, @typescript-eslint/no-misused-promises -- Exercises dynamically selected emitted SDKs and local HTTP/WebSocket fixture handlers. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout } from 'node:timers'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2)
  options.set(process.argv[index], process.argv[index + 1])
const output = resolve(
  options.get('--output') ?? '.suitecut/real-world-review/collaboration/results.json',
)
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const sdk = await import(pathToFileURL(sdkPath).href)
let WebSocketServer
try {
  const packageName = 'ws'
  ;({ WebSocketServer } = await import(packageName))
} catch {
  const dependency = (await readdir('node_modules/.pnpm')).find((entry) => entry.startsWith('ws@'))
  if (!dependency)
    throw new Error('The existing ws development dependency is required for this scenario.')
  ;({ WebSocketServer } = await import(
    pathToFileURL(resolve('node_modules/.pnpm', dependency, 'node_modules/ws/wrapper.mjs')).href
  ))
}
const states = new Map()
const state = (room) => {
  if (!states.has(room))
    states.set(room, {
      saved: '',
      revision: 0,
      socketMessages: [],
      pointerDrops: [],
      offline: false,
      handbookNetworkRequests: 0,
    })
  return states.get(room)
}
const payload = async (request) => {
  const chunks = []
  for await (const chunk of request) chunks.push(chunk)
  return Buffer.concat(chunks).toString('utf8')
}
const html = (room) => `<!doctype html><html><meta charset="utf-8"><title>Team workspace</title>
<style>body{font:16px sans-serif;margin:24px}button{margin:8px;padding:8px}#editor{border:1px solid #777;min-height:70px;padding:12px}#board{display:flex;gap:160px;margin-top:24px}#card,#done{width:150px;height:80px;padding:12px;border:2px solid #678;user-select:none;touch-action:none}#done{background:#eaf5ea}output{display:block;margin:10px}</style>
<h1>Release collaboration workspace</h1><div id="editor" role="textbox" aria-label="Document editor" contenteditable="true"></div>
<button id="save">Save document</button><output id="save-status">Not saved</output><output id="socket-status">Connecting</output><output id="peer" aria-live="polite">No peer updates</output>
<div id="board"><div id="card">Release checklist card</div><div id="done">Done lane</div></div><output id="drag-status">Card pending</output>
<button id="prepare-cache">Prepare offline handbook</button><button id="outage">Simulate handbook outage</button><button id="read-cache">Read handbook</button><output id="cache-status">Cache not prepared</output><output id="handbook"></output>
<script>
const room=${JSON.stringify(room)},editor=document.querySelector('#editor'),client=crypto.randomUUID();
fetch('/document?room='+room).then(r=>r.json()).then(doc=>{editor.textContent=doc.text;document.querySelector('#save-status').textContent='Loaded revision '+doc.revision});
const socket=new WebSocket('ws://'+location.host+'/socket?room='+room);
socket.onopen=()=>document.querySelector('#socket-status').textContent='Connected';
socket.onmessage=event=>{const message=JSON.parse(event.data);if(message.client!==client)document.querySelector('#peer').textContent=message.text};
editor.addEventListener('input',()=>{if(socket.readyState===1)socket.send(JSON.stringify({client,text:editor.innerText}))});
document.querySelector('#save').onclick=async()=>{const response=await fetch('/document?room='+room,{method:'POST',body:editor.innerText});const result=await response.json();document.querySelector('#save-status').textContent='Saved revision '+result.revision};
let dragging=false,moves=0,trusted=true;const card=document.querySelector('#card');
card.addEventListener('pointerdown',event=>{dragging=true;moves=0;trusted=event.isTrusted;card.setPointerCapture(event.pointerId)});
card.addEventListener('pointermove',event=>{if(dragging){moves++;trusted&&=event.isTrusted}});
card.addEventListener('pointerup',async event=>{if(!dragging)return;dragging=false;trusted&&=event.isTrusted;const box=document.querySelector('#done').getBoundingClientRect();if(event.clientX>=box.left&&event.clientX<=box.right&&event.clientY>=box.top&&event.clientY<=box.bottom){document.querySelector('#drag-status').textContent='Card completed';await fetch('/drop?room='+room,{method:'POST',body:JSON.stringify({moves,trusted})})}});
document.querySelector('#prepare-cache').onclick=async()=>{try{await navigator.serviceWorker.register('/sw.js?room='+room);await navigator.serviceWorker.ready;if(!navigator.serviceWorker.controller)await new Promise(resolve=>navigator.serviceWorker.addEventListener('controllerchange',resolve,{once:true}));document.querySelector('#cache-status').textContent='Handbook cached and controlled'}catch(error){document.querySelector('#cache-status').textContent='Unavailable: '+error.message}};
document.querySelector('#outage').onclick=async()=>{await fetch('/outage?room='+room,{method:'POST'});document.querySelector('#cache-status').textContent='Handbook origin unavailable'};
document.querySelector('#read-cache').onclick=async()=>{const response=await fetch('/cached-handbook?room='+room);document.querySelector('#handbook').textContent=await response.text()};
</script></html>`
const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://127.0.0.1'),
      room = url.searchParams.get('room') ?? 'default',
      current = state(room)
    response.setHeader('cache-control', 'no-store')
    if (url.pathname === '/document') {
      if (request.method === 'POST') {
        current.saved = await payload(request)
        current.revision++
      }
      response.setHeader('content-type', 'application/json')
      response.end(JSON.stringify({ text: current.saved, revision: current.revision }))
      return
    }
    if (url.pathname === '/drop') {
      current.pointerDrops.push(JSON.parse(await payload(request)))
      response.end('ok')
      return
    }
    if (url.pathname === '/outage') {
      current.offline = true
      response.end('ok')
      return
    }
    if (url.pathname === '/cached-handbook') {
      current.handbookNetworkRequests++
      response.writeHead(current.offline ? 503 : 200, {
        'content-type': 'text/plain; charset=utf-8',
      })
      response.end(current.offline ? 'Origin unavailable' : 'Offline playbook: ship café ✅')
      return
    }
    if (url.pathname === '/sw.js') {
      response.setHeader('content-type', 'application/javascript')
      response.end(
        `const resource=${JSON.stringify('/cached-handbook?room=' + room)},cacheName=${JSON.stringify('collaboration-' + room)};self.addEventListener('install',event=>event.waitUntil(caches.open(cacheName).then(cache=>cache.add(resource)).then(()=>self.skipWaiting())));self.addEventListener('activate',event=>event.waitUntil(self.clients.claim()));self.addEventListener('fetch',event=>{if(new URL(event.request.url).pathname==='/cached-handbook')event.respondWith(caches.open(cacheName).then(cache=>cache.match(event.request)).then(cached=>cached||fetch(event.request)))});`,
      )
      return
    }
    response.setHeader('content-type', 'text/html; charset=utf-8')
    response.end(html(room))
  } catch (error) {
    response.writeHead(500)
    response.end(String(error))
  }
})
const sockets = new WebSocketServer({ server, path: '/socket' })
sockets.on('connection', (socket, request) => {
  const room = new URL(request.url, 'http://127.0.0.1').searchParams.get('room')
  socket.room = room
  socket.on('message', (bytes) => {
    const message = JSON.parse(bytes.toString())
    state(room).socketMessages.push(message)
    for (const peer of sockets.clients)
      if (peer.room === room && peer.readyState === 1) peer.send(JSON.stringify(message))
  })
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${server.address().port}`
const until = async (operation, expected, timeout = 5000) => {
  const deadline = Date.now() + timeout
  let actual
  do {
    actual = await operation()
    if (actual === expected) return actual
    await new Promise((resolve) => setTimeout(resolve, 25))
  } while (Date.now() < deadline)
  assert.equal(actual, expected)
}
const results = []
try {
  for (const backend of options.get('--backend') ? [options.get('--backend')] : ['cef', 'cdp']) {
    const room = `${backend}-${Date.now()}`
    const started = Date.now()
    let browser, page, peer
    const record = { backend, journeys: [] }
    try {
      browser = await sdk.launchBrowser(
        backend === 'cef'
          ? {
              backend,
              executablePath: resolve(
                options.get('--cef-executable') ?? '.suitecut/memory-deep/runtime/suitecut-browser',
              ),
              headless: true,
              width: 1000,
              height: 760,
              startupTimeoutMs: 20000,
            }
          : {
              backend,
              executablePath: resolve(
                options.get('--chrome-executable') ??
                  '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
              ),
              args: ['--no-sandbox', '--disable-dev-shm-usage'],
              viewport: { width: 1000, height: 760 },
              timeoutMs: 12000,
            },
      )
      page = await browser.newPage()
      await page.goto(`${origin}/workspace?room=${room}`)
      const run = async (name, action) => {
        const begin = Date.now()
        try {
          record.journeys.push({
            name,
            status: 'passed',
            milliseconds: Date.now() - begin,
            evidence: await action(),
          })
          record.journeys.at(-1).milliseconds = Date.now() - begin
        } catch (error) {
          record.journeys.push({
            name,
            status: 'failed',
            milliseconds: Date.now() - begin,
            error: String(error?.stack ?? error),
          })
        }
      }
      await run('rich-editor-unicode-save-reload', async () => {
        await until(() => page.locator('#save-status').textContent(), 'Loaded revision 0')
        await page
          .getByRole('textbox', { name: 'Document editor', exact: true })
          .fill('Sprint memo — café 👩🏽‍💻 東京')
        await page.keyboard.press('End')
        await page.keyboard.insertText(' — reviewed ✅')
        await page.getByRole('button', { name: 'Save document', exact: true }).click()
        await until(() => page.locator('#save-status').textContent(), 'Saved revision 1')
        const saved = state(room).saved
        assert.equal(saved, 'Sprint memo — café 👩🏽‍💻 東京 — reviewed ✅')
        await page.goto(`${origin}/workspace?room=${room}`)
        await until(() => page.locator('#save-status').textContent(), 'Loaded revision 1')
        assert.equal(await page.locator('#editor').innerText(), saved)
        return { saved, revision: state(room).revision, persistedAfterReload: true }
      })
      await run('two-tab-websocket-collaboration', async () => {
        peer = await browser.newPage()
        await peer.goto(`${origin}/workspace?room=${room}`)
        await until(() => peer.locator('#socket-status').textContent(), 'Connected')
        await until(() => page.locator('#socket-status').textContent(), 'Connected')
        const first = 'Release owner: 東京 🚀',
          second = 'Reviewer: Zürich ✅'
        await page.getByRole('textbox', { name: 'Document editor', exact: true }).fill(first)
        await until(() => peer.locator('#peer').textContent(), first)
        await peer.getByRole('textbox', { name: 'Document editor', exact: true }).fill(second)
        await until(() => page.locator('#peer').textContent(), second)
        assert.ok(state(room).socketMessages.some((message) => message.text === first))
        assert.ok(state(room).socketMessages.some((message) => message.text === second))
        return {
          messagesObservedByServer: state(room).socketMessages.length,
          bidirectionalPeerUpdates: true,
        }
      })
      await run('trusted-pointer-kanban-drag', async () => {
        await page.locator('#card').dragTo(page.locator('#done'), { steps: 12 })
        await until(() => page.locator('#drag-status').textContent(), 'Card completed')
        const drop = state(room).pointerDrops.at(-1)
        assert.ok(drop?.trusted)
        assert.ok(drop.moves >= 2)
        return drop
      })
      await run('service-worker-cached-handbook-outage', async () => {
        await page.locator('#prepare-cache').click()
        await until(
          () => page.locator('#cache-status').textContent(),
          'Handbook cached and controlled',
          7000,
        )
        const requestsBeforeOutage = state(room).handbookNetworkRequests
        await page.locator('#outage').click()
        await until(
          () => page.locator('#cache-status').textContent(),
          'Handbook origin unavailable',
        )
        await page.locator('#read-cache').click()
        await until(() => page.locator('#handbook').textContent(), 'Offline playbook: ship café ✅')
        assert.ok(state(room).offline)
        assert.equal(state(room).handbookNetworkRequests, requestsBeforeOutage)
        return {
          serverOriginUnavailable: true,
          cachedContentCorrect: true,
          requestsBeforeOutage,
          requestsAfterRead: state(room).handbookNetworkRequests,
        }
      })
    } catch (error) {
      record.error = String(error?.stack ?? error)
    } finally {
      try {
        await browser?.close()
      } catch (error) {
        record.closeError = String(error)
      }
    }
    record.milliseconds = Date.now() - started
    record.serverEvidence = state(room)
    results.push(record)
    console.log(JSON.stringify(record))
  }
} finally {
  for (const socket of sockets.clients) socket.terminate()
  await new Promise((resolve) => sockets.close(resolve))
  await new Promise((resolve) => server.close(resolve))
  await mkdir(resolve(output, '..'), { recursive: true })
  await writeFile(
    output,
    JSON.stringify(
      { timestamp: new Date().toISOString(), sdkPath, localOnly: true, videos: false, results },
      null,
      2,
    ) + '\n',
  )
}
if (
  results.some((result) =>
    [
      result.error,
      result.closeError,
      result.journeys.some((journey) => journey.status !== 'passed'),
    ].some(Boolean),
  )
)
  process.exitCode = 1
