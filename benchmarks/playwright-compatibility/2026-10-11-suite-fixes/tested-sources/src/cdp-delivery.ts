/** Deliver CDP packets in order, yielding for promise continuations between packets. */
export class CDPDelivery {
  private queue: { message: object; bytes: number }[] = []
  private queuedBytes = 0
  private scheduled: ReturnType<typeof setImmediate> | undefined
  private closed = false
  private ending: string | undefined

  constructor(
    private readonly deliver: (message: object) => void,
    private readonly onClose: (reason: string) => void,
    private readonly maxMessages = 1024,
    private readonly maxBytes = 64 * 1024 * 1024,
  ) {}

  enqueue(message: object, bytes: number): void {
    if (this.closed || this.ending !== undefined) return
    if (
      !Number.isSafeInteger(bytes) ||
      bytes < 0 ||
      this.queue.length >= this.maxMessages ||
      this.queuedBytes + bytes > this.maxBytes
    ) {
      this.close('CEF CDP delivery capacity exceeded')
      return
    }
    this.queue.push({ message, bytes })
    this.queuedBytes += bytes
    this.schedule()
  }

  /** Preserve final packets before reporting a native process disconnection. */
  end(reason: string): void {
    if (this.closed || this.ending !== undefined) return
    this.ending = reason
    this.schedule()
  }

  close(reason: string): void {
    if (this.closed) return
    this.closed = true
    if (this.scheduled) clearImmediate(this.scheduled)
    this.scheduled = undefined
    this.queue = []
    this.queuedBytes = 0
    this.onClose(reason)
  }

  private schedule(): void {
    if (this.closed || this.scheduled) return
    this.scheduled = setImmediate(() => {
      this.scheduled = undefined
      const next = this.queue.shift()
      if (next) {
        this.queuedBytes -= next.bytes
        try {
          this.deliver(next.message)
        } catch {
          this.close('CEF CDP delivery failed')
          return
        }
      } else if (this.ending !== undefined) {
        this.close(this.ending)
        return
      }
      // Each packet gets its own task. A batched loop here reintroduces the
      // frame-tree / execution-context race even when the loop is asynchronous.
      if (this.queue.length || this.ending !== undefined) this.schedule()
    })
  }
}
