// SPDX-License-Identifier: MIT
// Local application and network barriers. No internet services or fake SDK calls.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { createServer } from 'node:http'
import { URL } from 'node:url'

export const inventory = Buffer.from('sku,quantity\nkeyboard-λ,3\n' + '漢字🙂\n'.repeat(4096))
export const digest = (bytes) => createHash('sha256').update(bytes).digest('hex')

export function deferred() {
  let resolve, reject
  const promise = new Promise((yes, no) => {
    resolve = yes
    reject = no
  })
  // Race subjects may reject before the test awaits them; keep the original result.
  promise.catch(() => {})
  return { promise, resolve, reject }
}

const shell = (body) =>
  `<!doctype html><meta charset="utf-8"><style>body{font:16px system-ui;margin:20px}input,button,select{margin:8px;padding:8px}iframe{width:500px;height:200px}output{display:block;white-space:pre-wrap}</style>${body}`
const app = shell(`<h1>Warehouse operations</h1>
<form id=login><label>Email<input id=email autocomplete=username></label><label>Password<input id=password type=password autocomplete=current-password></label><button>Sign in</button></form>
<output id=identity>Guest</output><button id=logout>Sign out</button>
<label>Quantity<input id=quantity type=number value=1 min=1 max=20></label><button id=checkout>Place order</button><output id=order></output>
<label>Inventory<input id=inventory type=file></label><button id=upload>Upload inventory</button><output id=uploaded></output>
<a id=download href=/api/export>Export inventory</a><button id=popup>Open approval</button><output id=approved></output>
<script>
globalThis.trusted=[];for(const type of ['click','input','submit'])document.addEventListener(type,e=>trusted.push({type,trusted:e.isTrusted}));
async function api(path,body){const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:{'content-type':'application/json'},body:body===undefined?undefined:JSON.stringify(body)});const data=await r.json();if(!r.ok)throw Error(data.error);return data}
async function who(){const s=await api('/api/session');document.querySelector('#identity').textContent=s.email||'Guest'}
document.querySelector('#login').onsubmit=async e=>{e.preventDefault();try{await api('/api/login',{email:email.value,password:password.value});await who()}catch(e){identity.textContent=e.message}};
document.querySelector('#logout').onclick=async()=>{await api('/api/logout',{});await who()};
document.querySelector('#checkout').onclick=async()=>{try{const s=await api('/api/order',{quantity:Number(quantity.value),key:'order-once'});order.textContent=JSON.stringify(s)}catch(e){order.textContent=e.message}};
document.querySelector('#upload').onclick=async()=>{const f=document.querySelector('#inventory').files[0];if(!f)return;const r=await fetch('/api/upload',{method:'POST',body:f});uploaded.textContent=JSON.stringify(await r.json())};
document.querySelector('#popup').onclick=()=>window.open('/approval','approval');window.addEventListener('message',e=>{if(e.origin===location.origin&&e.data==='approved')approved.textContent='Approved'});who();
</script>`)
const editor =
  shell(`<h1>Support composer</h1><label>Subject<input id=subject></label><div id=editor contenteditable role=textbox aria-label="Message">Draft</div><button id=save>Save draft</button><output id=saved></output><section id=shadow></section>
<script>const root=document.querySelector('#shadow').attachShadow({mode:'open'});root.innerHTML='<label>Tag<input id=tag></label><button id=tag-save>Apply tag</button><output id=tagged></output>';root.querySelector('button').onclick=()=>root.querySelector('output').textContent=root.querySelector('input').value;save.onclick=()=>{const data={subject:subject.value,body:editor.textContent};localStorage.setItem('draft',JSON.stringify(data));saved.textContent=JSON.stringify(data)};</script>`)
const firstAuthor = `<script>globalThis.firstAuthor={seed:globalThis.regressionSeed??null,early:document.readyState==='loading',leaked:'nativeSnapshot' in globalThis,xhrBlocked:false};try{const x=new XMLHttpRequest();x.open('GET','/plain',false);x.send()}catch(e){firstAuthor.xhrBlocked=true}</script>`

export async function createFixture() {
  const audit = [],
    gates = new Map(),
    sessions = new Map(),
    orders = new Map(),
    sockets = new Set()
  let nextSession = 0,
    nextOrder = 0,
    closing = false
  const gate = (name) => {
    if (!gates.has(name))
      gates.set(name, { entered: deferred(), release: deferred(), aborted: deferred() })
    return gates.get(name)
  }
  let origin, cross
  const server = createServer(async (request, response) => {
    const url = new URL(request.url, origin)
    const row = {
      path: url.pathname,
      query: url.search,
      method: request.method,
      cookie: request.headers.cookie ?? '',
      ended: false,
      aborted: false,
    }
    audit.push(row)
    response.on('finish', () => {
      row.ended = true
    })
    response.on('close', () => {
      if (!row.ended) row.aborted = true
    })
    response.setHeader('Cache-Control', 'no-store')
    const json = (value, status = 200) => {
      response.writeHead(status, { 'Content-Type': 'application/json' })
      response.end(JSON.stringify(value))
    }
    try {
      if (url.pathname.startsWith('/gate/')) {
        const name = url.pathname.slice(6),
          g = gate(name)
        g.entered.resolve(row)
        response.on('close', () => {
          if (!row.ended) g.aborted.resolve(row)
        })
        if (url.searchParams.has('headers')) {
          response.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' })
          response.write('prefix:')
        }
        const body = await g.release.promise
        if (!response.destroyed) response.end(body ?? 'released:' + name)
        return
      }
      if (url.pathname === '/redirect') {
        response.writeHead(302, { Location: '/app' })
        response.end()
        return
      }
      if (url.pathname === '/api/upload') {
        const chunks = []
        let bytes = 0
        for await (const chunk of request) {
          bytes += chunk.length
          assert(bytes <= 2 * 1024 * 1024)
          chunks.push(chunk)
        }
        const body = Buffer.concat(chunks)
        json({ bytes, sha256: digest(body) })
        return
      }
      if (url.pathname === '/api/export') {
        response.writeHead(200, {
          'Content-Type': 'application/octet-stream',
          'Content-Disposition': "attachment; filename*=UTF-8''inventory-%CE%BB.csv",
        })
        response.end(inventory)
        return
      }
      if (url.pathname === '/api/binary') {
        response.writeHead(200, { 'Content-Type': 'application/octet-stream' })
        response.end(inventory)
        return
      }
      if (url.pathname.startsWith('/api/')) {
        let raw = ''
        for await (const chunk of request) {
          raw += chunk
          assert(raw.length < 16384)
        }
        const body = raw ? JSON.parse(raw) : {},
          sid = /(?:^|;\s*)session=([^;]+)/.exec(request.headers.cookie ?? '')?.[1],
          email = sessions.get(sid)
        if (url.pathname === '/api/login') {
          if (!body.email?.includes('@') || body.password !== 'local-fixture-password') {
            json({ error: 'Invalid credentials' }, 401)
            return
          }
          const id = 's' + ++nextSession
          sessions.set(id, body.email)
          response.setHeader('Set-Cookie', `session=${id}; HttpOnly; SameSite=Lax; Path=/`)
          json({ email: body.email })
          return
        }
        if (url.pathname === '/api/session') {
          json({ email: email ?? null })
          return
        }
        if (url.pathname === '/api/logout') {
          sessions.delete(sid)
          response.setHeader('Set-Cookie', 'session=; Max-Age=0; Path=/')
          json({ ok: true })
          return
        }
        if (url.pathname === '/api/order') {
          if (!email) {
            json({ error: 'Sign in required' }, 401)
            return
          }
          if (!Number.isInteger(body.quantity) || body.quantity < 1 || body.quantity > 20) {
            json({ error: 'Quantity must be 1–20' }, 422)
            return
          }
          const key = email + ':' + body.key
          if (!orders.has(key))
            orders.set(key, {
              id: ++nextOrder,
              email,
              quantity: body.quantity,
              total: body.quantity * 2499,
            })
          json(orders.get(key))
          return
        }
        json({ error: 'Unknown API' }, 404)
        return
      }
      response.setHeader('Content-Type', 'text/html; charset=utf-8')
      if (url.pathname.startsWith('/restricted')) {
        response.setHeader('Permissions-Policy', 'sync-xhr=()')
        let children = ''
        if (url.pathname === '/restricted')
          children = `<iframe id=local src=/restricted-child></iframe><iframe id=remote src="${cross}/restricted-remote"></iframe>`
        if (url.pathname === '/restricted-remote')
          children = '<iframe id=nested src=/restricted-child></iframe>'
        response.end(
          shell(
            firstAuthor +
              '<input id=field><button id=send onclick="globalThis.sent=document.querySelector(\'#field\').value">Send</button>' +
              children,
          ),
        )
        return
      }
      if (url.pathname === '/app') {
        response.end(app)
        return
      }
      if (url.pathname === '/editor') {
        response.end(editor)
        return
      }
      if (url.pathname === '/approval') {
        response.end(
          shell(
            '<button id=approve onclick="opener.postMessage(\'approved\',location.origin);window.close()">Approve order</button>',
          ),
        )
        return
      }
      if (url.pathname === '/worker.js') {
        response.setHeader('Content-Type', 'text/javascript')
        response.end(
          "self.onmessage=e=>{if(e.data==='hold')return;postMessage({generation:e.data.generation,value:e.data.value*2})}",
        )
        return
      }
      if (url.pathname === '/sw.js') {
        response.setHeader('Content-Type', 'text/javascript')
        response.end(
          "self.addEventListener('install',e=>e.waitUntil(self.skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));self.addEventListener('fetch',e=>{if(new URL(e.request.url).pathname==='/cached')e.respondWith(new Response('offline-cached-v1'))})",
        )
        return
      }
      if (url.pathname === '/search') {
        response.end(
          shell(
            `<input id=query><button id=search>Search</button><output id=result></output><script>let version=0;search.onclick=async()=>{const current=++version,q=query.value;const value=await(await fetch('/gate/search-'+q)).text();if(current===version)result.textContent=value}</script>`,
          ),
        )
        return
      }
      if (url.pathname === '/frame-host') {
        response.end(
          shell(
            `<input id=root value=untouched><iframe id=child src="${cross}/child"></iframe><iframe id=sibling src=/child></iframe>`,
          ),
        )
        return
      }
      if (url.pathname === '/child') {
        response.end(
          shell(
            '<input id=field value=child><button id=send onclick="globalThis.sent=document.querySelector(\'#field\').value">Send</button>',
          ),
        )
        return
      }
      if (url.pathname === '/media') {
        response.end(
          shell(
            `<canvas width=640 height=360></canvas><button id=audio>Enable audio</button><script>let n=0;const c=document.querySelector('canvas').getContext('2d');function draw(){c.fillStyle=n++%2?'#ee2244':'#2244ee';c.fillRect(0,0,640,360);globalThis.paints=n;requestAnimationFrame(draw)}draw();audio.onclick=async()=>{const a=new AudioContext();const o=a.createOscillator(),g=a.createGain();g.gain.value=.1;o.connect(g).connect(a.destination);o.start();await a.resume();globalThis.audioReady=a.state==='running'};</script>`,
          ),
        )
        return
      }
      response.end(
        shell(
          '<title>Local document ' +
            url.pathname.replace(/[^\w/-]/g, '') +
            '</title><h1 id=title>Document</h1><input id=field><button id=button onclick="globalThis.clicks=(globalThis.clicks||0)+1">Save</button>',
        ),
      )
    } catch (error) {
      if (!closing) row.error = String(error)
      if (!response.headersSent) response.writeHead(500)
      response.end('Fixture error')
    }
  })
  server.maxConnections = 64
  server.on('connection', (socket) => {
    sockets.add(socket)
    socket.on('close', () => sockets.delete(socket))
  })
  await new Promise((done) => server.listen(0, '127.0.0.1', done))
  origin = `http://127.0.0.1:${server.address().port}`
  cross = `http://localhost:${server.address().port}`
  return {
    origin,
    cross,
    audit,
    gate,
    orders,
    async close() {
      closing = true
      for (const g of gates.values()) g.release.resolve('fixture teardown')
      for (const socket of sockets) socket.destroy()
      await new Promise((done) => server.close(done))
    },
  }
}
