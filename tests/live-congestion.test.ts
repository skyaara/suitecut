import { describe, expect, it } from 'vitest'

import { LiveCongestion, type LiveStreamDiagnostic } from '../src/live-congestion.js'

describe('live congestion recovery', () => {
  it('allows catch-up, counts incidents with hysteresis, and bounds stale capture time', () => {
    const events: LiveStreamDiagnostic[] = []
    const recovery = new LiveCongestion((event) => events.push(event))
    recovery.measure({ processingMs: 6000 })
    expect(recovery.update(0, 6000)).toBe(0)
    expect(recovery.update(1000, 5100)).toBe(0)
    recovery.update(2000, 500)
    recovery.update(12000, 500)
    expect(events.map((e) => e.event)).toEqual(['recovering', 'caught-up'])
    expect(events[0]?.bottleneck).toBe('frame-processing')
    recovery.update(13000, 6000)
    recovery.update(14000, 0)
    recovery.update(24000, 0)
    recovery.update(25000, 6000)
    expect(events.some((e) => e.event === 'persistent-congestion' && e.incidents === 3)).toBe(true)
    expect(recovery.update(26000, 2000)).toBe(2000)
    recovery.update(27000, 0)
    recovery.update(327000, 0)
    expect(recovery.update(328000, 2000)).toBe(0)
    expect(recovery.update(329000, 11_000)).toBe(11_000)
  })

  it('does not count repeated samples as incidents and escalates sustained congestion', () => {
    const events: LiveStreamDiagnostic[] = []
    const recovery = new LiveCongestion((e) => events.push(e))
    recovery.measure({ videoWriteMs: 6500, audioWriteMs: 10 })
    for (let at = 0; at <= 31000; at += 1000) recovery.update(at, 6000)
    expect(events.filter((e) => e.event === 'recovering')).toHaveLength(1)
    expect(events.filter((e) => e.event === 'persistent-congestion')).toHaveLength(1)
    expect(events[0]?.bottleneck).toBe('video-input-backpressure')
  })

  it('does not report delivery recovery just because capture time was skipped', () => {
    const events: LiveStreamDiagnostic[] = []
    const recovery = new LiveCongestion((event) => events.push(event))
    recovery.update(0, 6000)
    recovery.measure({ outputLagMs: 3000, progressAgeMs: 100 })
    recovery.update(1000, 0)
    recovery.update(20000, 0)
    expect(events.some((event) => event.event === 'caught-up')).toBe(false)
    recovery.measure({
      outputLagMs: 0,
      progressAgeMs: 100,
      networkBlockedMs: 3000,
      networkQueuedBytes: 65536,
    })
    recovery.update(21000, 0)
    recovery.update(31000, 0)
    expect(events.some((event) => event.event === 'caught-up')).toBe(false)
    recovery.measure({ networkBlockedMs: 0, networkQueuedBytes: 0 })
    recovery.update(32000, 0)
    recovery.update(42000, 0)
    expect(events.some((event) => event.event === 'caught-up')).toBe(true)
  })

  it('can discard matching audio when the byte cap evicts video, without leaking callback errors', () => {
    const recovery = new LiveCongestion(() => {
      throw Error('caller-secret')
    })
    expect(recovery.update(0, 700, true)).toBe(700)
    expect(() => recovery.stalled(20_000)).not.toThrow()
  })
})
