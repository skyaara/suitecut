// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase the HTTP lifecycle edge. */
import { Buffer } from 'node:buffer'
import { close, fstatSync, ReadStream, statSync } from 'node:fs'
import { basename } from 'node:path'
import process from 'node:process'

import type { NativeMultipartFile } from './native-http.js'
import type { UntrustedInput } from './untrusted.js'

type CloseOwnedZero = (fd: number, done: (error: NodeJS.ErrnoException | null) => void) => void
interface Identity {
  dev: bigint
  ino: bigint
}

type Value = string | number | boolean | NativeMultipartFile | Blob | ReadStream
export type NativeStreamMultipartInput = Record<string, Value> | FormData
const MAX_BYTES = 8 * 1024 * 1024
// Host I/O is separate from native byte/curl ownership. A user-visible timeout
// never releases a slot occupied by a ReadStream or promise still settling.
const physicalJobs = new Set<NativeMultipartStreamJob>()
const physicalStreams = new Set<ReadStream>()

export function hasNativeMultipartStream(value: NativeStreamMultipartInput): boolean {
  return (
    !(value instanceof FormData) && Object.values(value).some((item) => item instanceof ReadStream)
  )
}

function zeroFileIdentity(stream: ReadStream): Identity {
  const stat = fstatSync(0, { bigint: true }),
    path = statSync(stream.path, { bigint: true })
  if (stat.dev !== path.dev || stat.ino !== path.ino)
    throw new Error('Native multipart descriptor0 does not identify its genuine file path')
  return { dev: stat.dev, ino: stat.ino }
}

export class NativeMultipartStreamJob {
  readonly abort = new AbortController()
  readonly closed: Promise<void>
  private resolveClosed: () => void = () => undefined
  private readonly entries: [string, Value][]
  private readonly outstanding = new Set<ReadStream>()
  private finished = false
  private released = false
  private started = false

  static physical(): { jobs: number; streams: number } {
    return { jobs: physicalJobs.size, streams: physicalStreams.size }
  }
  constructor(value: NativeStreamMultipartInput, closeOwnedZero: CloseOwnedZero = close) {
    const entries: [string, Value][] = []
    if (value instanceof FormData) value.forEach((item, name) => entries.push([name, item]))
    else entries.push(...Object.entries(value))
    if (entries.length > 1024) throw new Error('Native multipart supports at most 1024 parts')
    const streams = entries
      .filter((pair): pair is [string, ReadStream] => pair[1] instanceof ReadStream)
      .map((pair) => pair[1])
    if (
      !streams.length ||
      new Set(streams).size !== streams.length ||
      streams.some(
        (stream) =>
          physicalStreams.has(stream) ||
          stream.destroyed ||
          stream.closed ||
          stream.readableEncoding !== null ||
          stream.readableObjectMode ||
          stream.readableHighWaterMark > MAX_BYTES ||
          !(typeof stream.path === 'string' || Buffer.isBuffer(stream.path)) ||
          stream._destroy !== ReadStream.prototype._destroy,
      )
    )
      throw new Error(
        'Native multipart requires distinct available binary ReadStreams with a path, original destruction hook and at most 8 MiB highWaterMark',
      )
    if (physicalJobs.size >= 16 || streams.length > 128 - physicalStreams.size)
      throw new Error('Native multipart host physical stream capacity exceeded')
    let known = 0
    for (const [name, item] of entries) {
      if (Buffer.byteLength(name) > 8192)
        throw new Error('Native multipart field name exceeds 8192 bytes')
      if (item instanceof ReadStream) continue
      const bytes =
        item instanceof Blob
          ? item.size
          : typeof item === 'object'
            ? item.buffer.length
            : Buffer.byteLength(String(item))
      if (bytes > MAX_BYTES - known) throw new Error('Native API upload exceeds 8 MiB')
      known += bytes
    }
    const zeroIdentities = new Map<ReadStream, Identity>()
    for (const stream of streams) {
      const descriptor = Reflect.get(stream, 'fd') as UntrustedInput
      if (descriptor === 0) zeroIdentities.set(stream, zeroFileIdentity(stream))
    }
    this.entries = entries
    this.closed = new Promise((resolve) => {
      this.resolveClosed = resolve
    })
    physicalJobs.add(this)
    for (const stream of streams) {
      this.outstanding.add(stream)
      physicalStreams.add(stream)
      // eslint-disable-next-line @typescript-eslint/unbound-method -- Restore this exact native method after invoking it with .call(stream), preserving its actual fs.close receipt.
      const originalDestroy = stream._destroy
      let zeroIdentity = zeroIdentities.get(stream)
      let unresolvedZero = false
      const opened = (fd: number) => {
        if (fd !== 0) return
        try {
          zeroIdentity = zeroFileIdentity(stream)
        } catch (error) {
          this.stop(
            error instanceof Error ? error : new Error('Native multipart file identity failed'),
          )
        }
      }
      const cleanup = () => {
        stream.removeListener('error', onError)
        stream.removeListener('close', onClose)
        stream.removeListener('open', opened)
      }
      const physicallyClosed = () => {
        if (!this.outstanding.delete(stream)) return
        physicalStreams.delete(stream)
        if (stream._destroy === observeDestroy) stream._destroy = originalDestroy
        // Node schedules error/close emissions from its original destruction
        // callback. Keep our error listener through those genuine emissions.
        process.nextTick(cleanup)
        this.release()
      }
      const onError = (error: Error) => this.stop(error)
      const onClose = () => {
        if (!unresolvedZero && stream.closed) physicallyClosed()
      }
      const observeDestroy: ReadStream['_destroy'] = (error, done) => {
        const complete = (failure: Error | null | undefined, settled = true) => {
          if (failure) this.stop(failure)
          try {
            done(failure ?? null)
          } finally {
            if (settled) physicallyClosed()
          }
        }
        originalDestroy.call(stream, error, (failure) => {
          // Node's FS close helper historically skips descriptor0 as falsy.
          // The real _destroy receipt already waited all in-flight reads. Only
          // close the exact path-backed descriptor identity this job adopted.
          const descriptor = Reflect.get(stream, 'fd') as UntrustedInput
          if (descriptor !== 0) {
            complete(failure)
            return
          }
          if (!zeroIdentity) {
            unresolvedZero = true
            complete(
              new Error('Native multipart descriptor0 closure identity is unavailable'),
              false,
            )
            return
          }
          try {
            const current = fstatSync(0, { bigint: true })
            if (current.dev !== zeroIdentity.dev || current.ino !== zeroIdentity.ino) {
              Reflect.set(stream, 'fd', null)
              complete(new Error('Native multipart descriptor ownership changed before closure'))
              return
            }
          } catch (error) {
            if (error instanceof Error && 'code' in error && error.code === 'EBADF') {
              Reflect.set(stream, 'fd', null)
              complete(error)
            } else {
              unresolvedZero = true
              complete(
                error instanceof Error
                  ? error
                  : new Error('Native multipart descriptor identity failed'),
                false,
              )
            }
            return
          }
          unresolvedZero = true
          Reflect.set(stream, 'fd', null)
          closeOwnedZero(0, (closeError) => {
            if (closeError && closeError.code !== 'EBADF') {
              // A failed OS close does not prove that the descriptor is gone.
              complete(closeError, false)
              return
            }
            unresolvedZero = false
            complete(failure ?? closeError)
          })
        })
      }
      stream._destroy = observeDestroy
      stream.once('error', onError)
      stream.once('close', onClose)
      stream.once('open', opened)
    }
  }
  stop(error: Error): void {
    if (!this.started) this.finished = true
    if (!this.abort.signal.aborted) this.abort.abort(error)
    for (const stream of this.outstanding) if (!stream.destroyed) stream.destroy(error)
    this.release()
  }
  private release(): void {
    if (this.released || !this.finished || this.outstanding.size) return
    this.released = true
    physicalJobs.delete(this)
    this.resolveClosed()
  }
  async pump(
    send: (chunk: Buffer, signal: AbortSignal) => Promise<void>,
  ): Promise<Record<string, UntrustedInput>[]> {
    if (this.started) throw new Error('Native multipart stream preparation already started')
    this.started = true
    const parts: Record<string, UntrustedInput>[] = []
    let total = 0
    const append = async (buffer: Buffer): Promise<void> => {
      if (buffer.length > MAX_BYTES - total) throw new Error('Native API upload exceeds 8 MiB')
      for (let offset = 0; offset < buffer.length; offset += 24 * 1024) {
        this.abort.signal.throwIfAborted()
        await send(buffer.subarray(offset, offset + 24 * 1024), this.abort.signal)
      }
      total += buffer.length
    }
    try {
      for (const [name, item] of this.entries) {
        this.abort.signal.throwIfAborted()
        const offset = total
        let filename: string | undefined,
          mimeType: string | undefined,
          inferMime = false
        if (item instanceof ReadStream) {
          filename =
            typeof item.path === 'string' || Buffer.isBuffer(item.path)
              ? basename(item.path.toString())
              : 'stream'
          inferMime = true
          for await (const chunk of item) {
            if (!Buffer.isBuffer(chunk))
              throw new Error('Native multipart ReadStream yielded non-binary data')
            await append(chunk)
          }
        } else if (item instanceof Blob) {
          const bytes = await item.arrayBuffer()
          this.abort.signal.throwIfAborted()
          await append(Buffer.from(bytes))
          filename = item instanceof File ? item.name : 'blob'
          mimeType = item.type || 'application/octet-stream'
        } else if (typeof item === 'object') {
          await append(item.buffer)
          filename = item.name
          mimeType = item.mimeType ?? 'application/octet-stream'
        } else await append(Buffer.from(String(item)))
        parts.push({
          name,
          offset,
          size: total - offset,
          ...(filename === undefined ? {} : { filename }),
          ...(mimeType === undefined ? {} : { mimeType }),
          ...(inferMime ? { inferMime: true } : {}),
        })
        if (Buffer.byteLength(JSON.stringify(parts)) > 48 * 1024)
          throw new Error('Native multipart metadata exceeds 48 KiB')
      }
    } catch (error) {
      this.stop(error instanceof Error ? error : new Error('Native multipart host stream failed'))
      throw error
    } finally {
      this.finished = true
      for (const stream of this.outstanding) if (!stream.destroyed) stream.destroy()
      this.release()
      await this.closed
    }
    this.abort.signal.throwIfAborted()
    return parts
  }
}
