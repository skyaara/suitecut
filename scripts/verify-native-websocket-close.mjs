// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
/** @type {Map<string, import('node:stream').Duplex>} */
const peers = new Map()
const server = createServer((_request, response) => response.end('<title>WebSocket close</title>'))
/**
 * Encode one bounded server WebSocket frame.
 * @param {number} opcode
 * @param {Buffer} bytes
 */
function frame(opcode, bytes) {
  assert(bytes.length < 126)
  return Buffer.concat([Buffer.from([128 | opcode, bytes.length]), bytes])
}
server.on('upgrade', (request, socket) => {
  const key = z.string().parse(request.headers['sec-websocket-key'])
  const path = z.string().parse(request.url)
  const accept = createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64')
  socket.write(
    `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
  )
  peers.set(path, socket)
  socket.on('error', () => undefined)
  socket.on('data', (chunk) => {
    const bytes = z.instanceof(Buffer).parse(chunk)
    if (((bytes[0] ?? 0) & 15) === 8) socket.end(frame(8, Buffer.from([3, 232])))
  })
})
await new Promise((accept) => server.listen(0, '127.0.0.1', () => accept(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const origin = `http://127.0.0.1:${address.port}`
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  headless: true,
  onDiagnostic(message) {
    stderr = (stderr + message).slice(-1048576)
  },
})
const page = createNativePage(source)
/**
 * Open a genuine page socket and observe its public identity.
 * @param {string} name
 */
async function open(name) {
  const created = page.waitForEvent('websocket', { timeout: 2000 })
  await page.evaluate(
    `globalThis[${JSON.stringify(name)}]=new WebSocket(${JSON.stringify(origin.replace('http:', 'ws:') + '/' + name)});new Promise((resolve,reject)=>{globalThis[${JSON.stringify(name)}].onopen=()=>resolve();globalThis[${JSON.stringify(name)}].onerror=()=>reject(new Error('Actual socket open failed'))})`,
  )
  const socket = await created
  assert.equal(socket.url(), origin.replace('http:', 'ws:') + '/' + name)
  assert.equal(socket.isClosed(), false)
  return socket
}
/**
 * Close the actual browser socket and await its native event.
 * @param {string} name
 * @param {import('../dist/native.js').NativeWebSocket} socket
 */
async function close(name, socket) {
  const closed = socket.waitForEvent('close', { timeout: 2000 })
  await page.evaluate(`globalThis[${JSON.stringify(name)}].close(1000,'actual-close')`)
  assert.equal(await closed, socket)
  assert.equal(socket.isClosed(), true)
}
try {
  await page.goto(origin)
  const early = await open('early')
  /** @type {string[]} */
  const order = []
  early.on('close', () => order.push('close'))
  const rejected = assert.rejects(
    early.waitForEvent('framereceived', { timeout: 2000 }).catch((error) => {
      order.push('reject')
      throw error
    }),
    { message: 'WebSocket closed' },
  )
  await close('early', early)
  await rejected
  assert.deepEqual(order, ['close', 'reject'])
  assert.equal(await page.evaluate('6*7'), 42)

  const first = await open('framefirst')
  const received = first.waitForEvent('framereceived', { timeout: 2000 })
  const closed = first.waitForEvent('close', { timeout: 2000 })
  const peer = peers.get('/framefirst')
  assert(peer)
  peer.write(
    Buffer.concat([
      frame(1, Buffer.from('actual-frame-before-close')),
      frame(8, Buffer.from([3, 232])),
    ]),
  )
  assert.equal((await received).payload, 'actual-frame-before-close')
  assert.equal(await closed, first)

  const a = await open('isolatedA'),
    b = await open('isolatedB')
  let settled = false
  const isolated = b.waitForEvent('framereceived', { timeout: 2000 }).then((value) => {
    settled = true
    return value
  })
  await close('isolatedA', a)
  assert.equal(settled, false)
  assert.equal(b.isClosed(), false)
  const livePeer = peers.get('/isolatedB')
  assert(livePeer)
  livePeer.write(frame(1, Buffer.from('actual-isolated-frame')))
  assert.equal((await isolated).payload, 'actual-isolated-frame')
  await close('isolatedB', b)

  const already = await open('already')
  await close('already', already)
  await assert.rejects(already.waitForEvent('socketerror', { timeout: 2000 }), {
    message: 'WebSocket closed',
  })

  const deadline = await open('deadline')
  await assert.rejects(
    deadline.waitForEvent('framereceived', { timeout: 100 }),
    /deadline|timed out/iu,
  )
  assert.equal(deadline.isClosed(), false)
  const controller = new AbortController(),
    reason = new Error('authored-socket-abort')
  const canceled = assert.rejects(
    deadline.waitForEvent('framereceived', { timeout: 2000, signal: controller.signal }),
    (error) => error === reason,
  )
  controller.abort(reason)
  await canceled
  await close('deadline', deadline)
  assert.equal(await page.evaluate('6*7'), 42)
} finally {
  await source.close()
  await source.processClosed
  for (const socket of peers.values()) socket.destroy()
  await new Promise((accept) => server.close(accept))
}
assert.equal(source.processExit?.()?.code, 0)
assert.equal(stderr, '')
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-websocket-close',
)
await mkdir(directory, { recursive: true })
await writeFile(
  resolve(directory, 'report.json'),
  JSON.stringify(
    {
      completed: true,
      checks: [
        'close event precedes rejection of pending frame wait',
        'last frame is delivered before close',
        'closing one socket preserves another socket and its pending frame',
        'already closed socket rejects a new event wait immediately',
        'deadline and explicit abort preserve the live socket and exact abort reason',
      ],
      exit: source.processExit?.(),
      stderr,
    },
    null,
    2,
  ) + '\n',
)
console.log('Native WebSocket early-close: five behavior groups PASS')
