import { describe, expect, it } from 'vitest'

import { LiveBitrate } from '../src/live-bitrate.js'

const blocked = { blockedMs: 3000, queuedBytes: 65536, sentKbps: 3000 }
const clear = { blockedMs: 0, queuedBytes: 0, sentKbps: 6000 }

describe('network bitrate control', () => {
  it('ignores CPU-only lag and brief socket pressure', () => {
    const policy = new LiveBitrate(6000)
    for (let at = 0; at < 60000; at += 1000) expect(policy.update(at, clear, false)).toBeUndefined()
    policy.update(60000, blocked, false)
    expect(policy.update(64000, clear, true)).toBeUndefined()
    expect(policy.current).toBe(6000)
  })
  it('reduces toward measured throughput, bounds changes and restores slowly', () => {
    const policy = new LiveBitrate(6000)
    policy.update(0, blocked, false)
    expect(policy.update(5000, blocked, false)).toBe(3000)
    policy.resetConnection()
    policy.update(6000, blocked, false)
    expect(policy.update(11000, blocked, false)).toBeUndefined()
    expect(policy.update(35000, blocked, false)).toBe(2250)
    policy.update(36000, clear, true)
    expect(policy.update(335000, clear, true)).toBeUndefined()
    expect(policy.update(336000, clear, true)).toBe(2850)
  })
  it('does not increase during an output backlog or reduce below the floor', () => {
    const policy = new LiveBitrate(6000)
    for (let at = 0; at < 300000; at += 1000) policy.update(at, { ...blocked, sentKbps: 1 }, false)
    expect(policy.current).toBe(1500)
    expect(policy.update(900000, clear, false)).toBeUndefined()
  })
})
