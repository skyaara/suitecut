// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { resolve } from 'node:path'

import { describe, expect, it } from 'vitest'

import { NativeOutputCallbacks } from '../src/native-runner-output.js'

const packet = (id = '1', test = '1', generation = '1', path = '/tmp/actual-native-root') => [
  'OUTPUT_CONTEXT',
  id,
  test,
  generation,
  '3',
  '71',
  Buffer.from(path).toString('hex'),
  '7',
  '19',
]
describe('native output directory snapshots and lexical marshaling', () => {
  it('ACKs checked native owner metadata and joins bounded paths without file IO', async () => {
    const sent: (string | number)[][] = []
    const callbacks = new NativeOutputCallbacks((...fields) => {
      sent.push(fields)
      return Promise.resolve()
    })
    await callbacks.packet(packet(), 3, 71)
    expect(sent).toEqual([['OUTPUT_CONTEXT_READY', '1', '1', '1']])
    const info = callbacks.snapshot('1')
    expect(Object.isFrozen(info)).toBe(true)
    expect(info.outputDir).toBe('/tmp/actual-native-root')
    expect(info.outputPath()).toBe(info.outputDir)
    expect(info.outputPath('a dir', 'file.txt')).toBe(resolve(info.outputDir, 'a dir', 'file.txt'))
  })
  it('rejects traversal, absolute, NUL and unbounded inputs rather than producing foreign paths', async () => {
    const callbacks = new NativeOutputCallbacks(() => Promise.resolve())
    await callbacks.packet(packet(), 3, 71)
    const info = callbacks.snapshot('1')
    for (const path of ['..', '../foreign', '/tmp/foreign', 'a\0b', 'x'.repeat(4097)])
      expect(() => info.outputPath(path)).toThrow()
    expect(() => info.outputPath(...Array<string>(33).fill('a'))).toThrow()
    expect(info.outputPath('a', '..', 'safe')).toBe(resolve(info.outputDir, 'safe'))
  })
  it('keeps earlier callback snapshots immutable through later attempts', async () => {
    const callbacks = new NativeOutputCallbacks(() => Promise.resolve())
    await callbacks.packet(packet(), 3, 71)
    const first = callbacks.snapshot('1')
    await callbacks.packet(packet('2', '2', '7', '/tmp/another-native-root'), 3, 71)
    expect(first.outputPath('real')).toBe('/tmp/actual-native-root/real')
    expect(callbacks.snapshot('2').outputPath('real')).toBe('/tmp/another-native-root/real')
    expect(() => callbacks.snapshot('1')).toThrow('admission')
  })
  it('rejects stale/foreign native owners before ACK or snapshot mutation', async () => {
    let count = 0
    const callbacks = new NativeOutputCallbacks(() => {
      ++count
      return Promise.resolve()
    })
    await callbacks.packet(packet(), 3, 71)
    await expect(callbacks.packet(packet(), 3, 71)).rejects.toThrow('Stale')
    await expect(callbacks.packet(packet('2', '2', '2'), 4, 71)).rejects.toThrow('owner')
    await expect(callbacks.packet(packet('2', '2', '2'), 3, 72)).rejects.toThrow('owner')
    expect(count).toBe(1)
    expect(callbacks.snapshot('1').outputDir).toBe('/tmp/actual-native-root')
  })
  it('rejects malformed native path bytes and noncanonical paths without inventing directories', async () => {
    const callbacks = new NativeOutputCallbacks(() => Promise.resolve())
    const malformed = packet()
    malformed[6] = 'ff'
    await expect(callbacks.packet(malformed, 3, 71)).rejects.toThrow('path')
    await expect(callbacks.packet(packet('1', '1', '1', '/tmp/../tmp/dir'), 3, 71)).rejects.toThrow(
      'path',
    )
    expect(() => callbacks.snapshot('1')).toThrow('admission')
  })
})
