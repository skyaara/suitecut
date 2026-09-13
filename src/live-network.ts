import { once } from 'node:events'
import { createConnection, createServer, type Socket } from 'node:net'
import { connect, TLSSocket } from 'node:tls'

export interface NetworkSample {
  blockedMs: number
  queuedBytes: number
  sentKbps: number
}

/** Transparent bounded transport; measures socket pressure independently of encoding. */
export async function createLiveNetwork(destination: string): Promise<{
  url: string
  pressure(): { networkBlockedMs: number; networkQueuedBytes: number }
  sample(): NetworkSample
  close(): Promise<void>
}> {
  const target = new URL(destination)
  const sockets = new Set<Socket>()
  let active: Socket | undefined
  let blockedAt: number | undefined
  let sentBytes = 0
  let sampledAt = performance.now()
  let previousBytes = 0
  const server = createServer((client) => {
    // Each monitor belongs to one publishing attempt.
    if (active) {
      client.destroy()
      return
    }
    const host = target.hostname.replace(/^\[|\]$/gu, '')
    const port = Number(target.port || (target.protocol === 'rtmps:' ? 443 : 1935))
    const upstream =
      target.protocol === 'rtmps:'
        ? connect({ host, port, servername: host, rejectUnauthorized: true })
        : createConnection({ host, port })
    active = upstream
    sockets.add(client)
    sockets.add(upstream)
    const destroy = (): void => {
      client.destroy()
      upstream.destroy()
    }
    client.on('error', destroy)
    upstream.on('error', destroy)
    client.on('close', () => {
      sockets.delete(client)
      upstream.destroy()
    })
    upstream.on('close', () => {
      sockets.delete(upstream)
      client.destroy()
    })
    client.setNoDelay(true)
    upstream.setNoDelay(true)
    // At most one bounded readable chunk plus the socket high-water mark is queued.
    client.on('data', (chunk: Buffer) => {
      if (
        !upstream.write(chunk, (error) => {
          if (!error) sentBytes += chunk.length
        })
      ) {
        blockedAt ??= performance.now()
        client.pause()
      }
    })
    upstream.on('drain', () => {
      blockedAt = undefined
      client.resume()
    })
    upstream.pipe(client)
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  if (address === null || typeof address === 'string')
    throw new Error('Live network monitor could not bind.')
  const local = new URL(destination)
  local.protocol = 'rtmp:'
  local.hostname = '127.0.0.1'
  local.port = String(address.port)
  return {
    url: local.toString(),
    pressure: () => ({
      networkBlockedMs: blockedAt === undefined ? 0 : performance.now() - blockedAt,
      networkQueuedBytes: active?.writableLength ?? 0,
    }),
    sample: () => {
      const now = performance.now()
      const sentKbps = ((sentBytes - previousBytes) * 8) / Math.max(1, now - sampledAt)
      sampledAt = now
      previousBytes = sentBytes
      return {
        blockedMs: blockedAt === undefined ? 0 : now - blockedAt,
        queuedBytes: active?.writableLength ?? 0,
        sentKbps,
      }
    },
    close: async () => {
      for (const socket of sockets) {
        if (socket instanceof TLSSocket || socket.connecting || socket.destroyed) socket.destroy()
        else socket.resetAndDestroy()
      }
      if (server.listening) await new Promise<void>((resolve) => server.close(() => resolve()))
    },
  }
}
