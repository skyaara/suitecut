import { Worker } from 'node:worker_threads'

import {
  type SuiteCutNarrationWorkerRequest,
  type SuiteCutNarrationWorkerSuccess,
  SuiteCutNarrationWorkerRequestSchema,
  SuiteCutNarrationWorkerResponseSchema,
} from './narration-worker-protocol.js'
import { type UntrustedInput } from './untrusted.js'

interface PendingNarrationJob {
  resolve(result: SuiteCutNarrationWorkerSuccess): void
  reject(error: Error): void
}

export type SuiteCutNarrationRequest = Omit<SuiteCutNarrationWorkerRequest, 'type'>

const WORKER_CLOSE_TIMEOUT_MS = 5_000

function responseError(response: {
  error: { name: string; message: string; stack?: string }
}): Error {
  const error = new Error(response.error.message)
  error.name = response.error.name
  if (response.error.stack !== undefined) error.stack = response.error.stack
  return error
}

export class SuiteCutNarrationWorker {
  readonly #worker: Worker
  readonly #pending = new Map<string, PendingNarrationJob>()
  readonly #signal: AbortSignal | undefined
  #fatalError: Error | undefined
  #closing = false
  #closePromise: Promise<void> | undefined
  #resolveClose: (() => void) | undefined
  #rejectClose: ((error: Error) => void) | undefined

  constructor(signal?: AbortSignal) {
    signal?.throwIfAborted()
    this.#signal = signal
    this.#worker = new Worker(new URL('./narration-worker.js', import.meta.url))
    this.#worker.on('message', (message: UntrustedInput) => {
      const parsed = SuiteCutNarrationWorkerResponseSchema.safeParse(message)
      if (!parsed.success) {
        this.#fail(new Error('SuiteCut narration worker returned an invalid response'))
        return
      }
      const response = parsed.data

      if (response.type === 'closed') {
        this.#resolveClose?.()
        return
      }
      if (response.type === 'close-failed') {
        this.#rejectClose?.(responseError(response))
        return
      }

      const pending = this.#pending.get(response.jobId)
      if (pending === undefined) return
      this.#pending.delete(response.jobId)

      if (response.type === 'synthesized') {
        pending.resolve(response)
        return
      }

      pending.reject(responseError(response))
    })
    this.#worker.on('error', (error: Error) => {
      this.#fail(error)
      this.#rejectClose?.(error)
    })
    this.#worker.on('exit', (code) => {
      if (this.#closing) {
        if (code !== 0) {
          this.#rejectClose?.(new Error(`SuiteCut narration worker exited with code ${code}`))
        }
        return
      }
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
    signal?.addEventListener('abort', this.#onAbort, { once: true })
    if (signal?.aborted === true) this.#onAbort()
  }

  synthesize(request: SuiteCutNarrationRequest): Promise<SuiteCutNarrationWorkerSuccess> {
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

    const result = new Promise<SuiteCutNarrationWorkerSuccess>((resolve, reject) => {
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
    if (this.#closePromise !== undefined) return this.#closePromise
    this.#closing = true
    if (this.#pending.size > 0) {
      this.#fail(new Error('SuiteCut narration worker closed with unfinished jobs'))
    }
    this.#signal?.removeEventListener('abort', this.#onAbort)
    const closed = new Promise<void>((resolve, reject) => {
      this.#resolveClose = resolve
      this.#rejectClose = reject
    })
    this.#closePromise = (async () => {
      let timer: ReturnType<typeof setTimeout> | undefined
      try {
        this.#worker.postMessage({ type: 'close' })
        await Promise.race([
          closed,
          new Promise<never>((_, reject) => {
            timer = setTimeout(
              () => reject(new Error('SuiteCut narration worker did not close within 5000ms')),
              WORKER_CLOSE_TIMEOUT_MS,
            )
            timer.unref()
          }),
        ])
      } finally {
        if (timer !== undefined) clearTimeout(timer)
        this.#resolveClose = undefined
        this.#rejectClose = undefined
        await this.#worker.terminate()
      }
    })()
    return this.#closePromise
  }

  readonly #onAbort = (): void => {
    const reason =
      this.#signal?.reason instanceof Error
        ? this.#signal.reason
        : new DOMException('The operation was aborted', 'AbortError')
    this.#fail(reason)
    this.#signal?.removeEventListener('abort', this.#onAbort)
    void this.close().catch(() => undefined)
  }

  #fail(error: Error): void {
    this.#fatalError ??= error
    for (const pending of this.#pending.values()) pending.reject(this.#fatalError)
    this.#pending.clear()
  }
}
