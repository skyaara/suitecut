// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'
import { once } from 'node:events'
import { createServer, type ServerResponse } from 'node:http'

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
  createInput(): LiveAudioInput
  close(): Promise<void>
}

/** Private loopback PCM input for FFmpeg, supplied directly by native CEF capture. */
export async function createLiveAudioTransport(): Promise<LiveAudioTransport> {
  let closed = false
  const token = randomUUID()
  const buffer = new LiveAudioBuffer()
  const inputs = new Map<string, (response: ServerResponse) => void>()
  const closeInputs = new Set<() => void>()
  const server = createServer((request, response) => {
    const parts = request.url?.split('/') ?? []
    const accept =
      parts[1] === token && parts[2] === 'input' ? inputs.get(parts[3] ?? '') : undefined
    if (request.method !== 'GET' || !accept) {
      response.writeHead(404).end()
      return
    }
    inputs.delete(parts[3] ?? '')
    response.writeHead(200, { 'Content-Type': 'application/octet-stream' })
    response.flushHeaders()
    accept(response)
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
    createInput: () => {
      if (closed) throw new Error('SuiteCut live audio transport is closed')
      buffer.clear()
      const id = randomUUID()
      let response: ServerResponse | undefined
      let queued: Buffer[] = []
      let queuedBytes = 0
      let inputClosed = false
      const lifetime = new AbortController()
      const closeInput = (): void => {
        if (inputClosed) return
        inputClosed = true
        inputs.delete(id)
        closeInputs.delete(closeInput)
        queued = []
        queuedBytes = 0
        lifetime.abort(new Error('SuiteCut live audio input is closed'))
        const output = response
        response = undefined
        output?.end()
      }
      closeInputs.add(closeInput)
      inputs.set(id, (value) => {
        response = value
        value.once('error', closeInput)
        value.once('close', closeInput)
        for (const chunk of queued) value.write(chunk)
        queued = []
        queuedBytes = 0
      })
      return {
        url: `${url}/input/${id}`,
        write: async (at, samples, signal) => {
          if (inputClosed) throw new Error('SuiteCut live audio input is closed')
          signal.throwIfAborted()
          if (!response && samples > (192000 - queuedBytes) / BYTES_PER_SAMPLE)
            throw new Error('SuiteCut encoder did not connect to its audio input')
          const pcm = buffer.read(at, samples)
          if (!response) {
            queuedBytes += pcm.length
            queued.push(pcm)
            return
          }
          const output = response
          if (output.destroyed) throw new Error('SuiteCut live audio input disconnected')
          if (!output.write(pcm))
            await once(output, 'drain', { signal: AbortSignal.any([signal, lifetime.signal]) })
        },
        close: closeInput,
      }
    },
    close: async () => {
      if (closed) return
      closed = true
      buffer.clear()
      for (const closeInput of closeInputs) closeInput()
      inputs.clear()
      server.closeAllConnections()
      await new Promise<void>((resolve, reject) =>
        server.close((error) => (error ? reject(error) : resolve())),
      )
    },
  }
}
