import { Worker } from 'node:worker_threads'

import {
  type SuiteCutNarrationWorkerRequest,
  SuiteCutNarrationWorkerRequestSchema,
  SuiteCutNarrationWorkerResponseSchema,
} from './narration-worker-protocol.js'
import { type UntrustedInput } from './untrusted.js'

interface PendingNarrationJob {
  resolve(): void
  reject(error: Error): void
}

export type SuiteCutNarrationRequest = Omit<SuiteCutNarrationWorkerRequest, 'type'>

export class SuiteCutNarrationWorker {
  readonly #worker: Worker
  readonly #pending = new Map<string, PendingNarrationJob>()
  #fatalError: Error | undefined
  #closing = false

  constructor() {
    this.#worker = new Worker(new URL('./narration-worker.js', import.meta.url))
    this.#worker.on('message', (message: UntrustedInput) => {
      const parsed = SuiteCutNarrationWorkerResponseSchema.safeParse(message)
      if (!parsed.success) {
        this.#fail(new Error('SuiteCut narration worker returned an invalid response'))
        return
      }
      const response = parsed.data

      const pending = this.#pending.get(response.jobId)
      if (pending === undefined) return
      this.#pending.delete(response.jobId)

      if (response.type === 'synthesized') {
        pending.resolve()
        return
      }

      const error = new Error(response.error.message)
      error.name = response.error.name
      if (response.error.stack !== undefined) error.stack = response.error.stack
      pending.reject(error)
    })
    this.#worker.on('error', (error: Error) => {
      this.#fail(error)
    })
    this.#worker.on('exit', (code) => {
      if (this.#closing) return
      if (this.#pending.size > 0) {
        this.#fail(
          new Error(
            `SuiteCut narration worker exited before completing ${this.#pending.size} job(s)`,
          ),
        )
        return
      }
      if (code !== 0) this.#fail(new Error(`SuiteCut narration worker exited with code ${code}`))
    })
  }

  synthesize(request: SuiteCutNarrationRequest): Promise<void> {
    if (this.#fatalError !== undefined) return Promise.reject(this.#fatalError)
    if (this.#closing) {
      return Promise.reject(new Error('SuiteCut narration worker is closing'))
    }
    if (this.#pending.has(request.jobId)) {
      return Promise.reject(new Error(`Duplicate narration job ID: ${request.jobId}`))
    }

    const parsed = SuiteCutNarrationWorkerRequestSchema.safeParse({
      type: 'synthesize',
      ...request,
    })
    if (!parsed.success) return Promise.reject(parsed.error)

    const result = new Promise<void>((resolve, reject) => {
      this.#pending.set(parsed.data.jobId, { resolve, reject })
    })

    try {
      this.#worker.postMessage(parsed.data)
    } catch (error) {
      this.#pending.delete(parsed.data.jobId)
      return Promise.reject(error instanceof Error ? error : new Error(String(error)))
    }

    return result
  }

  async close(): Promise<void> {
    this.#closing = true
    if (this.#pending.size > 0) {
      this.#fail(new Error('SuiteCut narration worker closed with unfinished jobs'))
    }
    await this.#worker.terminate()
  }

  #fail(error: Error): void {
    this.#fatalError ??= error
    for (const pending of this.#pending.values()) pending.reject(this.#fatalError)
    this.#pending.clear()
  }
}
