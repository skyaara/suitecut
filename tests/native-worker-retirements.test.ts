// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { type NativeWorkerEvent } from '../src/native-worker-protocol.js'
import { NativeWorkerRetirements } from '../src/native-worker-retirements.js'

const event = (
  kind: 'worker' | 'close' | 'channelclosed',
  id: string,
  sequence: number,
): NativeWorkerEvent => ({
  event: kind,
  sequence,
  worker: { id, type: 'worker', url: `https://example.test/${id}.js` },
})

describe('native worker retirement journal', () => {
  it('snapshots only its own scope and releases a closed scope', () => {
    const journal = new NativeWorkerRetirements()
    expect(journal.observe('page:1', event('worker', 'a', 1))).toBe(true)
    expect(journal.observe('page:1', event('close', 'b', 2))).toBe(true)
    expect(journal.observe('page:2', event('close', 'a', 3))).toBe(true)
    expect(journal.observe('context:1', event('worker', 'c', 4))).toBe(true)
    expect(journal.snapshot('page:1')).toEqual({
      records: [{ id: 'b', type: 'worker', event: 'close', sequence: 2, reason: undefined }],
      ready: [{ id: 'a', type: 'worker', sequence: 1 }],
    })
    expect(journal.snapshot('page:2').records.map((record) => record.id)).toEqual(['a'])
    // Ordering rules stay per scope: a stale receipt or a channel close after close is refused.
    expect(journal.observe('page:2', event('worker', 'a', 2))).toBe(false)
    expect(journal.observe('page:2', event('channelclosed', 'a', 5))).toBe(false)
    journal.release('page:1')
    expect(journal.snapshot('page:1')).toEqual({ records: [], ready: [] })
    expect(journal.snapshot('page:2').records).toHaveLength(1)
    expect(journal.snapshot('context:1').ready).toHaveLength(1)
  })

  it('restores an owner snapshot into its local view', () => {
    const journal = new NativeWorkerRetirements()
    journal.restore({
      records: [{ id: 'gone', type: 'worker', event: 'close', sequence: 4 }],
      ready: [{ id: 'live', type: 'worker', sequence: 2 }],
    })
    expect(journal.get('gone')?.sequence).toBe(4)
    expect(journal.readySequence('live')).toBe(2)
    expect(journal.latestSequence('gone')).toBe(4)
    expect(journal.observe('', event('worker', 'gone', 5))).toBe(false)
  })
})
