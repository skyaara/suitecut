import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type NativeWorkerEvent } from '../src/native-worker-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

// Controlled real SDK private-pipe transport; these tests do not claim CEF.
async function transport() {
  const child = new ChildProcess(),
    input = new PassThrough(),
    output = new PassThrough()
  child.stdin = input
  child.stdout = output
  child.stderr = new PassThrough()
  vi.mocked(spawn).mockReset().mockReturnValue(child)
  const killed = vi.spyOn(child, 'kill').mockReturnValue(true)
  input.once('finish', () => queueMicrotask(() => child.emit('close', 0)))
  const emit = (header: object, value?: object) => {
    const data = value === undefined ? Buffer.alloc(0) : Buffer.from(JSON.stringify(value))
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  const launching = launchNativeBrowser({
    executablePath: '/authored/worker-helper',
    width: 2,
    height: 2,
  })
  await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
  emit({ type: 'ready', timestampMs: 1, cefVersion: 'controlled-transport' })
  return { source: await launching, child, emit, killed, input }
}
const worker = {
  id: 'actual-target',
  url: 'https://native.test/worker.js',
  type: 'worker' as const,
}

describe('observed native worker transport boundaries', () => {
  it('delivers actual pre-ready updates without inserting Page or Context discovery inventory', async () => {
    const { source, emit } = await transport()
    const pageEvents: NativeWorkerEvent[] = [],
      contextEvents: NativeWorkerEvent[] = []
    source.onWorkerMessage?.((event) => pageEvents.push(event))
    source.onContextWorkerMessage?.((record) => contextEvents.push(record.workerEvent))
    try {
      const updated = { event: 'updated' as const, worker, sequence: 1 }
      emit({ type: 'worker', sequence: 1 }, updated)
      emit({ type: 'contextworker' }, { contextId: 2, workerEvent: updated })
      expect(pageEvents).toEqual([updated])
      expect(contextEvents).toEqual([updated])
      expect(source.workerInfos?.()).toEqual([])
      expect(source.contextWorkerInfos?.(2)).toEqual([])
      const ready = { ...updated, event: 'worker', sequence: 2 }
      emit({ type: 'worker', sequence: 2 }, ready)
      emit({ type: 'contextworker' }, { contextId: 2, workerEvent: ready })
      const changed = {
        ...updated,
        sequence: 3,
        worker: { ...worker, url: 'https://native.test/changed.js' },
      }
      emit({ type: 'worker', sequence: 3 }, changed)
      emit({ type: 'contextworker' }, { contextId: 2, workerEvent: changed })
      expect(source.workerInfos?.()).toEqual([changed.worker])
      expect(source.contextWorkerInfos?.(2)).toEqual([changed.worker])
      emit({ type: 'worker', sequence: 4 }, { ...changed, event: 'close', sequence: 4 })
      expect(source.workerRetirements?.().records[0]?.event).toBe('close')
      expect(source.contextWorkerRetirements?.(2).records).toEqual([])
      emit(
        { type: 'contextworker' },
        { contextId: 2, workerEvent: { ...changed, event: 'channelclosed', sequence: 4 } },
      )
      expect(source.contextWorkerRetirements?.(2).records[0]?.event).toBe('channelclosed')
      expect(source.workerRetirements?.().records[0]?.event).toBe('close')
    } finally {
      await source.close()
      vi.restoreAllMocks()
    }
  })
  it('renews actual console wait transport only after a newer ready receipt and ignores an old close', async () => {
    const { source, emit, input } = await transport()
    try {
      const pageSource = source.pageSource?.(1)
      if (!pageSource) throw new Error('Page source unavailable')
      const page = createNativePage(pageSource)
      emit({ type: 'worker', sequence: 1 }, { event: 'worker', worker, sequence: 1 })
      const target = page.workerFromConsole(worker)
      emit({ type: 'worker', sequence: 2 }, { event: 'channelclosed', worker, sequence: 2 })
      emit({ type: 'worker', sequence: 3 }, { event: 'worker', worker, sequence: 3 })
      emit({ type: 'worker', sequence: 2 }, { event: 'close', worker, sequence: 2 })
      expect(pageSource.workerInfos?.()).toEqual([worker])
      expect(page.workerFromConsole(worker)).toBe(target)
      expect(target.isClosed()).toBe(false)
      let controls = ''
      input.on('data', (data: Buffer) => {
        controls += data.toString()
      })
      const pending = target.waitForEvent('console', { timeout: 100 })
      const settled = pending.catch((error: UntrustedInput) => error)
      await vi.waitFor(() => expect(controls).toContain('waitEventPredicate'))
      expect(controls).toContain('console')
      await source.close()
      expect(await settled).toBeInstanceOf(Error)
      expect(target.isClosed()).toBe(true)
    } finally {
      await source.close()
      vi.restoreAllMocks()
    }
  })
  it('retains actual retirement receipts for a controller constructed only after target closure', async () => {
    const { source, emit } = await transport()
    try {
      const pageSource = source.pageSource?.(1)
      if (!pageSource) throw new Error('Page source unavailable')
      emit({ type: 'worker', sequence: 1 }, { event: 'close', worker, sequence: 1 })
      const page = createNativePage(pageSource)
      expect(page.workerFromConsole(worker).isClosed()).toBe(true)
      expect(page.workers()).toEqual([])
    } finally {
      await source.close()
      vi.restoreAllMocks()
    }
  })
  it('uses actual processExit for a first controller constructed after child termination', async () => {
    const { source, child } = await transport()
    const pageSource = source.pageSource?.(1)
    if (!pageSource) throw new Error('Page source unavailable')
    const page = createNativePage(pageSource)
    child.emit('close', 1, null)
    await source.closed
    expect(page.workerFromConsole(worker).isClosed()).toBe(true)
    vi.restoreAllMocks()
  })
  it('retains observed worker after logical fatal error until actual child close and stdio receipt', async () => {
    const { source, child, emit, killed } = await transport()
    const pageSource = source.pageSource?.(1)
    expect(pageSource).toBeDefined()
    if (!pageSource) throw new Error('Actual Page source unavailable')
    expect(pageSource.processClosed).toBe(source.closed)
    const page = createNativePage(pageSource),
      observed = page.workerFromConsole(worker),
      closed = vi.fn()
    observed.on('close', closed)
    let physicallyClosed = false
    void pageSource.processClosed?.then(() => {
      physicallyClosed = true
    })
    emit({ type: 'error', error: 'Actual controlled bootstrap failure' })
    expect(source.signal.aborted).toBe(true)
    expect(killed).toHaveBeenCalledWith('SIGKILL')
    expect(physicallyClosed).toBe(false)
    expect(observed.isClosed()).toBe(false)
    expect(closed).not.toHaveBeenCalled()
    child.emit('close', null, 'SIGKILL')
    await source.closed
    expect(physicallyClosed).toBe(true)
    expect(observed.isClosed()).toBe(true)
    expect(closed).toHaveBeenCalledExactlyOnceWith(observed)
    vi.restoreAllMocks()
  })
  it('keeps real process closure pending after an actual individual Page close packet', async () => {
    const { source, emit } = await transport()
    try {
      emit({ type: 'context' }, { event: 'page-created', contextId: 1, pageId: 2, sequence: 1 })
      const page = source.pageSource?.(2)
      expect(page).toBeDefined()
      if (!page) throw new Error('Actual child source unavailable')
      let physical = false
      void page.processClosed?.then(() => {
        physical = true
      })
      emit({ type: 'context' }, { event: 'page-closed', contextId: 1, pageId: 2, sequence: 2 })
      await page.closed
      expect(physical).toBe(false)
      expect(source.state).toBe('running')
      expect(page.processClosed).toBe(source.closed)
    } finally {
      await source.close()
      vi.restoreAllMocks()
    }
  })
})
