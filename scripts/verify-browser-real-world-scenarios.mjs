#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Executes a selectable emitted production SDK across different browser backends. */
/* global setTimeout, clearTimeout */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash, randomUUID } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2) {
  assert.ok(
    process.argv[index]?.startsWith('--') && process.argv[index + 1],
    'Expected --option value',
  )
  options.set(process.argv[index], process.argv[index + 1])
}
const output = resolve(options.get('--output') ?? '.suitecut/real-world-review/journeys')
await mkdir(output, { recursive: true })
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
async function sdkManifest() {
  const base = dirname(sdkPath)
  const manifest = {}
  for (const file of (await readdir(base, { recursive: true }))
    .filter((file) => file.endsWith('.js'))
    .sort())
    manifest[file] = createHash('sha256')
      .update(await readFile(resolve(base, file)))
      .digest('hex')
  return manifest
}
const sdkBefore = await sdkManifest()
const sdk = await import(pathToFileURL(sdkPath).href)
const candidates = [
  {
    backend: 'cdp',
    executablePath: resolve(
      options.get('--cdp-executable') ??
        '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
    ),
  },
  {
    backend: 'cef',
    executablePath: resolve(
      options.get('--cef-executable') ?? '.suitecut/memory-deep/runtime/suitecut-browser',
    ),
  },
  {
    backend: 'bidi',
    executablePath: resolve(
      options.get('--bidi-executable') ?? '.suitecut/browser-protocol/tools/firefox/firefox',
    ),
  },
].filter((candidate) =>
  (options.get('--backends') ?? 'cdp,cef,bidi').split(',').includes(candidate.backend),
)
const uploadBytes = Buffer.from(
  'Quarterly supplier inventory\nsku,quantity\nkeyboard-pro,3\nUnicode: λ 漢字\n'.repeat(2048),
)
const uploadHash = createHash('sha256').update(uploadBytes).digest('hex')
const uploadPath = resolve(output, 'supplier-inventory.csv')
await writeFile(uploadPath, uploadBytes)
const accounts = new Map()
const audit = []
const started = performance.now()
const report = {
  passed: false,
  started: new Date().toISOString(),
  sdkPath,
  sdkBefore,
  scope:
    'Local realistic app journeys through public SDK controls; no external accounts, purchases, videos or official benchmark score.',
  timingCaveat:
    'Other approved workloads may overlap; elapsed times describe this verification and are not comparative performance claims.',
  backends: [],
}
let server,
  browser,
  origin,
  deadline,
  stopping = false

const fixture = `<!doctype html><meta charset="utf-8"><title>SuiteCut Operations</title>
<style>body{font:16px sans-serif;margin:20px}label,button,select{display:block;margin:6px}iframe{width:600px;height:140px}</style>
<main id="application">Loading application</main>
<script>
const app=document.querySelector('#application');let state;let category='all';let query='';
async function api(path,body){const response=await fetch(path,{method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'content-type':'application/json'},body:body===undefined?undefined:JSON.stringify(body)});const value=await response.json();if(!response.ok)throw Error(value.error);return value}
function login(){app.innerHTML='<h1>Supplier sign in</h1><label>Email<input id=email></label><label>Password<input id=password type=password></label><button id=login>Sign in</button><output id=status></output>';document.querySelector('#login').onclick=async()=>{try{await api('/api/login',{email:document.querySelector('#email').value,password:document.querySelector('#password').value});await start()}catch(error){document.querySelector('#status').textContent=error.message}}}
function dashboard(){app.innerHTML='<h1>Operations workspace</h1><p id=identity></p><button id=logout>Sign out</button><label>Catalog search<input id=search></label><label>Category<select id=category><option value=all>All products</option><option value=electronics>Electronics</option><option value=furniture>Furniture</option></select></label><button id=search-button>Search catalog</button><section id=catalog></section><h2>Cart</h2><label>Cart quantity<input id=quantity type=number></label><button id=update>Update cart</button><output id=cart></output><label>Shipping email<input id=shipping-email></label><label>Shipping address<input id=address></label><button id=checkout>Confirm order</button><output id=checkout-status></output><button id=popup>Open team workspace</button><output id=collaboration></output><iframe id=collaboration-frame src=/frame></iframe><label>Inventory file<input id=inventory type=file></label><button id=upload>Upload inventory</button><output id=upload-status></output><a id=report href=/report>Download confirmed order</a>';document.querySelector('#identity').textContent=state.email;document.querySelector('#quantity').value=state.quantity;document.querySelector('#cart').textContent='Quantity '+state.quantity;document.querySelector('#logout').onclick=async()=>{await api('/api/logout',{});await start()};document.querySelector('#search-button').onclick=()=>{query=document.querySelector('#search').value;category=document.querySelector('#category').value;catalog()};document.querySelector('#update').onclick=async()=>{state=await api('/api/cart',{quantity:Number(document.querySelector('#quantity').value)});document.querySelector('#cart').textContent='Quantity '+state.quantity};document.querySelector('#checkout').onclick=async()=>{try{state=await api('/api/checkout',{email:document.querySelector('#shipping-email').value,address:document.querySelector('#address').value});document.querySelector('#checkout-status').textContent='Confirmed '+state.order.id+' total '+state.order.total}catch(error){document.querySelector('#checkout-status').textContent=error.message}};document.querySelector('#popup').onclick=()=>window.open('/popup');document.querySelector('#upload').onclick=async()=>{const file=document.querySelector('#inventory').files[0];if(!file){document.querySelector('#upload-status').textContent='Choose a file';return}const response=await fetch('/api/upload',{method:'POST',body:file});const result=await response.json();document.querySelector('#upload-status').textContent='Uploaded '+result.bytes+' bytes '+result.sha256};catalog()}
function catalog(){const entries=[{id:'keyboard-pro',name:'Keyboard Pro',category:'electronics',price:14900},{id:'keyboard-mini',name:'Keyboard Mini',category:'electronics',price:7900},{id:'desk',name:'Standing Desk',category:'furniture',price:45000}];document.querySelector('#catalog').innerHTML=entries.filter(item=>(category==='all'||item.category===category)&&item.name.toLowerCase().includes(query.toLowerCase())).map(item=>'<article><span>'+item.name+'</span><button data-testid="add-'+item.id+'">Add '+item.name+'</button></article>').join('');for(const button of document.querySelectorAll('[data-testid]'))button.onclick=async()=>{state=await api('/api/cart',{quantity:state.quantity+1});document.querySelector('#quantity').value=state.quantity;document.querySelector('#cart').textContent='Quantity '+state.quantity}}
async function start(){state=await api('/api/session');if(state.authenticated)dashboard();else login()}
window.addEventListener('message',event=>{if(event.origin===location.origin&&event.data.kind==='collaboration')document.querySelector('#collaboration').textContent=event.data.text});start();
</script>`
const frameHTML = `<!doctype html><meta charset="utf-8"><title>Review notes</title><label>Review note<input id=note></label><button onclick="save()">Publish note</button><output></output><script>async function save(){const text=document.querySelector('input').value;await fetch('/api/note',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({text})});document.querySelector('output').textContent='Published '+text;parent.postMessage({kind:'collaboration',text},location.origin)}</script>`
const popupHTML = `<!doctype html><meta charset="utf-8"><title>Team workspace</title><label>Team update<input id=update></label><button onclick="save()">Share update</button><output></output><script>async function save(){const text=document.querySelector('input').value;await fetch('/api/note',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({text})});opener.postMessage({kind:'collaboration',text},location.origin);document.querySelector('output').textContent='Shared '+text}</script>`
await writeFile(resolve(output, 'app.html'), fixture)
await writeFile(resolve(output, 'frame.html'), frameHTML)
await writeFile(resolve(output, 'popup.html'), popupHTML)

function session(request) {
  const id = /(?:^|;\s*)suitecut_session=([^;]+)/u.exec(request.headers.cookie ?? '')?.[1]
  return id ? accounts.get(id) : undefined
}
function json(response, value, status = 200, headers = {}) {
  response.writeHead(status, {
    'content-type': 'application/json',
    'cache-control': 'no-store',
    ...headers,
  })
  response.end(JSON.stringify(value))
}
async function requestBody(request) {
  const chunks = []
  let size = 0
  for await (const chunk of request) {
    size += chunk.length
    assert.ok(size <= 2 * 1024 * 1024, 'Fixture request exceeds bounded upload size')
    chunks.push(chunk)
  }
  return Buffer.concat(chunks)
}
async function serve(request, response) {
  const path = new URL(request.url ?? '/', origin).pathname
  const state = session(request)
  if (path.startsWith('/api/')) {
    const body = request.method === 'POST' ? await requestBody(request) : undefined
    if (path === '/api/login') {
      const credentials = JSON.parse(body.toString())
      if (
        credentials.password !== 'correct horse battery staple' ||
        !credentials.email.endsWith('@suitecut.test')
      )
        return json(response, { error: 'Invalid account credentials' }, 401)
      const id = randomUUID()
      const account = {
        authenticated: true,
        email: credentials.email,
        quantity: 0,
        order: null,
        notes: [],
        uploads: [],
      }
      accounts.set(id, account)
      audit.push({ event: 'login', email: account.email })
      return json(response, account, 200, {
        'set-cookie': `suitecut_session=${id}; Path=/; HttpOnly; SameSite=Lax`,
      })
    }
    if (path === '/api/session') return json(response, state ?? { authenticated: false })
    if (!state) return json(response, { error: 'Authentication required' }, 401)
    if (path === '/api/logout') {
      audit.push({ event: 'logout', email: state.email })
      const id = /(?:^|;\s*)suitecut_session=([^;]+)/u.exec(request.headers.cookie ?? '')?.[1]
      accounts.delete(id)
      return json(response, { authenticated: false }, 200, {
        'set-cookie': 'suitecut_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax',
      })
    }
    if (path === '/api/upload') {
      const stored = { bytes: body.length, sha256: createHash('sha256').update(body).digest('hex') }
      state.uploads.push(stored)
      audit.push({ event: 'upload', email: state.email, ...stored })
      return json(response, stored)
    }
    const value = body ? JSON.parse(body.toString()) : {}
    if (path === '/api/cart') {
      if (!Number.isInteger(value.quantity) || value.quantity < 0 || value.quantity > 20)
        return json(response, { error: 'Quantity must be from 0 to 20' }, 400)
      state.quantity = value.quantity
      return json(response, state)
    }
    if (path === '/api/checkout') {
      if (!value.email?.includes('@') || !value.address?.trim())
        return json(response, { error: 'Shipping email and address are required' }, 400)
      if (state.quantity <= 0) return json(response, { error: 'Cart is empty' }, 400)
      state.order = {
        id: `ORDER-${state.email.split('@')[0]}`,
        quantity: state.quantity,
        total: 14900 * state.quantity,
        shippingEmail: value.email,
        address: value.address,
      }
      audit.push({ event: 'order', email: state.email, order: state.order })
      return json(response, state)
    }
    if (path === '/api/note') {
      state.notes.push(value.text)
      audit.push({ event: 'note', email: state.email, text: value.text })
      return json(response, { stored: true })
    }
  }
  if (path === '/report') {
    if (!state?.order) return json(response, { error: 'No confirmed order' }, 409)
    const bytes = Buffer.from(JSON.stringify(state.order))
    response.writeHead(200, {
      'content-type': 'application/json',
      'content-disposition': 'attachment; filename="confirmed-order.json"',
      'content-length': bytes.length,
    })
    response.end(bytes)
    return
  }
  response.writeHead(path === '/offline' ? 503 : 200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
  })
  response.end(
    path === '/frame'
      ? frameHTML
      : path === '/popup'
        ? popupHTML
        : path === '/offline'
          ? '<!doctype html><title>Service temporarily unavailable</title><h1>Temporary outage</h1><button onclick="location.href=\'/app\'">Retry application</button>'
          : fixture,
  )
}

async function eventually(read, expected, timeout = 5000) {
  const until = performance.now() + timeout
  let actual, lastError
  while (performance.now() < until) {
    try {
      actual = await read()
      if (typeof expected === 'function' ? expected(actual) : actual === expected) return actual
    } catch (error) {
      lastError = error
    }
    await delay(20)
  }
  throw new Error(
    `Expected state ${String(expected)}; observed ${JSON.stringify(actual)}${lastError ? `; ${lastError.message}` : ''}`,
  )
}
function memory() {
  globalThis.gc?.()
  return process.memoryUsage()
}
async function signIn(page, email) {
  await eventually(() => page.getByRole('button', { name: 'Sign in', exact: true }).count(), 1)
  await page.getByLabel('Email', { exact: true }).fill(email)
  await page.getByLabel('Password', { exact: true }).fill('correct horse battery staple')
  await page.getByRole('button', { name: 'Sign in', exact: true }).click()
  await eventually(() => page.locator('#identity').textContent(), email)
}
async function verify(candidate) {
  const result = {
    ...candidate,
    passed: false,
    journeys: [],
    unsupported: [],
    memoryBefore: memory(),
  }
  report.backends.push(result)
  const check = async (name, work) => {
    const start = performance.now()
    try {
      const evidence = await work()
      result.journeys.push({ name, passed: true, elapsedMs: performance.now() - start, evidence })
    } catch (error) {
      result.journeys.push({
        name,
        passed: false,
        elapsedMs: performance.now() - start,
        error: error.message,
      })
      throw error
    }
  }
  const backendDir = resolve(output, candidate.backend)
  await mkdir(backendDir, { recursive: true })
  try {
    browser = await sdk.launchBrowser(
      candidate.backend === 'cef'
        ? {
            backend: 'cef',
            executablePath: candidate.executablePath,
            headless: true,
            width: 1100,
            height: 850,
            framesPerSecond: 10,
            pixelFormat: 'bgra',
            userDataDir: resolve(backendDir, 'profile'),
          }
        : {
            ...candidate,
            headless: true,
            viewport: { width: 1100, height: 850 },
            timeoutMs: 15_000,
            args:
              candidate.backend === 'cdp' && process.getuid?.() === 0
                ? ['--no-sandbox', '--disable-dev-shm-usage']
                : [],
          },
    )
    result.version = browser.version()
    result.capabilities = browser.capabilities()
    const context = await browser.newContext({
      viewport: { width: 1100, height: 850 },
      acceptDownloads: true,
    })
    const page = await context.newPage()
    page.setDefaultTimeout(4000)
    page.setDefaultNavigationTimeout?.(8000)
    const email = `${candidate.backend}@suitecut.test`
    await page.goto(`${origin}/app`)
    await check('account sign-in, reload persistence and isolated cookies', async () => {
      await signIn(page, email)
      await page.reload()
      await eventually(() => page.locator('#identity').textContent(), email)
      const cookies = await context.cookies()
      assert.equal(cookies.filter((cookie) => cookie.name === 'suitecut_session').length, 1)
      const isolated = await browser.newContext()
      try {
        const isolatedPage = await isolated.newPage()
        await isolatedPage.goto(`${origin}/app`)
        await eventually(
          () => isolatedPage.getByRole('button', { name: 'Sign in', exact: true }).count(),
          1,
        )
        assert.equal(
          (await isolated.cookies()).some((cookie) => cookie.name === 'suitecut_session'),
          false,
        )
      } finally {
        await isolated.close()
      }
      return { persistedEmail: email, isolatedCookieAbsent: true }
    })
    const account = [...accounts.values()].find((value) => value.email === email)
    await check('catalog filtering, cart edits, checkout validation and confirmation', async () => {
      await page.getByLabel('Catalog search', { exact: true }).fill('keyboard')
      await page.getByLabel('Category', { exact: true }).selectOption('electronics')
      await page.getByRole('button', { name: 'Search catalog', exact: true }).click()
      assert.equal(
        await page.getByRole('button', { name: 'Add Standing Desk', exact: true }).count(),
        0,
      )
      await page.getByRole('button', { name: 'Add Keyboard Pro', exact: true }).click()
      await eventually(() => page.locator('#cart').textContent(), 'Quantity 1')
      await page.getByLabel('Cart quantity', { exact: true }).fill('3')
      await page.getByRole('button', { name: 'Update cart', exact: true }).click()
      await eventually(() => page.locator('#cart').textContent(), 'Quantity 3')
      await page.getByRole('button', { name: 'Confirm order', exact: true }).click()
      await eventually(
        () => page.locator('#checkout-status').textContent(),
        'Shipping email and address are required',
      )
      assert.equal(account.order, null, 'Invalid checkout must not create server order')
      await page.getByLabel('Shipping email', { exact: true }).fill(email)
      await page.getByLabel('Shipping address', { exact: true }).fill('42 Native SDK Way')
      await page.getByRole('button', { name: 'Confirm order', exact: true }).click()
      await eventually(
        () => page.locator('#checkout-status').textContent(),
        `Confirmed ORDER-${candidate.backend} total 44700`,
      )
      assert.deepEqual(account.order, {
        id: `ORDER-${candidate.backend}`,
        quantity: 3,
        total: 44700,
        shippingEmail: email,
        address: '42 Native SDK Way',
      })
      return { serverOrder: account.order, invalidCheckoutRejected: true }
    })
    if (candidate.backend === 'bidi')
      result.unsupported.push(
        'Popup/frame collaboration and upload/download are not implemented by the current BiDi adapter; common account, commerce and recovery journeys still run.',
      )
    else {
      await check('popup and embedded-frame team collaboration', async () => {
        const pending = page.waitForEvent('popup', { timeout: 6000 })
        await page.getByRole('button', { name: 'Open team workspace', exact: true }).click()
        const popup = await pending
        try {
          await popup
            .getByLabel('Team update', { exact: true })
            .fill(`Approved ${candidate.backend}`)
          await popup.getByRole('button', { name: 'Share update', exact: true }).click()
          await eventually(
            () => page.locator('#collaboration').textContent(),
            `Approved ${candidate.backend}`,
          )
        } finally {
          await popup.close()
        }
        await page
          .frameLocator('#collaboration-frame')
          .getByLabel('Review note', { exact: true })
          .fill(`Reviewed ${candidate.backend}`)
        await page
          .frameLocator('#collaboration-frame')
          .getByRole('button', { name: 'Publish note', exact: true })
          .click()
        await eventually(
          () => page.locator('#collaboration').textContent(),
          `Reviewed ${candidate.backend}`,
        )
        assert.deepEqual(account.notes, [
          `Approved ${candidate.backend}`,
          `Reviewed ${candidate.backend}`,
        ])
        return { independentlyStoredNotes: account.notes }
      })
      await check('server-verified inventory upload and confirmed report download', async () => {
        await page.getByLabel('Inventory file', { exact: true }).setInputFiles(uploadPath)
        await page.getByRole('button', { name: 'Upload inventory', exact: true }).click()
        await eventually(
          () => page.locator('#upload-status').textContent(),
          `Uploaded ${uploadBytes.length} bytes ${uploadHash}`,
        )
        assert.deepEqual(account.uploads, [{ bytes: uploadBytes.length, sha256: uploadHash }])
        const pending = page.waitForEvent('download', { timeout: 6000 })
        await page.locator('#report').click()
        const download = await pending
        const path = resolve(backendDir, 'confirmed-order.json')
        await download.saveAs(path)
        const actual = await readFile(path)
        const expected = Buffer.from(JSON.stringify(account.order))
        assert.deepEqual(actual, expected)
        return {
          uploadBytes: uploadBytes.length,
          uploadSha256: uploadHash,
          reportBytes: actual.length,
          reportSha256: createHash('sha256').update(actual).digest('hex'),
        }
      })
    }
    await check('503 page retry, failed-action recovery and logout', async () => {
      await page.goto(`${origin}/offline`)
      await eventually(
        () => page.getByRole('button', { name: 'Retry application', exact: true }).count(),
        1,
      )
      page.setDefaultTimeout(50)
      await assert.rejects(page.locator('#missing-control').click())
      page.setDefaultTimeout(4000)
      await page.getByRole('button', { name: 'Retry application', exact: true }).click()
      await eventually(() => page.locator('#identity').textContent(), email)
      await page.getByRole('button', { name: 'Sign out', exact: true }).click()
      await eventually(() => page.getByRole('button', { name: 'Sign in', exact: true }).count(), 1)
      await page.reload()
      await eventually(() => page.getByRole('button', { name: 'Sign in', exact: true }).count(), 1)
      assert.equal(
        (await context.cookies()).some((cookie) => cookie.name === 'suitecut_session'),
        false,
      )
      assert.ok(audit.some((event) => event.event === 'logout' && event.email === email))
      return { sessionAbsentAfterReload: true, serverLogoutObserved: true }
    })
    await context.close()
    result.beforeClose = browser.connectionStats?.()
    result.passed = true
  } catch (error) {
    result.error = { message: error.message, stack: error.stack }
  } finally {
    try {
      await browser?.close()
      if (browser) await browser.closed
      result.afterClose = browser?.connectionStats?.()
      if (result.afterClose) {
        assert.equal(result.afterClose.pending, 0)
        assert.equal(result.afterClose.eventListeners, 0)
      }
      result.nativeProcessExit = browser?.source?.processExit?.()
      result.cleanupPassed = true
    } catch (error) {
      result.cleanupPassed = false
      result.cleanupError = error.message
      result.passed = false
    }
    result.memoryAfter = memory()
    browser = undefined
    await writeFile(resolve(backendDir, 'results.json'), `${JSON.stringify(result, null, 2)}\n`)
    console.log(
      JSON.stringify({
        backend: result.backend,
        passed: result.passed,
        journeys: result.journeys.map((value) => ({ name: value.name, passed: value.passed })),
        unsupported: result.unsupported,
        error: result.error?.message,
      }),
    )
  }
}
try {
  server = createServer((request, response) => {
    void serve(request, response).catch((error) => response.destroy(error))
  })
  await new Promise((resolveListen) => server.listen(0, '127.0.0.1', resolveListen))
  origin = `http://127.0.0.1:${server.address().port}`
  await Promise.race([
    (async () => {
      for (const candidate of candidates) {
        if (stopping) break
        await verify(candidate)
      }
    })(),
    new Promise((_, reject) => {
      deadline = setTimeout(() => {
        stopping = true
        void browser?.close().catch(() => undefined)
        reject(new Error('Real-world journeys exceeded 180 second deadline'))
      }, 180_000)
    }),
  ])
  report.passed =
    report.backends.length === candidates.length && report.backends.every((value) => value.passed)
} catch (error) {
  report.error = { message: error.message, stack: error.stack }
} finally {
  clearTimeout(deadline)
  await browser?.close().catch(() => undefined)
  if (server) {
    server.closeAllConnections()
    await new Promise((resolveClose) => server.close(resolveClose))
  }
  report.durationMs = performance.now() - started
  report.audit = audit
  report.sdkAfter = await sdkManifest()
  report.sdkUnchanged = JSON.stringify(report.sdkBefore) === JSON.stringify(report.sdkAfter)
  if (!report.sdkUnchanged) report.passed = false
  await writeFile(resolve(output, 'results.json'), `${JSON.stringify(report, null, 2)}\n`)
  if (!report.passed) process.exitCode = 1
  console.log(
    JSON.stringify({
      passed: report.passed,
      durationMs: report.durationMs,
      report: resolve(output, 'results.json'),
    }),
  )
}
