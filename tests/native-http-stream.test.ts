import { Buffer } from 'node:buffer'
import { createReadStream, read, open, close, openSync, fstatSync } from 'node:fs'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { expect, it } from 'vitest'

import { NativeMultipartStreamJob } from '../src/native-http-stream.js'

it('reads actual file ranges as bounded raw chunks with ordered native metadata', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-'))
  const path = join(directory, 'actual.json')
  const bytes = Buffer.alloc(90 * 1024)
  for (let index = 0; index < bytes.length; ++index) bytes[index] = index % 256
  await writeFile(path, bytes)
  try {
    const stream = createReadStream(path, { start: 7, end: bytes.length - 4 })
    const job = new NativeMultipartStreamJob({ first: 'John', stream, tail: true })
    const chunks: Buffer[] = []
    const parts = await job.pump((chunk) => {
      expect(chunk.length).toBeLessThanOrEqual(24 * 1024)
      chunks.push(Buffer.from(chunk))
      return Promise.resolve()
    })
    await job.closed
    expect(Buffer.concat(chunks)).toEqual(
      Buffer.concat([Buffer.from('John'), bytes.subarray(7, -3), Buffer.from('true')]),
    )
    expect(parts).toEqual([
      { name: 'first', offset: 0, size: 4 },
      {
        name: 'stream',
        offset: 4,
        size: bytes.length - 10,
        filename: 'actual.json',
        inferMime: true,
      },
      { name: 'tail', offset: bytes.length - 6, size: 4 },
    ])
    expect(stream.closed).toBe(true)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    await rm(directory, { recursive: true, force: true })
  }
})

it('retains physical host I/O after cancellation until the actual file-read and close callbacks settle', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-'))
  const path = join(directory, 'held.bin')
  await writeFile(path, Buffer.from([0, 255, 1, 128]))
  let notifyRead: () => void = () => undefined
  const actualRead = new Promise<void>((resolve) => {
    notifyRead = resolve
  })
  let releaseRead: (() => void) | undefined
  const stream = createReadStream(path, {
    fs: {
      open,
      close,
      read: (
        fd: number,
        buffer: Buffer,
        offset: number,
        length: number,
        position: number | null,
        callback: (error: NodeJS.ErrnoException | null, bytes: number, data: Buffer) => void,
      ): void => {
        read(fd, buffer, offset, length, position, (error, bytes, data) => {
          releaseRead = () => callback(error, bytes, data)
          notifyRead()
        })
      },
    },
  })
  const job = new NativeMultipartStreamJob({ stream })
  const pending = job
    .pump(() => Promise.resolve())
    .catch((error) => (error instanceof Error ? error : new Error('unexpected stream failure')))
  try {
    await actualRead
    job.stop(new Error('actual caller cancellation'))
    expect(stream.closed).toBe(false)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 1, streams: 1 })
    expect(releaseRead).toBeTypeOf('function')
    releaseRead?.()
    const error = await pending
    expect(error).toBeInstanceOf(Error)
    if (error instanceof Error) expect(error.message).toContain('actual caller cancellation')
    await job.closed
    expect(stream.closed).toBe(true)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    releaseRead?.()
    stream.destroy()
    await job.closed
    await rm(directory, { recursive: true, force: true })
  }
})

it('closes adopted streams when lookup/native admission rejects before pumping begins', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-'))
  const path = join(directory, 'not-pumped.bin')
  await writeFile(path, 'actual')
  try {
    const stream = createReadStream(path),
      job = new NativeMultipartStreamJob({ stream })
    job.stop(new Error('native preparation rejected'))
    await job.closed
    expect(stream.closed).toBe(true)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    await rm(directory, { recursive: true, force: true })
  }
})

it('rejects text-decoded streams before adopting caller-owned host I/O', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-'))
  const path = join(directory, 'text.txt')
  await writeFile(path, 'text')
  const stream = createReadStream(path, { encoding: 'utf8' })
  try {
    expect(() => new NativeMultipartStreamJob({ stream })).toThrow('binary ReadStreams')
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    const closed = new Promise<void>((resolve) => stream.once('close', resolve))
    stream.destroy()
    await closed
    await rm(directory, { recursive: true, force: true })
  }
})

it('bounds host jobs and rejects duplicate stream ownership without adopting excess streams', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-capacity-'))
  const path = join(directory, 'capacity.bin')
  await writeFile(path, 'actual native host I/O')
  const jobs: NativeMultipartStreamJob[] = []
  const streams = Array.from({ length: 17 }, () => createReadStream(path))
  try {
    for (const stream of streams.slice(0, 16)) jobs.push(new NativeMultipartStreamJob({ stream }))
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 16, streams: 16 })
    expect(() => new NativeMultipartStreamJob({ stream: streams[0]! })).toThrow(
      'distinct available',
    )
    expect(() => new NativeMultipartStreamJob({ stream: streams[16]! })).toThrow(
      'physical stream capacity',
    )
    expect(streams[16]!.destroyed).toBe(false)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 16, streams: 16 })
  } finally {
    for (const job of jobs) job.stop(new Error('actual capacity cleanup'))
    await Promise.all(jobs.map((job) => job.closed))
    const excess = streams[16]!
    if (!excess.closed) {
      const closed = new Promise<void>((resolve) => excess.once('close', resolve))
      excess.destroy()
      await closed
    }
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
    await rm(directory, { recursive: true, force: true })
  }
})

it('rejects oversized host read buffers before adopting or reading caller streams', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-read-budget-'))
  const path = join(directory, 'bounded.bin')
  await writeFile(path, 'actual')
  const stream = createReadStream(path, { highWaterMark: 8 * 1024 * 1024 + 1 })
  try {
    expect(() => new NativeMultipartStreamJob({ stream })).toThrow('highWaterMark')
    expect(stream.destroyed).toBe(false)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    const closed = new Promise<void>((resolve) => stream.once('close', resolve))
    stream.destroy()
    await closed
    await rm(directory, { recursive: true, force: true })
  }
})

it('observes actual destruction receipts when close events and automatic close are disabled', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-no-close-event-'))
  const path = join(directory, 'actual.bin')
  const bytes = Buffer.from([0, 255, 13, 10, 128])
  await writeFile(path, bytes)
  let notifyClose: () => void = () => undefined
  const actualClose = new Promise<void>((resolve) => {
    notifyClose = resolve
  })
  let releaseClose: (() => void) | undefined
  const stream = createReadStream(path, {
    emitClose: false,
    autoClose: false,
    fs: {
      open,
      read,
      close(fd: number, callback: (error: NodeJS.ErrnoException | null) => void) {
        close(fd, (error) => {
          releaseClose = () => callback(error)
          notifyClose()
        })
      },
    },
  })
  let closeEvents = 0
  stream.on('close', () => {
    ++closeEvents
  })
  const job = new NativeMultipartStreamJob({ stream }),
    chunks: Buffer[] = []
  const pending = job.pump((chunk) => {
    chunks.push(Buffer.from(chunk))
    return Promise.resolve()
  })
  try {
    await actualClose
    expect(stream.closed).toBe(false)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 1, streams: 1 })
    const release = releaseClose
    releaseClose = undefined
    if (!release) throw new Error('Missing genuine fs.close receipt')
    release()
    await pending
    await job.closed
    expect(Buffer.concat(chunks)).toEqual(bytes)
    expect(stream.closed).toBe(true)
    expect(closeEvents).toBe(0)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    releaseClose?.()
    job.stop(new Error('actual close callback cleanup'))
    await job.closed
    await rm(directory, { recursive: true, force: true })
  }
})

it('rejects caller-descriptor streams before adopting descriptor ownership', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-descriptor-'))
  const path = join(directory, 'caller.bin')
  await writeFile(path, 'actual caller-owned descriptor')
  const fd = openSync(path, 'r')
  const stream = createReadStream(path, { fd, autoClose: false })
  try {
    expect(stream.path).toBeUndefined()
    expect(() => new NativeMultipartStreamJob({ stream })).toThrow('with a path')
    expect(stream.destroyed).toBe(false)
    expect(fstatSync(fd).isFile()).toBe(true)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    const closed = new Promise<void>((resolve) => stream.once('close', resolve))
    stream.destroy()
    await closed
    await rm(directory, { recursive: true, force: true })
  }
})

it('rejects overridden destruction hooks before relying on their completion receipts', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-stream-custom-destroy-'))
  const path = join(directory, 'actual.bin')
  await writeFile(path, 'actual')
  const stream = createReadStream(path)
  // eslint-disable-next-line @typescript-eslint/unbound-method -- Intentionally preserve and restore the exact native method; it is never invoked unbound.
  const original = stream._destroy
  stream._destroy = (error, done) => done(error)
  try {
    expect(() => new NativeMultipartStreamJob({ stream })).toThrow('original destruction hook')
    expect(stream.destroyed).toBe(false)
    expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
  } finally {
    stream._destroy = original
    const closed = new Promise<void>((resolve) => stream.once('close', resolve))
    stream.destroy()
    await closed
    await rm(directory, { recursive: true, force: true })
  }
})
