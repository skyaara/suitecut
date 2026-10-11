import { type UntrustedInput } from './untrusted.js'

/** Bound native IPC concurrency while preserving CDP command submission order. */
export class CDPCommandQueue {
  private queue: { message: object; bytes: number }[] = []
  private active = 0
  private bytes = 0
  private closed = false

  constructor(
    private readonly send: (message: object) => Promise<void>,
    private readonly onError: (reason: string) => void,
    private readonly concurrency = 16,
    // Playwright adopts querySelectorAll handles concurrently. A normal page
    // with 2,000 matches can submit several thousand small protocol commands.
    private readonly maxMessages = 8192,
    private readonly maxBytes = 64 * 1024 * 1024,
  ) {}

  enqueue(message: object, bytes: number): void {
    if (this.closed) return
    if (
      !Number.isSafeInteger(bytes) ||
      bytes < 0 ||
      this.active + this.queue.length >= this.maxMessages ||
      this.bytes + bytes > this.maxBytes
    ) {
      this.fail('CEF CDP command capacity exceeded')
      return
    }
    this.bytes += bytes
    this.queue.push({ message, bytes })
    this.pump()
  }

  close(): void {
    this.closed = true
    this.queue = []
    this.bytes = 0
  }

  private fail(reason: string): void {
    if (this.closed) return
    this.close()
    this.onError(reason)
  }

  private pump(): void {
    while (!this.closed && this.active < this.concurrency && this.queue.length) {
      const next = this.queue.shift()
      if (!next) return
      this.active++
      // Only await the native IPC acknowledgement, not the CDP response. A
      // paused renderer may require another CDP command to make progress.
      void Promise.resolve()
        .then(() => {
          if (!this.closed) return this.send(next.message)
          return undefined
        })
        .then(
          () => {
            this.active--
            if (this.closed) return
            this.bytes -= next.bytes
            this.pump()
          },
          (error: UntrustedInput) => {
            this.active--
            this.fail(error instanceof Error ? error.message : 'CEF CDP command failed')
          },
        )
    }
  }
}
