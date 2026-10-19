// SPDX-License-Identifier: MIT
import { copyFile, mkdir, rm, stat } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'

/** A streamed browser download. File bytes stay on disk, outside the protocol connection. */
export class ProtocolDownload {
  private readonly settled: Promise<void>
  private resolveSettled!: () => void
  private status: 'pending' | 'completed' | 'failed' = 'pending'
  private error: string | null = null
  private deleted = false
  private cancellation: Promise<void> | undefined

  constructor(
    private readonly sourceURL: string,
    private readonly filename: string,
    private readonly localPath: string,
    private requestCancel: (() => Promise<void>) | undefined,
    private readonly available: (() => boolean) | { closed: boolean },
  ) {
    // This promise always resolves, including failed and abandoned downloads. Calling path()
    // creates the rejection only for the consumer that asks for a successful file.
    this.settled = new Promise((resolve) => {
      this.resolveSettled = resolve
    })
  }

  url(): string {
    return this.sourceURL
  }
  suggestedFilename(): string {
    return this.filename
  }

  /** Internal: idempotently settle from a protocol event or connection teardown. */
  finish(error: string | null): void {
    if (this.status !== 'pending') return
    this.error = error
    this.status = error === null ? 'completed' : 'failed'
    this.requestCancel = undefined
    this.resolveSettled()
  }

  async path(): Promise<string> {
    await this.settled
    if (this.error !== null) throw new Error(`Download failed: ${this.error}`)
    if (this.deleted) throw new Error('Download has been deleted')
    if (!(typeof this.available === 'function' ? this.available() : !this.available.closed))
      throw new Error('Browser download storage is closed')
    const file = await stat(this.localPath)
    if (!file.isFile()) throw new Error('Download did not produce a regular file')
    return this.localPath
  }

  async saveAs(destination: string): Promise<void> {
    if (typeof destination !== 'string' || !destination || destination.includes('\0'))
      throw new Error('Download destination must be a non-empty path')
    const source = await this.path()
    const output = resolve(destination)
    if (source === output) return
    await mkdir(dirname(output), { recursive: true })
    await copyFile(source, output)
  }

  async failure(): Promise<string | null> {
    await this.settled
    return this.error
  }

  async cancel(): Promise<void> {
    if (this.status !== 'pending' || !this.requestCancel) return
    const request = this.requestCancel
    this.cancellation ??= (async () => {
      await request()
      this.finish('cancelled')
    })()
    return this.cancellation
  }

  async delete(): Promise<void> {
    if (this.status === 'pending') await this.cancel()
    await this.settled
    this.deleted = true
    await rm(this.localPath, { force: true })
  }
}
