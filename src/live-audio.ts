import { randomUUID } from 'node:crypto'
import { once } from 'node:events'
import { createServer, type ServerResponse } from 'node:http'

import { WebSocketServer, type WebSocket } from 'ws'

import { LIVE_RECOVERY_WINDOW_MS } from './live-congestion.js'

export const LIVE_AUDIO_DELAY_MS = 150
const SAMPLE_RATE = 48_000
const BYTES_PER_SAMPLE = 4

/** Timestamped stereo PCM with bounded retention and silence for missing samples. */
export class LiveAudioBuffer {
  private chunks: { at: number; pcm: Buffer }[] = []

  clear(): void {
    this.chunks = []
  }

  push(at: number, pcm: Buffer): void {
    const now = performance.now()
    if (
      !Number.isFinite(at) ||
      at < now - LIVE_RECOVERY_WINDOW_MS - LIVE_AUDIO_DELAY_MS ||
      at > now + 100 ||
      pcm.length !== 3840
    )
      return
    this.chunks.push({ at, pcm })
    this.chunks = this.chunks
      .filter((chunk) => chunk.at >= now - LIVE_RECOVERY_WINDOW_MS - LIVE_AUDIO_DELAY_MS)
      .slice(-510)
  }

  read(at: number, samples: number): Buffer {
    const output = Buffer.alloc(samples * BYTES_PER_SAMPLE)
    for (const chunk of this.chunks) {
      const offset = Math.round(((chunk.at - at) * SAMPLE_RATE) / 1000)
      const start = Math.max(0, offset)
      const end = Math.min(samples, offset + chunk.pcm.length / BYTES_PER_SAMPLE)
      if (end > start)
        chunk.pcm.copy(
          output,
          start * BYTES_PER_SAMPLE,
          (start - offset) * BYTES_PER_SAMPLE,
          (end - offset) * BYTES_PER_SAMPLE,
        )
    }
    this.chunks = this.chunks.filter((chunk) => chunk.at + 20 > at + (samples * 1000) / SAMPLE_RATE)
    return output
  }
}

export interface LiveAudioInput {
  url: string
  write(at: number, samples: number, signal: AbortSignal): Promise<void>
  close(): void
}

export interface LiveAudioTransport {
  url: string
  buffer: LiveAudioBuffer
  activate(generation: number): Promise<void>
  createInput(): LiveAudioInput
  close(): Promise<void>
}

/** Binary WebSocket capture and a continuous loopback HTTP input for FFmpeg. */
export async function createLiveAudioTransport(
  onPCM: (generation: number, timestamp: number, pcm: Buffer) => void,
  onEnded: (generation: number) => void,
): Promise<LiveAudioTransport> {
  let closed = false
  let generation = 0
  let ready: ((error: boolean) => void) | undefined
  const token = randomUUID()
  const buffer = new LiveAudioBuffer()
  const inputs = new Map<string, (response: ServerResponse) => void>()
  const server = createServer((request, response) => {
    const parts = request.url?.split('/') ?? []
    if (parts[1] !== token) {
      response.writeHead(404).end()
      return
    }
    if (request.method === 'GET' && parts[2] === 'config') {
      response.setHeader('Content-Type', 'application/json')
      response.end(JSON.stringify({ generation, url }))
      return
    }
    if (request.method === 'POST' && parts[2] === 'status') {
      if (Number(parts[3]) === generation) ready?.(parts[4] !== 'ok')
      response.end()
      return
    }
    if (request.method === 'GET' && parts[2] === 'input') {
      const accept = inputs.get(parts[3] ?? '')
      if (!accept) {
        response.writeHead(404).end()
        return
      }
      inputs.delete(parts[3] ?? '')
      response.writeHead(200, { 'Content-Type': 'application/octet-stream' })
      response.flushHeaders()
      accept(response)
      return
    }
    if (request.method !== 'POST') {
      response.writeHead(405).end()
      return
    }
    if (parts[2] === 'ended') {
      onEnded(Number(parts[3]))
      response.end()
      return
    }
    response.writeHead(404).end()
  })
  const sockets = new WebSocketServer({
    noServer: true,
    maxPayload: 3848,
    perMessageDeflate: false,
  })
  let activeSocket: WebSocket | undefined
  server.on('upgrade', (request, socket, head) => {
    const parts = request.url?.split('/') ?? []
    if (
      parts.length !== 4 ||
      parts[1] !== token ||
      parts[2] !== 'pcm' ||
      Number(parts[3]) !== generation
    ) {
      socket.destroy()
      return
    }
    sockets.handleUpgrade(request, socket, head, (client) => {
      activeSocket?.terminate()
      activeSocket = client
      const source = generation
      client.on('error', () => undefined)
      client.on('message', (data, binary) => {
        if (!binary || !Buffer.isBuffer(data) || data.length !== 3848) {
          client.terminate()
          return
        }
        if (source === generation) onPCM(source, data.readDoubleLE(0), data.subarray(8))
        // Acknowledge consumption so the producer can bound outstanding audio.
        if (client.bufferedAmount > 64) client.terminate()
        else client.send(Buffer.from([1]))
      })
    })
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  if (!address || typeof address === 'string')
    throw new Error('Could not start SuiteCut audio transport')
  const url = `http://127.0.0.1:${address.port}/${token}`
  return {
    url,
    buffer,
    activate: (value) => {
      activeSocket?.terminate()
      generation = value
      return new Promise<void>((resolve, reject) => {
        ready = (error) => {
          ready = undefined
          if (error) reject(new Error('SuiteCut could not capture selected tab audio'))
          else resolve()
        }
      })
    },
    createInput: () => {
      buffer.clear()
      const id = randomUUID()
      let response: ServerResponse | undefined
      let queued: Buffer[] = []
      let queuedBytes = 0
      inputs.set(id, (value) => {
        response = value
        value.on('error', () => undefined)
        for (const chunk of queued) value.write(chunk)
        queued = []
        queuedBytes = 0
      })
      return {
        url: `${url}/input/${id}`,
        write: async (at, samples, signal) => {
          const pcm = buffer.read(at, samples)
          if (!response) {
            queuedBytes += pcm.length
            if (queuedBytes > 192000)
              throw new Error('SuiteCut encoder did not connect to its audio input')
            queued.push(pcm)
            return
          }
          const output = response
          if (output.destroyed) throw new Error('SuiteCut live audio input disconnected')
          if (!output.write(pcm)) await once(output, 'drain', { signal })
        },
        close: () => {
          inputs.delete(id)
          queued = []
          queuedBytes = 0
          response?.end()
        },
      }
    },
    close: async () => {
      if (closed) return
      closed = true
      buffer.clear()
      inputs.clear()
      for (const client of sockets.clients) client.terminate()
      sockets.close()
      ready?.(true)
      server.closeAllConnections()
      await new Promise<void>((resolve, reject) =>
        server.close((error) => (error ? reject(error) : resolve())),
      )
    },
  }
}
