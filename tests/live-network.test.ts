import { once } from 'node:events'
import { createConnection, createServer, type Socket } from 'node:net'
import { setTimeout as delay } from 'node:timers/promises'

import { expect, it } from 'vitest'

import { createLiveNetwork } from '../src/live-network.js'

it('detects real socket backpressure, bounds buffering and closes blocked transports', async () => {
  let receiver: Socket | undefined
  const server = createServer((socket) => {
    receiver = socket
    socket.pause()
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  if (!address || typeof address === 'string') throw Error('Missing TCP address')
  const network = await createLiveNetwork(`rtmp://127.0.0.1:${address.port}/live/test`)
  const client = createConnection({ host: '127.0.0.1', port: Number(new URL(network.url).port) })
  client.on('error', () => undefined)
  const abort = new AbortController()
  const pumping = (async () => {
    await once(client, 'connect')
    const chunk = Buffer.alloc(65536)
    while (!abort.signal.aborted) {
      if (!client.write(chunk)) await once(client, 'drain', { signal: abort.signal })
    }
  })().catch(() => undefined)
  try {
    let sample = network.sample()
    for (let i = 0; i < 80 && sample.blockedMs < 500; i++) {
      await delay(100)
      sample = network.sample()
    }
    expect(sample.blockedMs).toBeGreaterThanOrEqual(500)
    expect(sample.queuedBytes).toBeLessThanOrEqual(131072)
    expect(sample.queuedBytes).toBeGreaterThan(0)
    const closed = new Promise<void>((resolve) => client.once('close', () => resolve()))
    client.resume()
    abort.abort()
    await network.close()
    await pumping
    await closed
    expect(client.destroyed || client.readableEnded).toBe(true)
  } finally {
    abort.abort()
    client.destroy()
    receiver?.destroy()
    await network.close()
    await new Promise<void>((resolve) => server.close(() => resolve()))
  }
}, 15000)
