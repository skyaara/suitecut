import { execFileSync } from 'node:child_process'
import { readFile, writeFile, stat } from 'node:fs/promises'
import { createServer, type Server } from 'node:http'
import { createServer as createHttpsServer } from 'node:https'
import { connect } from 'node:net'
import { resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { test as base, expect, chromium, type BrowserContext } from '@playwright/test'
import { launchEngine, type SuiteCutEngine } from '../../src/engine.js'

const test = base.extend<object, { engine: SuiteCutEngine | undefined }>({
  engine: [async ({}, use) => {
    if (process.env.COMPAT_BACKEND === 'chromium') return use(undefined)
    const engine = await launchEngine({ executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!, width: 640, height: 360, framesPerSecond: 30 })
    try { await use(engine) } finally { await engine.close() }
  }, { scope: 'worker' }],
  browser: [async ({ engine }, use) => {
    if (engine) return use(engine.browser)
    const browser = await chromium.launch({ headless: true, args: ['--site-per-process'] })
    try { await use(browser) } finally { await browser.close() }
  }, { scope: 'worker' }],
})

let server: Server, secure: Server, proxy: Server, origin: string, cross: string, tlsOrigin: string, proxyUrl: string
let proxyRequests: { url: string; authenticated: boolean }[] = []
const basic = 'Basic ' + Buffer.from('user:password').toString('base64')
const proxyBasic = 'Basic ' + Buffer.from('proxy:secret').toString('base64')
const html = `<!doctype html><button onclick="this.textContent='clicked'">Click</button><input aria-label="Name"><canvas width="640" height="250"></canvas><script>let n=0;function draw(){const c=document.querySelector('canvas').getContext('2d');c.fillStyle=n++%2?'red':'blue';c.fillRect(0,0,640,250);requestAnimationFrame(draw)}draw()</script>`
const sw = `self.addEventListener('install',e=>e.waitUntil(self.skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));self.addEventListener('fetch',e=>{if(new URL(e.request.url).pathname==='/sw-data')e.respondWith(new Response('from-worker'))});self.marker=42;`
function handle(req: Parameters<Parameters<typeof createServer>[0]>[0], res: Parameters<Parameters<typeof createServer>[0]>[1]) {
  const path = new URL(req.url!, 'http://localhost').pathname
  if (path === '/auth' && req.headers.authorization !== basic) {
    res.writeHead(401, { 'WWW-Authenticate': 'Basic realm="suitecut-local"' }); res.end('unauthorized'); return
  }
  if (path === '/sw.js') { res.writeHead(200, { 'Content-Type': 'application/javascript', 'Cache-Control': 'no-store' }); res.end(sw); return }
  if (path === '/worker.js') { res.writeHead(200, { 'Content-Type': 'application/javascript' }); res.end('self.marker=41;onmessage=e=>postMessage(e.data+1)'); return }
  if (path === '/redirect') { res.writeHead(302, { Location: '/json' }); res.end(); return }
  if (path === '/json') { res.setHeader('Content-Type', 'application/json'); res.end('{"value":42}'); return }
  if (path === '/post') { let body='';req.on('data',c=>body+=c);req.on('end',()=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify({body}))});return }
  res.setHeader('Content-Type', 'text/html');res.end(path === '/auth' ? '<h1>Authenticated</h1>' : html)
}
async function listen(s: Server) { await new Promise<void>(r => s.listen(0, '0.0.0.0', r)); const a=s.address();if(!a||typeof a==='string')throw Error('Missing address');return a.port }
test.beforeAll(async () => {
  server=createServer(handle);const port=await listen(server);origin=`http://127.0.0.1:${port}`;cross=`http://localhost:${port}`
  const certDir='/tmp/suitecut-playwright-upstream/playwright-1.62.1/tests/config/testserver'
  secure=createHttpsServer({ key: await readFile(resolve(certDir,'key.pem')), cert: await readFile(resolve(certDir,'cert.pem')) },handle)
  tlsOrigin=`https://127.0.0.1:${await listen(secure)}`
  proxy=createServer((req,res)=>{
    const authenticated=req.headers['proxy-authorization']===proxyBasic
    proxyRequests.push({url:req.url!,authenticated})
    if(req.url?.includes('auth-proxy.test')&&!authenticated){res.writeHead(407,{'Proxy-Authenticate':'Basic realm="local-proxy"'});res.end();return}
    const target=new URL(req.url!);const forwarded=server.address();if(!forwarded||typeof forwarded==='string')throw Error('Missing server')
    import('node:http').then(({request})=>{const headers={...req.headers};delete headers['proxy-authorization'];const upstream=request({hostname:'127.0.0.1',port:forwarded.port,path:target.pathname+target.search,method:req.method,headers},r=>{res.writeHead(r.statusCode!,r.headers);r.pipe(res)});upstream.on('error',e=>{res.writeHead(502);res.end(String(e))});req.pipe(upstream)})
  })
  proxy.on('connect',(req,socket,head)=>{
    const authenticated=req.headers['proxy-authorization']===proxyBasic;proxyRequests.push({url:req.url!,authenticated})
    if(req.url?.startsWith('auth-proxy.test')&&!authenticated){socket.end('HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="local-proxy"\r\nContent-Length: 0\r\n\r\n');return}
    const a=secure.address();if(!a||typeof a==='string')return socket.destroy()
    const upstream=connect(a.port,'127.0.0.1',()=>{socket.write('HTTP/1.1 200 Connection Established\r\n\r\n');if(head.length)upstream.write(head);upstream.pipe(socket);socket.pipe(upstream)})
    upstream.on('error',()=>socket.destroy());socket.on('error',()=>upstream.destroy());socket.on('close',()=>upstream.destroy())
  })
  proxyUrl=`http://127.0.0.1:${await listen(proxy)}`
})
test.afterAll(async()=>{for(const s of [proxy,secure,server]){s?.closeAllConnections();await new Promise<void>(r=>s?.close(()=>r()))}})

async function register(page: import('@playwright/test').Page) {
  await page.goto(origin)
  await page.evaluate(async()=>{await navigator.serviceWorker.register('/sw.js');await navigator.serviceWorker.ready;if(!navigator.serviceWorker.controller)await new Promise<void>(r=>navigator.serviceWorker.addEventListener('controllerchange',()=>r(),{once:true}))})
}
test('worker: dedicated Worker evaluation and messaging',async({page})=>{
  await page.goto(origin);const event=page.waitForEvent('worker');await page.evaluate(()=>{const w=new Worker('/worker.js');(window as any).worker=w})
  const worker=await event;expect(await worker.evaluate(()=> (self as any).marker)).toBe(41)
  expect(await page.evaluate(()=>new Promise(r=>{(window as any).worker.onmessage=(e:any)=>r(e.data);(window as any).worker.postMessage(8)}))).toBe(9)
})
test('worker: service worker website control and fetch',async({page})=>{await register(page);expect(await page.evaluate(()=>fetch('/sw-data').then(r=>r.text()))).toBe('from-worker')})
test('worker: context serviceworker event, enumeration and evaluation',async({page,context})=>{
  const event=context.waitForEvent('serviceworker',{timeout:5000});void event.catch(()=>{})
  await register(page);const worker=await event;expect(context.serviceWorkers()).toContain(worker);expect(await worker.evaluate(()=> (self as any).marker)).toBe(42)
})
test('worker: serviceWorkers block prevents registration',async({browser})=>{
  const c=await browser.newContext({serviceWorkers:'block'});try{const p=await c.newPage();await p.goto(origin);await p.evaluate(()=>navigator.serviceWorker.register('/sw.js').catch(()=>null));expect(await p.evaluate(()=>navigator.serviceWorker.getRegistrations().then(r=>r.length))).toBe(0)}finally{await c.close()}
})
test('oopif: cross-site frame is a separate CDP target and remains interactive',async({page,context})=>{
  await page.goto(origin);const session=await context.newCDPSession(page);const attachments:any[]=[];session.on('Target.attachedToTarget',e=>attachments.push(e));await session.send('Target.setAutoAttach',{autoAttach:true,waitForDebuggerOnStart:false,flatten:true})
  await page.evaluate(url=>{const f=document.createElement('iframe');f.src=url;document.body.append(f)},cross)
  await page.frameLocator('iframe').getByRole('button').click();await expect(page.frameLocator('iframe').getByRole('button')).toHaveText('clicked')
  await expect.poll(()=>attachments.some(e=>e.targetInfo.type==='iframe')).toBe(true)
  const frame=page.frames().find(f=>f.url().startsWith(cross))!;expect(await frame.evaluate(()=>location.origin)).toBe(cross)
  await session.detach()
})
test('oopif: cross-site navigation, detach and network routing',async({page,context})=>{
  await context.route('**/json',r=>r.fulfill({json:{value:'routed'}}));await page.goto(origin)
  await page.evaluate(url=>{const f=document.createElement('iframe');f.src=url;document.body.append(f)},cross)
  const inner=page.frameLocator('iframe');await inner.getByLabel('Name').fill('cross-site');await expect(inner.getByLabel('Name')).toHaveValue('cross-site')
  const f=page.frames().find(f=>f.url().startsWith(cross))!;expect(await f.evaluate(()=>fetch('/json').then(r=>r.json()))).toEqual({value:'routed'})
  await f.goto(origin+'/next');await inner.getByRole('button').click();await expect(inner.getByRole('button')).toHaveText('clicked')
  const detached=page.waitForEvent('framedetached');await page.locator('iframe').evaluate(f=>f.remove());await detached;expect(page.frames()).toHaveLength(1)
})
test('state: IndexedDB auth export to file and restore into a fresh context',async({page,context,browser},info)=>{
  await page.goto(origin);await page.evaluate(async()=>{
    localStorage.setItem('session','local-token');document.cookie='auth=cookie-token; SameSite=Lax; path=/'
    const db=await new Promise<IDBDatabase>((r,j)=>{const q=indexedDB.open('auth-db',3);q.onupgradeneeded=()=>{const s=q.result.createObjectStore('tokens',{keyPath:'id'});s.createIndex('byScope','scope',{unique:true})};q.onsuccess=()=>r(q.result);q.onerror=()=>j(q.error)})
    await new Promise<void>((r,j)=>{const t=db.transaction('tokens','readwrite');t.objectStore('tokens').put({id:'user',scope:'login',token:'idb-token',bytes:new Uint8Array([0,1,254,255]),created:new Date('2026-01-01')});t.oncomplete=()=>r();t.onerror=()=>j(t.error)});db.close()
  })
  const path=info.outputPath('state.json');const state=await context.storageState({path,indexedDB:true});expect(state.origins[0].indexedDB?.[0].version).toBe(3)
  await context.close();const restored=await browser.newContext({storageState:path})
  try{const p=await restored.newPage();await p.goto(origin);expect(await p.evaluate(()=>({cookie:document.cookie,local:localStorage.getItem('session')}))).toEqual({cookie:'auth=cookie-token',local:'local-token'})
    expect(await p.evaluate(()=>new Promise((r,j)=>{const q=indexedDB.open('auth-db');q.onsuccess=()=>{const db=q.result,t=db.transaction('tokens'),get=t.objectStore('tokens').index('byScope').get('login');get.onsuccess=()=>{const v=get.result;r({token:v.token,bytes:Array.from(v.bytes),created:v.created.toISOString()});db.close()};get.onerror=()=>j(get.error)}}))).toEqual({token:'idb-token',bytes:[0,1,254,255],created:'2026-01-01T00:00:00.000Z'})
    expect(await restored.storageState({indexedDB:true})).toEqual(state)
  }finally{await restored.close()}
  const isolated=await browser.newContext();try{const p=await isolated.newPage();await p.goto(origin);expect(await p.evaluate(()=>({cookie:document.cookie,local:localStorage.getItem('session')}))).toEqual({cookie:'',local:null})}finally{await isolated.close()}
})
test('state: empty IndexedDB export and restore',async({page,context,browser})=>{
  await page.goto(origin);await page.evaluate(()=>new Promise<void>((r,j)=>{const q=indexedDB.open('empty-db');q.onsuccess=()=>{q.result.close();r()};q.onerror=()=>j(q.error)}))
  const state=await context.storageState({indexedDB:true});const c=await browser.newContext({storageState:state});try{expect(await c.storageState({indexedDB:true})).toEqual(state)}finally{await c.close()}
})
for(const scheme of ['http','https']){
  test(`auth: ${scheme} origin HTTP credentials`,async({browser})=>{const c=await browser.newContext({httpCredentials:{username:'user',password:'password'},ignoreHTTPSErrors:true});try{const p=await c.newPage();expect((await p.goto((scheme==='http'?origin:tlsOrigin)+'/auth'))!.status()).toBe(200);await expect(p.getByRole('heading')).toHaveText('Authenticated')}finally{await c.close()}})
  test(`proxy: ${scheme} through local proxy plus origin auth`,async({browser})=>{proxyRequests=[];const c=await browser.newContext({proxy:{server:proxyUrl,bypass:'<-loopback>'},httpCredentials:{username:'user',password:'password'},ignoreHTTPSErrors:true});try{const p=await c.newPage();expect((await p.goto(`${scheme}://proxy.test/auth`))!.status()).toBe(200);await expect(p.getByRole('heading')).toHaveText('Authenticated');expect(proxyRequests.length).toBeGreaterThan(0)}finally{await c.close()}})
  test(`proxy: ${scheme} authenticated proxy plus origin auth`,async({browser})=>{proxyRequests=[];const c=await browser.newContext({proxy:{server:proxyUrl,username:'proxy',password:'secret',bypass:'<-loopback>'},httpCredentials:{username:'user',password:'password'},ignoreHTTPSErrors:true});try{const p=await c.newPage();expect((await p.goto(`${scheme}://auth-proxy.test/auth`))!.status()).toBe(200);await expect(p.getByRole('heading')).toHaveText('Authenticated');expect(proxyRequests.some(r=>r.authenticated)).toBe(true)}finally{await c.close()}})
}
test('auth: wrong credentials fail and a fresh context does not inherit them',async({browser})=>{for(const creds of [{username:'user',password:'wrong'},undefined]){const c=await browser.newContext({httpCredentials:creds});try{const p=await c.newPage();expect((await p.goto(origin+'/auth'))!.status()).toBe(401)}finally{await c.close()}}})
test('proxy: bypass uses the real origin without contacting the proxy',async({browser})=>{proxyRequests=[];const c=await browser.newContext({proxy:{server:proxyUrl,bypass:'127.0.0.1'}});try{const p=await c.newPage();await p.goto(origin);expect(proxyRequests).toHaveLength(0)}finally{await c.close()}})
for(const extension of ['har','har.zip'])test(`har: ${extension} captures and replays bodies, POST and redirects`,async({browser},info)=>{
  const path=info.outputPath(`capture.${extension}`);const c=await browser.newContext({recordHar:{path,content:extension==='har'?'embed':'attach'},serviceWorkers:'block'})
  try{const p=await c.newPage();await p.goto(origin);expect(await p.evaluate(()=>fetch('/redirect').then(r=>r.json()))).toEqual({value:42});expect(await p.evaluate(()=>fetch('/post',{method:'POST',body:'hello'}).then(r=>r.json()))).toEqual({body:'hello'})}finally{await c.close()}
  expect((await stat(path)).size).toBeGreaterThan(100);if(extension==='har'){const data=JSON.parse(await readFile(path,'utf8'));expect(data.log.entries.some((e:any)=>e.response.status===302)).toBe(true);expect(data.log.entries.some((e:any)=>e.response.content.text?.includes('42'))).toBe(true)}
  const replay=await browser.newContext({serviceWorkers:'block'});try{await replay.routeFromHAR(path,{notFound:'abort'});const p=await replay.newPage();await p.goto(origin);expect(await p.evaluate(()=>fetch('/redirect').then(r=>r.json()))).toEqual({value:42});expect(await p.evaluate(()=>fetch('/post',{method:'POST',body:'hello'}).then(r=>r.json()))).toEqual({body:'hello'});await expect(p.goto(origin+'/not-in-har')).rejects.toThrow()}finally{await replay.close()}
})
test('video: Playwright recordVideo creates a decodable WebM after context close',async({browser},info)=>{
  const c=await browser.newContext({viewport:{width:640,height:360},recordVideo:{dir:info.outputPath('videos'),size:{width:320,height:180}}});let video:ReturnType<import('@playwright/test').Page['video']>
  try{const p=await c.newPage();await p.goto(origin);video=p.video();expect(video).not.toBeNull();await delay(1200)}finally{await c.close()}
  const path=await video!.path();const data=JSON.parse(execFileSync('ffprobe',['-v','error','-show_streams','-show_format','-of','json',path],{encoding:'utf8'}));expect(data.streams[0]).toMatchObject({codec_name:'vp8',width:320,height:180});expect(Number(data.format.duration)).toBeGreaterThan(.5);execFileSync('ffmpeg',['-v','error','-i',path,'-f','null','-'],{timeout:15000});await writeFile(info.outputPath('ffprobe.json'),JSON.stringify(data,null,2))
})
