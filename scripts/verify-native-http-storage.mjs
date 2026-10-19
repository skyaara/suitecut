// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-http-storage')
await mkdir(directory, { recursive: true })
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const executableSha256 = await hash(),
  started = new Date().toISOString(),
  checks = [],
  stderr = []
const server = createServer((request, response) => {
  if (request.url === '/cookies')
    response.setHeader('Set-Cookie', ['api_cookie=actual; HttpOnly; Path=/'])
  response.setHeader('Content-Type', 'text/html')
  response.end('<!doctype html><body>actual storage origin</body>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
let source, error, lastDiagnostics
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (message) => stderr.push(message),
  })
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext({ baseURL: origin })
  const page = await context.newPage()
  await page.goto(`${origin}/page`)
  await page.evaluate(`(async()=>{
    document.cookie='page_cookie=actual; Path=/';localStorage.setItem('actual','persistent');
    const opening=indexedDB.open('actual-api-storage',2);opening.onupgradeneeded=()=>opening.result.createObjectStore('records');
    const db=await new Promise((resolve,reject)=>{opening.onsuccess=()=>resolve(opening.result);opening.onerror=()=>reject(opening.error);});
    const tx=db.transaction('records','readwrite');tx.objectStore('records').put({actual:true,number:42},'actual-key');
    await new Promise((resolve,reject)=>{tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);});db.close();
    const root=await navigator.storage.getDirectory();await root.getDirectoryHandle('actual-empty',{create:true});
    const writer=await(await root.getFileHandle('actual.bin',{create:true})).createWritable();await writer.write(new Uint8Array([0,255,13,10]));await writer.close();return true;
  })()`)
  const api = context.request
  const response = await api.get('/cookies')
  assert.equal(response.status(), 200)
  await response.dispose()
  const config = { indexedDB: true, opfs: true }
  const expected = await context.storageState(config)
  assert.ok(expected.cookies.some((cookie) => cookie.name === 'api_cookie' && cookie.httpOnly))
  const stored = expected.origins.find((item) => item.origin === origin)
  assert.ok(stored)
  assert.deepEqual(stored.localStorage, [{ name: 'actual', value: 'persistent' }])
  assert.ok(stored.indexedDB?.length)
  assert.ok(stored.opfs?.some((item) => item.type === 'file' && item.base64 === 'AP8NCg=='))
  assert.deepEqual(await context.request.storageState(config), expected)
  assert.deepEqual(await page.request.storageState(config), expected)
  checks.push(
    'genuine context/page API storage captures cookies, localStorage, public IndexedDB and OPFS',
  )
  const file = resolve(directory, 'actual-api-storage.json')
  assert.deepEqual(await context.request.storageState({ ...config, path: file }), expected)
  assert.deepEqual(JSON.parse(await readFile(file, 'utf8')), expected)
  checks.push('actual API state file contains the owning native profile state')
  await page.close()
  assert.deepEqual(await context.request.storageState(config), expected)
  assert.deepEqual(await page.request.storageState(config), expected)
  checks.push('associated native state survives the public author page closing')
  const defaultPage = browser.defaultContext().pages()[0]
  assert.ok(defaultPage)
  await defaultPage.close()
  assert.deepEqual(await context.request.storageState(config), expected)
  checks.push('context API storage survives default page 1 closing')
  await context.close()
  await assert.rejects(api.storageState(config), /closed|disposed|context/iu)
  checks.push('actual owning context closure invalidates API storage capture')
  const diagnosticPage = await browser.defaultContext().newPage()
  lastDiagnostics = await diagnosticPage.source.runAutomation('diagnostics', {})
  await diagnosticPage.close()
} catch (caught) {
  error = caught instanceof Error ? caught : new Error('Actual API storage proof failed')
} finally {
  if (source)
    await source.close().catch((caught) => {
      error ??= caught instanceof Error ? caught : new Error('Native shutdown failed')
    })
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
  assert.equal(await hash(), executableSha256, 'Native binary changed during proof')
  await writeFile(resolve(directory, 'native-stderr.log'), stderr.join(''))
  const report = {
    ok: !error,
    started,
    ended: new Date().toISOString(),
    executableSha256,
    checks,
    diagnostics: lastDiagnostics,
    error: error?.stack,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
if (error) throw error
