// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { pid } from 'node:process'

import { describe, expect, it, vi } from 'vitest'

import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'

const owner = { token: '0', scope: '1', generation: '1' }
const wire = (value: string) => Buffer.from(value, 'utf16le').toString('hex')
function fixture() {
  const packets: (string | number)[][] = []
  const fatal = vi.fn<(error: Error) => void>()
  const settled = vi.fn<() => void>()
  const callbacks = new NativeAttachmentCallbacks(
    (...fields) => {
      packets.push(fields)
      return Promise.resolve()
    },
    fatal,
    settled,
  )
  callbacks.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', String(pid)])
  const attach = callbacks.bind(owner, () => true)
  return { callbacks, attach, packets, fatal, settled }
}

describe('accepted native attachment test-skip receipts', () => {
  it('keeps the pending API and bytes until a matching typed terminal, then rejects with the actual control', async () => {
    const f = fixture()
    const file = f.attach('pending', { body: new Uint8Array([0, 255]) })
    const control = new Error('actual callback control')
    f.callbacks.acceptTestSkip(owner, '9', control)
    f.callbacks.packet(['ATTACH_ADMITTED', '1', '0', '1', '1', '7', '1', '-'])
    expect(f.packets.map((packet) => packet[0])).toEqual(['ATTACH_BEGIN'])
    expect(f.callbacks.physical).toBe(1)
    expect(f.settled).not.toHaveBeenCalled()
    f.callbacks.packet(['ATTACH_TEST_SKIP_RESULT', '1', '0', '1', '1', '7', '9', '-'])
    await expect(file).rejects.toBe(control)
    expect(f.callbacks.physical).toBe(0)
    expect(f.settled).toHaveBeenCalledTimes(1)
    expect(f.fatal).not.toHaveBeenCalled()
  })

  it('rejects an unauthenticated terminal without retiring the pending API', async () => {
    const f = fixture()
    const file = f.attach('pending', { body: 'actual' })
    const control = new Error('actual accepted control')
    f.callbacks.acceptTestSkip(owner, '9', control)
    expect(() =>
      f.callbacks.packet(['ATTACH_TEST_SKIP_RESULT', '1', '0', '1', '1', '7', '10', '-']),
    ).toThrow('accepted mutation')
    expect(f.callbacks.physical).toBe(1)
    f.callbacks.packet(['ATTACH_TEST_SKIP_RESULT', '1', '0', '1', '1', '7', '9', '-'])
    await expect(file).rejects.toBe(control)
  })

  it('does not suppress an ordinary native error that arrives after acceptance', async () => {
    const f = fixture()
    const file = f.attach('pending', { body: 'actual' })
    f.callbacks.acceptTestSkip(owner, '9', new Error('accepted control'))
    f.callbacks.packet([
      'ATTACH_RESULT',
      '1',
      '0',
      '1',
      '1',
      '7',
      '0',
      wire('actual rollback failure'),
    ])
    await expect(file).rejects.toThrow('actual rollback failure')
    expect(f.fatal).toHaveBeenCalledTimes(1)
    expect(f.fatal.mock.calls[0]?.[0].message).toBe('actual rollback failure')
  })

  it('keeps an ordinary rejected attachment catchable without any skip observer', async () => {
    const f = fixture()
    const file = f.attach('ordinary', { path: '' })
    f.callbacks.packet([
      'ATTACH_ADMITTED',
      '1',
      '0',
      '1',
      '1',
      '0',
      '0',
      wire('actual native admission rejection'),
    ])
    await expect(file).rejects.toThrow('actual native admission rejection')
    expect(f.callbacks.physical).toBe(0)
    expect(f.fatal).not.toHaveBeenCalled()
  })
})
