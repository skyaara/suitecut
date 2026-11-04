// SPDX-License-Identifier: MIT
// Volumes real sites produce, reproduced locally. Each one used to close the
// page or the browser through suitecut's own bookkeeping while Chrome loaded
// the same page fine (see examples/real-sites/soak.ts). Chrome and Playwright
// deliver all of it; so must suitecut, or at least keep the page alive.
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { readFile } from 'node:fs/promises'
import { join } from 'node:path'

import { test } from './helpers.mjs'

const alive = async (page) => {
  assert.equal(page.isClosed(), false, 'page closed')
  assert.equal(await page.evaluate('1+1'), 2)
}
const degraded = async (h, page) => {
  const diagnostics = await page.source.runAutomation('diagnostics')
  h.observe('degraded', {
    page: diagnostics.degraded ?? {},
    engine: diagnostics.engineDegraded ?? {},
  })
}

export const resilience = [
  test(
    'resilience.request-volume',
    ['resilience', 'network'],
    async (h) => {
      const { page } = await h.page()
      page.setDefaultTimeout(120000)
      await page.goto(h.fixture.origin + '/app')
      const count = 9000
      let finished = 0
      page.on('requestfinished', () => ++finished)
      const last = page.waitForResponse(
        (r) => r.url().endsWith(`/resilience/item?n=${count - 1}`),
        {
          timeout: 120000,
        },
      )
      await page.evaluate(
        `(async()=>{for(let i=0;i<${count};i+=200)await Promise.all(Array.from({length:Math.min(200,${count}-i)},(_,j)=>fetch('/resilience/item?n='+(i+j)).then(r=>r.json())))})()`,
      )
      assert.deepEqual(await (await last).json(), { item: count - 1 })
      await h.until(() => finished >= count, 'not every request finished', 30000)
      await alive(page)
      await degraded(h, page)
      h.check(
        '9,000 requests, more than the request tables hold, all finish and the page stays open',
      )
    },
    240,
  ),
  test('resilience.page-error-burst', ['resilience', 'errors'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const errors = []
    page.on('pageerror', (error) => errors.push(error.message))
    await page.evaluate(`for(let i=0;i<300;i++)setTimeout(()=>{throw new Error('burst '+i)})`)
    await h.until(
      () => errors.length >= 300,
      `only ${errors.length} of 300 page errors arrived`,
      60000,
    )
    assert.deepEqual(
      errors,
      Array.from({ length: 300 }, (_, i) => `burst ${i}`),
    )
    await alive(page)
    await degraded(h, page)
    h.check('300 page errors thrown at once all arrive, in order, and the page stays open')
  }),
  test('resilience.console-volume', ['resilience', 'console'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const messages = []
    page.on('console', (message) => messages.push(message.text()))
    await page.evaluate(
      `for(let i=0;i<5000;i++)console.log('line '+i);console.log('x'.repeat(3*1024*1024))`,
    )
    await h.until(
      () => messages.length >= 5001,
      `only ${messages.length} of 5,001 messages arrived`,
      60000,
    )
    assert.equal(messages[4999], 'line 4999')
    assert.equal(messages[5000], 'x'.repeat(3 * 1024 * 1024))
    await alive(page)
    h.check('5,000 console messages and one of 3 MiB arrive whole and the page stays open')
  }),
  test('resilience.huge-exception', ['resilience', 'errors'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const errors = []
    page.on('pageerror', (error) => errors.push(error.message))
    await page.evaluate(`setTimeout(()=>{throw new Error('m'.repeat(2*1024*1024))})`)
    await h.until(() => errors.length >= 1, 'the 2 MiB page error never arrived', 30000)
    assert.equal(errors[0], 'm'.repeat(2 * 1024 * 1024))
    await alive(page)
    h.check('A page error with a 2 MiB message arrives whole and the page stays open')
  }),
  test(
    'resilience.many-frames-init-script',
    ['resilience', 'frames'],
    async (h) => {
      // Chrome itself stops at 1,000 frames per page; two such pages at once
      // put more frames in flight than suitecut used to track.
      const { page, context } = await h.page()
      await context.addInitScript('globalThis.__resilienceInit=true')
      const other = await context.newPage()
      const url = h.fixture.origin + '/resilience/frames?n=1000'
      await Promise.all([page.goto(url, { timeout: 120000 }), other.goto(url, { timeout: 120000 })])
      for (const each of [page, other])
        await h.until(
          () =>
            each.evaluate(
              `[...document.querySelectorAll('iframe')].filter(f=>f.contentWindow?.__resilienceInit).length===1000`,
            ),
          'not every frame ran the init script',
          60000,
        )
      await alive(page)
      await alive(other)
      await degraded(h, page)
      h.check(
        '2,000 iframes loading at once each run the context init script and both pages stay open',
      )
    },
    240,
  ),
  test(
    'resilience.busy-main-thread',
    ['resilience', 'network'],
    async (h) => {
      // IKEA product pages keep the main thread busy for most of a minute.
      // Chrome answers DevTools late then; that used to close the page after 30 s.
      const { page } = await h.page()
      await page.goto(h.fixture.origin + '/app')
      const response = page.waitForResponse((r) => r.url().endsWith('/resilience/item?n=1'))
      await page.evaluate(`fetch('/resilience/item?n=1').then(r=>r.json())`)
      const item = await response
      const started = Date.now()
      void page
        .evaluate(`(()=>{const end=Date.now()+35000;while(Date.now()<end);})()`)
        .catch(() => undefined)
      const body = await item.body().then(
        (bytes) => bytes.toString(),
        (error) => error,
      )
      h.observe('body during busy loop', {
        ms: Date.now() - started,
        result: typeof body === 'string' ? body : String(body?.message ?? body),
      })
      // Calls made while the page is busy time out, as they would in Chrome.
      await h.until(
        () =>
          page.evaluate('1+1').then(
            (value) => value === 2,
            () => false,
          ),
        'the page never answered after the busy loop',
        60000,
      )
      assert(Date.now() - started > 30000, 'the busy loop ended before the 30 s deadline')
      await alive(page)
      assert.deepEqual(JSON.parse((await item.body()).toString()), { item: 1 })
      h.check(
        'A main thread busy for 35 s delays DevTools replies without closing the page, and bodies still read',
      )
    },
    120,
  ),
  test('resilience.covered-click-reason', ['resilience', 'input'], async (h) => {
    // Real sites cover links with sticky banners. A click that cannot land
    // must say what covers it, as Playwright does, not just time out.
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    await page.evaluate(
      `document.body.innerHTML='<button id="go" onclick="this.textContent=\\'clicked\\'">Go</button><div id="overlay" class="sticky banner" style="position:fixed;inset:0;background:#0003"></div>'`,
    )
    const snapshot = await page.ariaSnapshot({ mode: 'ai' })
    const ref = snapshot.match(/- button "Go" \[ref=([^\]]+)\]/u)?.[1]
    assert(ref, 'the covered button is missing from the snapshot')
    const error = await page
      .getByRef(ref)
      .click({ timeout: 1500 })
      .then(
        () => null,
        (e) => e,
      )
    assert(error, 'a covered click succeeded')
    assert.match(
      error.message,
      /<div id="overlay" class="sticky banner"> intercepts pointer events/u,
    )
    // The click still waits, so it lands once the cover goes away.
    const click = page.getByRef(ref).click({ timeout: 10000 })
    await page.evaluate(`setTimeout(()=>document.getElementById('overlay').remove(),300)`)
    await click
    assert.equal(await page.locator('#go').textContent(), 'clicked')
    // Carousels re-render their slides; a ref to a removed element says so.
    await page.evaluate(`document.getElementById('go').remove()`)
    const stale = await page
      .getByRef(ref)
      .click({ timeout: 1500 })
      .then(
        () => null,
        (e) => e,
      )
    assert.match(stale?.message ?? '', /no longer in the page; take a new snapshot/u)
    h.check(
      'A covered click names what covers it and lands once the cover is gone; a removed ref says so',
    )
  }),
  test('resilience.removed-link-ref', ['resilience', 'input'], async (h) => {
    // GitHub re-renders its sidebar after a soft navigation, removing the link
    // a fresh snapshot named. A removed <a> is its own root, and its `host` is
    // the URL's host, not a shadow host: walks must not step into that string.
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    await page.evaluate(
      `document.body.innerHTML='<nav><a id="labels" href="https://example.test/labels"><span>Labels</span></a></nav>'`,
    )
    const snapshot = await page.ariaSnapshot({ mode: 'ai' })
    const ref = snapshot.match(/- link "Labels" \[ref=([^\]]+)\]/u)?.[1]
    assert(ref, 'the link is missing from the snapshot')
    const handle = await page.locator('#labels').elementHandle()
    const span = await page.locator('#labels span').elementHandle()
    await page.evaluate(`document.getElementById('labels').remove()`)
    const target = page.getByRef(ref)
    for (const act of [
      () => target.click({ timeout: 1500 }),
      () => target.hover({ timeout: 1500 }),
    ]) {
      const error = await act().then(
        () => null,
        (e) => e,
      )
      assert.match(error?.message ?? '', /no longer in the page; take a new snapshot/u)
    }
    assert.equal(await page.getByRef(ref).count(), 0)
    assert.equal(await handle.boundingBox(), null)
    assert.equal(await span.boundingBox(), null)
    assert.equal(await handle.isVisible(), false)
    h.check('A ref or handle to a removed link reports it is gone instead of a TypeError')
  }),
  test('resilience.shared-scripts', ['resilience', 'input'], async (h) => {
    // The query engine and snapshot scripts are installed once per document
    // in suitecut's isolated world. A new document and an iframe each get
    // their own copy, and the page's own world never sees them.
    const { page } = await h.page()
    const names = `['__suitecutDomQuery','__suitecutSemanticSnapshot','__suitecutAriaJSON','__suitecutAriaFormatter','__suitecutAriaInteractive','__suitecutQueryDiagnostics'].filter(name=>name in globalThis)`
    for (const round of [1, 2]) {
      await page.goto(h.fixture.origin + '/app')
      await page.evaluate(
        `document.body.innerHTML='<button id="main" onclick="this.textContent=\\'main ${round}\\'">Main</button><iframe srcdoc="<button onclick=&quot;this.textContent=\\'frame ${round}\\'&quot;>Inner</button>"></iframe>'`,
      )
      await page.frameLocator('iframe').getByRole('button', { name: 'Inner' }).waitFor()
      const snapshot = await page.ariaSnapshot({ mode: 'ai' })
      const inner = snapshot.match(/- button "Inner" \[ref=([^\]]+)\]/u)?.[1]
      assert(inner, `round ${round}: the iframe button is missing from the snapshot`)
      await page.getByRef(inner).click()
      await page.getByRole('button', { name: 'Main' }).click()
      assert.equal(await page.locator('#main').textContent(), `main ${round}`)
      assert.equal(
        await page.frameLocator('iframe').locator('button').textContent(),
        `frame ${round}`,
      )
      assert.deepEqual(await page.evaluate(names), [])
      for (const frame of page.frames()) assert.deepEqual(await frame.evaluate(names), [])
    }
    h.check('Shared scripts work across documents and frames and stay out of the page world')
  }),
  test('resilience.covered-click-fallback', ['resilience', 'input'], async (h) => {
    // browser-use clicks a covered element with element.click(); suitecut does
    // so only when asked, after a normal click has waited fallbackAfter ms.
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    await page.evaluate(
      `document.body.innerHTML='<button id="go" onclick="this.textContent=\\'clicked\\'">Go</button><button id="off" disabled>Off</button><div id="overlay" style="position:fixed;inset:0;background:#0003"></div>'`,
    )
    const started = Date.now()
    await page.locator('#go').click({ fallback: 'js', fallbackAfter: 300, timeout: 5000 })
    const ms = Date.now() - started
    assert.equal(await page.locator('#go').textContent(), 'clicked')
    assert(ms < 3000, `the fallback took ${ms} ms`)
    assert.equal(await page.locator('#overlay').count(), 1)
    const error = await page
      .locator('#off')
      .click({ force: false, fallback: 'js', fallbackAfter: 300, timeout: 1500 })
      .then(
        () => null,
        (e) => e,
      )
    assert.match(error?.message ?? '', /waiting for element to be enabled/u)
    h.observe('fallback', { ms })
    h.check(
      "A covered click with fallback 'js' lands through the cover; a disabled one still fails",
    )
  }),
  test('resilience.large-inputs', ['resilience', 'protocol'], async (h) => {
    // Playwright sends inputs of any size; commands used to stop at 64 KiB.
    const { context, page } = await h.page()
    page.setDefaultTimeout(30000)
    await page.goto(h.fixture.origin + '/app')
    const n = 16 * 1024 * 1024
    const timings = {}
    const time = async (name, run) => {
      const started = Date.now()
      const value = await run()
      timings[name] = Date.now() - started
      assert.equal(value, n, `${name} returned ${value}`)
    }
    await time('evaluate', () => page.evaluate((s) => s.length, 'a'.repeat(n)))
    await time('setContent', async () => {
      await page.setContent(`<textarea id="t"></textarea><p id="p">${'b'.repeat(n)}</p>`)
      return page.evaluate(`document.querySelector('#p').textContent.length`)
    })
    await time('getByText', () =>
      page
        .getByText('b'.repeat(n))
        .textContent()
        .then((s) => s.length),
    )
    await time('fill', async () => {
      await page.locator('#t').fill('c'.repeat(n))
      return page.evaluate(`document.querySelector('#t').value.length`)
    })
    await time('addInitScript', async () => {
      await context.addInitScript(`globalThis.big = ${JSON.stringify('d'.repeat(n))}`)
      await page.goto(h.fixture.origin + '/app')
      return page.evaluate('globalThis.big.length')
    })
    h.observe('ms', timings)
    h.check('16 MiB evaluate, setContent, getByText, fill and addInitScript inputs arrive whole')
  }),
  test('resilience.ai-snapshot-many-frames', ['resilience', 'dom'], async (h) => {
    // An AI snapshot used to fail past 31 iframes; Blink allows 1000 per page.
    const { page } = await h.page()
    page.setDefaultTimeout(30000)
    const frames = 40
    await page.setContent(
      Array.from(
        { length: frames },
        (_, i) => `<iframe srcdoc="<button>Frame ${i}</button>" width=80 height=40></iframe>`,
      ).join(''),
    )
    await page.waitForFunction(
      `[...document.querySelectorAll('iframe')].every((f) => f.contentDocument?.querySelector('button'))`,
    )
    const started = Date.now()
    const snapshot = await page.ariaSnapshot({ mode: 'ai' })
    h.observe('ms', Date.now() - started)
    const buttons = snapshot.match(/button "Frame \d+" \[ref=s\d+e\d+\]/gu) ?? []
    assert.equal(buttons.length, frames, snapshot.slice(0, 2000))
    await page.getByRef(/\[ref=(s\d+e\d+)\]/u.exec(buttons.at(-1))[1]).click()
    h.check(`an AI snapshot covers ${frames} iframes and its refs click`)
  }),
  test('resilience.websocket-large-frames', ['resilience', 'network', 'websocket'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const bytes = 3 * 1024 * 1024
    const received = []
    page.on('websocket', (socket) =>
      socket.on('framereceived', (frame) =>
        received.push(
          typeof frame.payload === 'string' ? frame.payload.length : frame.payload.byteLength,
        ),
      ),
    )
    const sizes = await page.evaluate(
      `new Promise((resolve,reject)=>{const s=new WebSocket(location.origin.replace('http','ws')+'/resilience/ws?bytes=${bytes}');s.binaryType='arraybuffer';const got=[];s.onmessage=e=>{got.push(typeof e.data==='string'?e.data.length:e.data.byteLength);if(got.length===2){s.send(new Uint8Array(${bytes}));}if(got.length===3){s.close();resolve(got)}};s.onerror=()=>reject(new Error('socket failed'))})`,
    )
    assert.deepEqual(sizes, [bytes, bytes, String(bytes).length])
    await h.until(() => received.length >= 3, 'frames were not observed', 30000)
    assert.deepEqual(received.slice(0, 2), [bytes, bytes])
    await alive(page)
    h.check('3 MiB WebSocket text and binary frames are observed whole and the page stays open')
  }),
  test('resilience.large-post-body', ['resilience', 'network'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/app')
    const size = 3 * 1024 * 1024
    const body = 'p'.repeat(size)
    const request = page.waitForRequest((r) => r.url().endsWith('/resilience/sink'))
    const result = await page.evaluate(
      `fetch('/resilience/sink',{method:'POST',body:'p'.repeat(${size})}).then(r=>r.json())`,
    )
    assert.deepEqual(result, {
      bytes: size,
      sha256: createHash('sha256').update(body).digest('hex'),
    })
    assert.equal((await request).postData(), body)
    await alive(page)
    h.check('A 3 MiB POST body reaches the server and request.postData() returns it whole')
  }),
  test(
    'resilience.har-volume',
    ['resilience', 'network', 'artifacts'],
    async (h) => {
      const path = join(h.output, 'volume.har')
      const { page, context } = await h.page({ recordHar: { path } })
      page.setDefaultTimeout(120000)
      await page.goto(h.fixture.origin + '/app')
      await page.evaluate(
        `(async()=>{for(let i=0;i<3000;i+=100)await Promise.all(Array.from({length:100},(_,j)=>fetch('/resilience/item?n='+(i+j))))})()`,
      )
      await alive(page)
      await context.close()
      const har = JSON.parse(await readFile(path, 'utf8'))
      const items = har.log.entries.filter((e) => e.request.url.includes('/resilience/item'))
      assert.equal(items.length, 3000)
      h.check('A HAR recording keeps all 3,000 entries, more than the old 2,048 cap')
    },
    240,
  ),
  test(
    'resilience.huge-snapshot',
    ['resilience', 'snapshot'],
    async (h) => {
      // The full Python docs index is 44,000 elements; Playwright snapshots
      // it whole. suitecut used to stop at 65,536 nodes and 64 levels.
      const { page } = await h.page()
      await page.goto(h.fixture.origin + '/resilience/large?rows=10000&longname=1', {
        timeout: 60000,
      })
      const wide = await page.ariaSnapshot({ mode: 'ai', timeout: 90000 })
      const last = wide.match(/- button "Add product 9999" \[ref=([^\]]+)\]/u)
      assert(last, 'the last row is missing from the snapshot')
      // Like Playwright, YAML leaves out names this long; it used to fail the snapshot.
      assert(wide.includes('- button [ref='), 'the long-named button is missing')
      await page.getByRef(last[1]).click()
      await page.goto(h.fixture.origin + '/resilience/large?rows=10&depth=100')
      const deep = await page.ariaSnapshot({ mode: 'ai', timeout: 60000 })
      assert(deep.includes('Add product 9'), 'the page nested 100 levels deep is missing')
      await alive(page)
      h.observe('snapshot', { chars: wide.length, refs: wide.split('[ref=').length - 1 })
      h.check(
        'A 50,000-element page and one nested 100 levels deep snapshot whole and their refs act',
      )
    },
    240,
  ),
  test(
    'resilience.trace-large-aria',
    ['resilience', 'artifacts'],
    async (h) => {
      const { page, context } = await h.page()
      await context.tracing.start({ screenshots: false, snapshots: { aria: true } })
      await page.goto(h.fixture.origin + '/resilience/large?rows=3000', { timeout: 60000 })
      await page.getByRole('button', { name: 'Add product 2999' }).click()
      const path = join(h.output, 'large-aria.zip')
      await context.tracing.stop({ path })
      const bytes = await readFile(path)
      assert.equal(bytes.subarray(0, 4).toString('hex'), '504b0304')
      assert(bytes.includes('aria/'), 'trace has no aria snapshot')
      await alive(page)
      h.check('Tracing a page whose accessibility tree is far over 64 KiB keeps its aria snapshots')
    },
    180,
  ),
]
