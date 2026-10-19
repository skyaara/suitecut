// SPDX-License-Identifier: MIT
/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript verification needs JSDoc types. */
/* global localStorage */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const binarySHA256 = await hash()
const root = await mkdtemp(join(tmpdir(), 'suitecut-profile-proof-'))
const profile = join(root, 'selected-profile')
await mkdir(profile)
await writeFile(join(profile, 'original-data'), 'preserve these exact bytes')
const server = createServer((request, response) => {
  response.writeHead(200, { 'content-type': 'text/html' })
  response.end('<title>Native profile</title>')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const port = z.object({ port: z.number() }).parse(server.address()).port
const origin = `http://localhost:${port}`
/**
 * Native sources owned by this proof.
 * @type {Set<import('../dist/native-browser.js').NativeBrowser>}
 */
const sources = new Set()
let stderr = ''
const launch = async (persistent = true) => {
  const source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    ...(persistent ? { userDataDir: profile } : {}),
    onDiagnostic: (message) => {
      stderr += message
    },
  })
  sources.add(source)
  return source
}
/**
 * Read actual state from this native browser document.
 * @param {import('../dist/native.js').NativePage} page
 */
const readState = async (page) =>
  z
    .object({
      local: z.string().nullable(),
      session: z.string().nullable(),
      cookies: z.array(z.string()),
      idb: z.string().nullable(),
      opfs: z.array(z.number()).nullable(),
    })
    .parse(
      await page.evaluate(`(async()=>{
    const names=await indexedDB.databases();let idb=null;
    if(names.some(item=>item.name==='native-profile')){
      const request=indexedDB.open('native-profile');
      const db=await new Promise((resolve,reject)=>{request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)});
      const read=db.transaction('store').objectStore('store').get('value');
      idb=await new Promise((resolve,reject)=>{read.onsuccess=()=>resolve(read.result??null);read.onerror=()=>reject(read.error)});db.close();
    }
    let opfs=null;
    try{const root=await navigator.storage.getDirectory();const handle=await root.getFileHandle('native-bytes');opfs=Array.from(new Uint8Array(await(await handle.getFile()).arrayBuffer()));}
    catch(error){if(error.name!=='NotFoundError')throw error;}
    return {local:localStorage.getItem('profile'),session:sessionStorage.getItem('session'),
      cookies:document.cookie?document.cookie.split('; ').sort():[],idb,opfs};
  })()`),
    )
const expected = {
  local: 'native-\ud800',
  session: null,
  cookies: ['persistent=actual', 'session=actual'],
  idb: 'genuine IndexedDB',
  opfs: [0, 255, 128, 32],
}
const empty = { local: null, session: null, cookies: [], idb: null, opfs: null }
const checks = []
try {
  const first = await launch()
  const page = createNativePage(first)
  await page.goto(origin)
  await page.evaluate(`(async()=>{
    localStorage.setItem('profile','native-'+String.fromCharCode(0xd800));sessionStorage.setItem('session','tab-only');
    document.cookie='persistent=actual; Max-Age=3600; Path=/';document.cookie='session=actual; Path=/';
    const request=indexedDB.open('native-profile',1);request.onupgradeneeded=()=>request.result.createObjectStore('store');
    const db=await new Promise((resolve,reject)=>{request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)});
    const tx=db.transaction('store','readwrite');tx.objectStore('store').put('genuine IndexedDB','value');
    await new Promise((resolve,reject)=>{tx.oncomplete=()=>resolve();tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error)});db.close();
    const root=await navigator.storage.getDirectory();const handle=await root.getFileHandle('native-bytes',{create:true});
    const writer=await handle.createWritable();await writer.write(new Uint8Array([0,255,128,32]));await writer.close();
  })()`)
  await assert.rejects(launch(), /persistent profile is already in use/u)
  assert.equal(await page.title(), 'Native profile')
  assert.equal(await readFile(join(profile, 'original-data'), 'utf8'), 'preserve these exact bytes')
  checks.push(
    'actual native process lock rejects overlapping launches without changing existing data or browser',
  )
  assert(
    typeof first.runContext === 'function' &&
      typeof first.pageSource === 'function' &&
      typeof first.onContextEvent === 'function',
  )
  const contexts = new NativeBrowserContexts(first)
  const isolated = await contexts.newContext()
  const isolatedPage = await isolated.newPage()
  await isolatedPage.goto(origin)
  assert.deepEqual(await readState(isolatedPage), empty)
  await isolatedPage.evaluate(() => localStorage.setItem('profile', 'isolated context'))
  assert.equal((await readState(page)).local, expected.local)
  await isolated.close()
  checks.push(
    'ordinary contexts inside a persistent helper use genuine isolated in-memory partitions',
  )
  await first.close()
  sources.delete(first)
  assert.equal(await readFile(join(profile, 'original-data'), 'utf8'), 'preserve these exact bytes')
  const files = await readdir(profile)
  assert(files.includes('Default'))
  const second = await launch()
  const restoredPage = createNativePage(second)
  await restoredPage.goto(origin)
  const restored = await readState(restoredPage)
  assert.deepEqual(restored, expected)
  checks.push(
    'actual disk profile restores cookies, exact UTF16 localStorage, IndexedDB and OPFS across helper restart; new tab sessionStorage is empty',
  )
  await second.close()
  sources.delete(second)
  const temporary = await launch(false)
  const temporaryPage = createNativePage(temporary)
  await temporaryPage.goto(origin)
  assert.deepEqual(await readState(temporaryPage), empty)
  await temporary.close()
  sources.delete(temporary)
  checks.push('ordinary temporary helper remains isolated from the selected persistent profile')
  assert.equal(await readFile(join(profile, 'original-data'), 'utf8'), 'preserve these exact bytes')
  assert.equal(await hash(), binarySHA256)
  assert.equal(stderr, '')
  await mkdir('.suitecut/native-profile', { recursive: true })
  await writeFile(
    '.suitecut/native-profile/report.json',
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: first.cefVersion,
        checks,
        restored,
        files,
        stderr,
      },
      null,
      2,
    ) + '\n',
  )
  console.log('Native profile verified: .suitecut/native-profile/report.json')
} finally {
  await Promise.all([...sources].map((source) => source.close()))
  server.closeAllConnections()
  await new Promise((resolve, reject) =>
    server.close((error) => (error ? reject(error) : resolve(undefined))),
  )
  await rm(root, { recursive: true, force: true })
}
