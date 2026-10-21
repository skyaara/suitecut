import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable, 'SUITECUT_NATIVE_EXECUTABLE is required')
const directory = resolve('.suitecut/native-storage-state')
await mkdir(directory, { recursive: true })
await rm(resolve(directory, 'report.json'), { force: true })
await rm(resolve(directory, 'failure.json'), { force: true })
const diagnostics = []
let diagnosticBytes = 0
let requests = 0
const requestRecords = []
const server = createServer((request, response) => {
  requests++
  requestRecords.push({
    index: requests,
    phase,
    url: request.url,
    host: request.headers.host,
    referer: request.headers.referer,
    destination: request.headers['sec-fetch-dest'],
  })
  response.setHeader('Content-Type', 'text/html')
  response.end(
    `<!doctype html><script>document.documentElement.dataset.restored=localStorage.getItem('auth')||'missing';</script><p>Storage origin</p>`,
  )
})
await new Promise((done) => server.listen(0, '0.0.0.0', done))
const address = server.address()
assert(address && typeof address === 'object')
const origin = `http://127.0.0.1:${address.port}`
const otherOrigin = `http://localhost:${address.port}`
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const executableSha256 = await hash()
const started = new Date().toISOString()
let source
let phase = 'launch'
let capturedError = null
try {
  source = await launchNativeBrowser({
    executablePath: resolve(executable),
    headless: true,
    width: 640,
    height: 480,
    onDiagnostic(message) {
      if (diagnosticBytes >= 1048576) return
      const bounded = message.slice(0, 1048576 - diagnosticBytes)
      diagnostics.push(bounded)
      diagnosticBytes += Buffer.byteLength(bounded)
    },
  })
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const page = await context.newPage()
  await page.goto(`${origin}/author`)
  await context.addCookies([{ name: 'auth-cookie', value: 'native', url: origin, httpOnly: true }])
  await page.evaluate(`(async()=>{
    localStorage.setItem('auth','native');
    const opening=indexedDB.open('suitecut-auth',3);
    opening.onupgradeneeded=()=>{const store=opening.result.createObjectStore('tokens');store.createIndex('by-label','label');opening.result.createObjectStore('zz-inline',{keyPath:'taskTitle'});};
    const db=await new Promise((resolve,reject)=>{opening.onsuccess=()=>resolve(opening.result);opening.onerror=()=>reject(opening.error);});
    const tx=db.transaction(['tokens','zz-inline'],'readwrite'),store=tx.objectStore('tokens');
    const shared=new Uint8Array([0,255,17,92]).buffer;
    const record={label:'native',date:new Date('2026-01-02T03:04:05Z'),bigint:42n,buffer:shared,view:new Uint8Array(shared,1,2),blob:new Blob(['native blob'],{type:'text/plain'}),map:new Map(),set:new Set(['native'])};
    record.self=record;record.map.set(record,record);store.put(record,'token');tx.objectStore('zz-inline').put({taskTitle:'Pet the cat',binaryTitle:new TextEncoder().encode('Pet the cat').buffer});
    await new Promise((resolve,reject)=>{tx.oncomplete=()=>resolve();tx.onerror=()=>reject(tx.error);});db.close();return true;
  })()`)
  await page.evaluate(`(async()=>{
    const root=await navigator.storage.getDirectory(),nested=await root.getDirectoryHandle('nested',{create:true});
    await nested.getDirectoryHandle('empty',{create:true});
    for(const[parent,name,data]of[[root,'hello.txt','Hello, world!'],[nested,'data.bin',new Uint8Array([0,1,2,255])]]){const writer=await(await parent.getFileHandle(name,{create:true})).createWritable();await writer.write(data);await writer.close();}return true;
  })()`)
  await page.goto(`${otherOrigin}/author`)
  await page.evaluate("localStorage.setItem('other-origin','retained after page close')")
  await page.waitForLoadState('networkidle')
  await page.close()
  const baselineRequests = requests
  phase = 'capture closed origins without author requests or public pages'
  const localState = await context.storageState()
  assert.equal(requests, baselineRequests)
  assert.equal(context.pages().length, 0)
  assert.equal(localState.origins.length, 2)
  assert(localState.origins.every((entry) => entry.indexedDB === undefined))
  const statePath = resolve(directory, 'authentication.json')
  const state = await context.storageState({ indexedDB: true, opfs: true, path: statePath })
  assert.equal(await readFile(statePath, 'utf8'), JSON.stringify(state, null, 2))
  assert.equal(requests, baselineRequests)
  assert.equal(state.cookies.find((cookie) => cookie.name === 'auth-cookie')?.value, 'native')
  const storedOrigin = state.origins.find((entry) => entry.origin === origin)
  assert.equal(storedOrigin?.indexedDB?.[0]?.version, 3)
  assert.equal(storedOrigin?.indexedDB?.[0]?.stores[0]?.indexes[0]?.name, 'by-label')
  assert.deepEqual(
    storedOrigin?.indexedDB?.[0]?.stores.find((store) => store.name === 'zz-inline')?.records,
    [
      {
        valueEncoded: {
          id: 1,
          o: [
            { k: 'taskTitle', v: 'Pet the cat' },
            { k: 'binaryTitle', v: { ab: { b: 'UGV0IHRoZSBjYXQ=' } } },
          ],
        },
      },
    ],
  )
  assert.deepEqual(storedOrigin?.opfs, [
    { path: 'hello.txt', type: 'file', base64: 'SGVsbG8sIHdvcmxkIQ==' },
    { path: 'nested', type: 'directory' },
    { path: 'nested/data.bin', type: 'file', base64: 'AAEC/w==' },
    { path: 'nested/empty', type: 'directory' },
  ])
  await assert.rejects(
    source.runContext('setStorageState', {
      contextId: context.id,
      state: {
        cookies: [],
        origins: [{ origin, localStorage: [], opfs: [{ path: '../outside', type: 'directory' }] }],
      },
    }),
    /Invalid native origin storage/,
  )
  assert.equal(
    (await context.cookies()).find((cookie) => cookie.name === 'auth-cookie')?.value,
    'native',
  )
  phase = 'restore before first public author script'
  const restored = await browser.newContext({ storageState: statePath })
  assert.equal(requests, baselineRequests)
  assert.equal(restored.pages().length, 0)
  assert.equal(
    (await restored.cookies()).find((cookie) => cookie.name === 'auth-cookie')?.httpOnly,
    true,
  )
  const restoredPage = await restored.newPage()
  await restoredPage.goto(`${origin}/author`)
  assert.equal(await restoredPage.locator('html').getAttribute('data-restored'), 'native')
  const record = z
    .object({
      date: z.string(),
      bigint: z.string(),
      cyclic: z.boolean(),
      map: z.boolean(),
      set: z.boolean(),
      shared: z.boolean(),
      bytes: z.array(z.number()),
      blob: z.string(),
      mime: z.string(),
    })
    .parse(
      await restoredPage.evaluate(`(async()=>{
    const opening=indexedDB.open('suitecut-auth');const db=await new Promise((resolve,reject)=>{opening.onsuccess=()=>resolve(opening.result);opening.onerror=()=>reject(opening.error);});
    try{const r=db.transaction('tokens').objectStore('tokens').get('token');const value=await new Promise((resolve,reject)=>{r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});
    return{date:value.date.toISOString(),bigint:String(value.bigint),cyclic:value.self===value,map:value.map.get(value)===value,set:value.set.has('native'),shared:value.view.buffer===value.buffer,bytes:Array.from(value.view),blob:await value.blob.text(),mime:value.blob.type};}finally{db.close();}
  })()`),
    )
  assert.deepEqual(record, {
    date: '2026-01-02T03:04:05.000Z',
    bigint: '42',
    cyclic: true,
    map: true,
    set: true,
    shared: true,
    bytes: [255, 17],
    blob: 'native blob',
    mime: 'text/plain',
  })
  assert.deepEqual(
    await restoredPage.evaluate(
      `(async()=>{const root=await navigator.storage.getDirectory();const nested=await root.getDirectoryHandle('nested');return{text:await(await(await root.getFileHandle('hello.txt')).getFile()).text(),bytes:Array.from(new Uint8Array(await(await(await nested.getFileHandle('data.bin')).getFile()).arrayBuffer())),empty:await Array.fromAsync((await nested.getDirectoryHandle('empty')).keys())};})()`,
    ),
    { text: 'Hello, world!', bytes: [0, 1, 2, 255], empty: [] },
  )
  await restoredPage.goto(`${otherOrigin}/author`)
  assert.equal(
    await restoredPage.evaluate("localStorage.getItem('other-origin')"),
    'retained after page close',
  )
  phase = 'chunked Unicode replacement clears omitted prior origins and IndexedDB'
  const replacement = {
    cookies: [],
    origins: [{ origin, localStorage: [{ name: 'unicode', value: '🧪'.repeat(30000) }] }],
  }
  await restoredPage.waitForLoadState('networkidle')
  const beforeReplacementRequests = requests
  await restored.setStorageState(replacement)
  assert.equal(requests, beforeReplacementRequests)
  assert.equal(await restoredPage.evaluate('localStorage.length'), 0)
  await restoredPage.goto(`${origin}/author`)
  assert.equal(
    await restoredPage.evaluate("localStorage.getItem('unicode')"),
    replacement.origins[0].localStorage[0].value,
  )
  assert.equal(await restoredPage.evaluate("localStorage.getItem('auth')"), null)
  assert.deepEqual(await restoredPage.evaluate('(async()=>await indexedDB.databases())()'), [])
  assert.deepEqual(await restored.cookies(), [])
  assert.deepEqual(
    await restoredPage.evaluate(
      '(async()=>await Array.fromAsync((await navigator.storage.getDirectory()).keys()))()',
    ),
    [],
  )
  phase = 'native owned page inherits state and closes its real context'
  const owned = await browser.newPage({ storageState: state })
  await owned.goto(`${origin}/author`)
  assert.equal(await owned.locator('html').getAttribute('data-restored'), 'native')
  const ownedContext = owned.context()
  await owned.close()
  assert(!browser.contexts().includes(ownedContext))
  phase = 'native cancellation and context closure release utility leases'
  await assert.rejects(
    source.runContext('storageState', { contextId: context.id, indexedDB: true, timeoutMs: 1 }),
    /timed out|Timeout/,
  )
  const closingContext = await browser.newContext({ storageState: state })
  const pending = closingContext.storageState({ indexedDB: true })
  const rejectedOnClose = assert.rejects(pending, /clos|cancel/i)
  await closingContext.close()
  await rejectedOnClose
  phase = 'repeated actual storage/utility ownership'
  for (let index = 0; index < 20; index++) {
    const cycle = await browser.newContext({ storageState: replacement })
    const snapshot = await cycle.storageState()
    assert.equal(
      snapshot.origins[0]?.localStorage[0]?.value,
      replacement.origins[0].localStorage[0].value,
    )
    await cycle.close()
  }
  await Promise.all([context.close(), restored.close()])
  phase = 'empty origin omission'
  const emptyContext = await browser.newContext()
  const emptyPage = await emptyContext.newPage()
  await emptyPage.goto(`${origin}/author`)
  await emptyPage.waitForLoadState('networkidle')
  await emptyPage.close()
  assert.deepEqual(await emptyContext.storageState(), { cookies: [], origins: [] })
  assert.deepEqual(await emptyContext.storageState({ indexedDB: true, opfs: true }), {
    cookies: [],
    origins: [],
  })
  await emptyContext.close()
  phase = 'native context default timeout and close-reason snapshots'
  const deadlineContext = await browser.newContext()
  deadlineContext.setDefaultTimeout(20)
  await assert.rejects(deadlineContext.waitForEvent('page'), { name: 'TimeoutError' })
  await deadlineContext.close()
  for (const contextClosure of [false, true]) {
    const reasonContext = await browser.newContext()
    const pages = await Promise.all(
      Array.from({ length: contextClosure ? 2 : 1 }, () => reasonContext.newPage()),
    )
    const observed = pages.map((page) =>
      page.waitForEvent('console', {
        predicate: (message) => message.text() === 'native-close-observed-2',
        timeout: 10000,
      }),
    )
    const snapshots = Promise.allSettled(
      pages.map((page) =>
        page.source.runAutomation('queryEvaluate', {
          selector: 'body',
          all: true,
          expectation: true,
          isNot: false,
          function:
            "nodes=>{globalThis.__nativeCloseObservation=(globalThis.__nativeCloseObservation||0)+1;console.log('native-close-observed-'+globalThis.__nativeCloseObservation);return{matches:false,actual:['primitive',nodes[0].tagName],count:nodes.length};}",
          timeoutMs: 30000,
        }),
      ),
    )
    await Promise.all(observed)
    const reason = contextClosure ? 'native context close reason' : 'native page close reason'
    if (contextClosure) await reasonContext.close({ reason })
    else await pages[0].close({ reason })
    for (const result of await snapshots) {
      if (result.status === 'rejected')
        throw result.reason instanceof Error ? result.reason : new Error(String(result.reason))
      const snapshot = z
        .object({
          closedReason: z.string(),
          result: z.object({ value: z.object({ actual: z.array(z.string()) }) }),
        })
        .parse(result.value)
      assert.equal(snapshot.closedReason, reason)
      assert.deepEqual(snapshot.result.value.actual, ['primitive', 'BODY'])
    }
    await reasonContext.close()
  }
  const zero = z
    .object({
      storageOperations: z.number(),
      storageClosingOperations: z.number(),
      storageRetainedBytes: z.number(),
      storageUploads: z.number(),
      storageUploadBytes: z.number(),
      utilityBrowsers: z.number(),
      utilityBrowserLeases: z.number(),
      utilityEvaluations: z.number(),
      utilityAcquisitions: z.number(),
      utilityDeferredResults: z.number(),
      utilityDeferredResultBytes: z.number(),
    })
    .parse(await source.runContext('list'))
  assert(
    Object.values(zero).every((value) => value === 0),
    JSON.stringify(zero),
  )
  const actualRequests = z
    .array(z.object({ url: z.string(), referer: z.string().optional() }))
    .parse(requestRecords)
  assert(
    actualRequests.every(
      (entry) =>
        entry.url === '/author' ||
        (entry.url === '/favicon.ico' && entry.referer?.endsWith('/author')),
    ),
    JSON.stringify(requestRecords),
  )
  await source.close()
  assert.deepEqual(source.processExit(), { code: 0, signal: null })
  assert.doesNotMatch(diagnostics.join(''), /stack smashing detected|Check failed:/)
  assert.equal(await hash(), executableSha256)
} catch (error) {
  capturedError = error instanceof Error ? error : new Error(String(error))
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ phase, error: String(error) }, null, 2),
  )
} finally {
  const cleanup = await Promise.allSettled([
    source?.close(),
    new Promise((done, reject) => server.close((error) => (error ? reject(error) : done()))),
  ])
  const failure = cleanup.find((result) => result.status === 'rejected')
  if (!capturedError && failure?.status === 'rejected')
    capturedError = new Error(String(failure.reason))
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join(''))
  await writeFile(resolve(directory, 'requests.json'), JSON.stringify(requestRecords, null, 2))
}
if (capturedError) throw capturedError
const report = {
  started,
  completed: new Date().toISOString(),
  executableSha256,
  evidence: [
    'actual CEF cookies',
    'closed-page origin storage',
    'no external/author traffic during snapshot',
    'restore before first author script',
    'IndexedDB schema and platform graph values',
    'public IndexedDB record tags and inline key omission',
    'actual OPFS hierarchy/binary capture and replacement',
    'native OPFS traversal admission and empty-origin omission',
    'bounded Unicode command chunks',
    'state replacement',
    'native owned-page context lifetime',
    'native timeout and close cancellation',
    '20 utility ownership cycles',
    'all native storage/utility registries zero',
    'graceful native helper exit',
  ],
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
console.log(JSON.stringify(report, null, 2))
