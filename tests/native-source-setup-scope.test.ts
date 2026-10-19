// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeSourceSetupProtocol } from '../src/native-runner-source-protocol.js'

const sidecar = (scope: 'test' | 'beforeAll' | 'afterAll', test: string) => [
  'SOURCE_SETUP',
  '3',
  '11',
  '7',
  '900',
  test,
  '13',
  'nativeFixture11',
  scope,
]
const fixture = (work = true) =>
  new NativeSourceSetupProtocol(
    () => Promise.resolve(),
    () => undefined,
    work,
  )

describe('native selected worker Source callback-scope marshalling', () => {
  it('preserves an original test-associated setup sidecar without a new bridge capability', () => {
    const p = fixture(false)
    p.packet(['SOURCE_SETUP', '3', '11', '7', '900', '5', '13', 'nativeFixture11'])
    expect(p.take('7', '900', '13', 'nativeFixture11', 'worker', false)?.test).toBe('5')
  })
  it('keeps genuine beforeAll associatedTest0 and native callback scope separate from fixture scope', () => {
    const p = fixture()
    p.packet(sidecar('beforeAll', '0'))
    expect(
      p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'beforeAll', '0'),
    ).toMatchObject({
      test: '0',
      owner: '900',
      generation: '13',
      declaration: '11',
      scope: 'worker',
      callbackScope: 'beforeAll',
    })
  })
  it('keeps genuine afterAll test0 without assigning a neighboring test attempt', () => {
    const p = fixture()
    p.packet(sidecar('afterAll', '0'))
    expect(
      p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'afterAll', '0')?.test,
    ).toBe('0')
  })
  it('rejects a fabricated zero test-scope or nonzero all-hook association before consuming a sidecar', () => {
    const p = fixture()
    expect(() => p.packet(sidecar('test', '0'))).toThrow('identity')
    expect(() => p.packet(sidecar('beforeAll', '5'))).toThrow('identity')
    p.packet(sidecar('beforeAll', '0'))
    expect(
      p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'beforeAll', '0'),
    ).toBeDefined()
  })
  it('retains a genuine all-hook sidecar after foreign actual callback scope/test/version or constant binding', () => {
    const p = fixture()
    p.packet(sidecar('beforeAll', '0'))
    expect(() => p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'test', '0')).toThrow(
      'differs',
    )
    expect(() =>
      p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'beforeAll', '5'),
    ).toThrow('differs')
    expect(() =>
      p.take('7', '900', '13', 'nativeFixture12', 'worker', false, 'beforeAll', '0'),
    ).toThrow('differs')
    expect(() =>
      p.take('7', '900', '13', 'nativeFixture11', 'worker', true, 'beforeAll', '0'),
    ).toThrow('differs')
    expect(
      p.take('7', '900', '13', 'nativeFixture11', 'worker', false, 'beforeAll', '0'),
    ).toBeDefined()
  })
  it('refuses new work-control packets without the explicit native launch bridge marker', () => {
    const p = fixture(false)
    expect(() => p.packet(['SOURCE_WORK_BEGIN', '7', '3', '9', '11', '1000.125'])).toThrow(
      'not admitted',
    )
    expect(p.idleReady).toBe(true)
  })
})
