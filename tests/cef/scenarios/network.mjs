// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { digest } from '../fixtures/app.mjs'
import { inventory } from '../fixtures/app.mjs'
import { test } from './helpers.mjs'
import { wait } from './helpers.mjs'
import { login } from './helpers.mjs'

export const network = [
  test('network.binary-response', ['journey', 'network'], async (h) => {
    const { page, context } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const pending = page.waitForResponse(h.fixture.origin + '/api/binary', { timeout: 8000 })
    await page.evaluate(
      `void fetch('/api/binary').then(r=>r.arrayBuffer()).then(b=>globalThis.receivedBytes=b.byteLength)`,
    )
    const response = await pending
    assert.equal(digest(await response.body()), digest(inventory))
    const api = await context.request.get(h.fixture.origin + '/api/binary')
    assert.equal(digest(await api.body()), digest(inventory))
    await api.dispose()
    await wait(page, `globalThis.receivedBytes===${inventory.length}`)
    h.check('Page network and API body transports preserve the same non-ASCII binary bytes')
  }),
  test('network.offline-recovery', ['journey', 'network'], async (h) => {
    const { page, context } = await h.page()
    await login(h, page)
    await context.setOffline(true)
    assert.equal(await page.evaluate(`fetch('/api/session').then(()=>false,()=>true)`), true)
    await context.setOffline(false)
    assert.equal(
      await page.evaluate(`fetch('/api/session').then(r=>r.json()).then(v=>v.email)`),
      'operator@example.test',
    )
    h.check('Offline failure and network restoration retain the authenticated context')
  }),
  test('worker.service-worker-offline', ['journey', 'workers', 'network'], async (h) => {
    const { page, context } = await h.page()
    await page.goto(h.fixture.origin + '/worker-app')
    await page.evaluate(
      `(async()=>{await navigator.serviceWorker.register('/sw.js');await navigator.serviceWorker.ready;if(!navigator.serviceWorker.controller)await new Promise(r=>navigator.serviceWorker.addEventListener('controllerchange',r,{once:true}));return true})()`,
    )
    assert.equal(await page.evaluate(`fetch('/cached').then(r=>r.text())`), 'offline-cached-v1')
    await context.setOffline(true)
    assert.equal(await page.evaluate(`fetch('/cached').then(r=>r.text())`), 'offline-cached-v1')
    await context.setOffline(false)
    await page.evaluate(
      `navigator.serviceWorker.getRegistrations().then(rs=>Promise.all(rs.map(r=>r.unregister())))`,
    )
    h.check(
      'Real service-worker install/claim/interception serves cached content with the network offline',
    )
  }),
  test('worker.replacement-generations', ['race', 'workers'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/workers')
    const result = await page.evaluate(
      `(async()=>{let stale=0;let old=new Worker('/worker.js');old.onmessage=()=>stale++;old.postMessage('hold');old.terminate();const next=new Worker('/worker.js');const actual=await new Promise(r=>{next.onmessage=e=>r(e.data);next.postMessage({generation:${h.seed},value:21})});next.terminate();return {actual,stale}})()`,
    )
    assert.deepEqual(result, { actual: { generation: h.seed, value: 42 }, stale: 0 })
    h.check(
      'Terminating a worker and creating a same-URL replacement preserves generation-specific messages',
    )
  }),
]
