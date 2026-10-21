// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type NativeFrameInfo } from '../src/native-protocol.js'

function fixture() {
  const parent: NativeFrameInfo = {
    id: 'parent',
    parentId: '',
    name: '',
    url: 'http://parent.test/',
    navigationSequence: 1,
  }
  const attached: NativeFrameInfo = {
    id: 'child',
    parentId: 'parent',
    name: '',
    url: '',
    navigationSequence: 0,
  }
  let infos = [parent, attached]
  const browser: NativeBrowser = {
    state: 'running',
    url: parent.url,
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'fixture',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation: () => Promise.resolve({}),
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => infos,
  }
  const page = createNativePage(browser)
  const child = page.frames().find((value) => value.identity() === 'child')
  if (!child) throw new Error('Controlled child frame missing')
  return {
    page,
    child,
    attached,
    publish: (value: NativeFrameInfo) => {
      infos = [parent, value]
    },
  }
}

describe('native frame observation ordering', () => {
  it('preserves a navigation predicate snapshot before the public registry catches up', async () => {
    const f = fixture()
    const committed = {
      ...f.attached,
      url: 'http://child.test/',
      name: 'committed',
      navigationSequence: 1,
    }
    f.child.update(committed)
    await Promise.resolve()
    expect(f.child.url()).toBe(committed.url)
    expect(f.child.name()).toBe('committed')
    f.child.update(f.attached)
    expect(f.child.url()).toBe(committed.url)
    f.publish(committed)
    expect(f.page.frames().find((value) => value.identity() === 'child')).toBe(f.child)
    expect(f.child.url()).toBe(committed.url)
  })

  it('keeps a newer registry observation when an older journal candidate is decoded', () => {
    const f = fixture()
    f.child.update({ ...f.attached, url: 'http://child.test/one', navigationSequence: 1 })
    f.publish({ ...f.attached, url: 'http://child.test/two', navigationSequence: 2 })
    expect(f.child.url()).toBe('http://child.test/two')
    f.child.update({ ...f.attached, url: 'http://child.test/one', navigationSequence: 1 })
    expect(f.child.url()).toBe('http://child.test/two')
  })

  it('retains compatibility with backends that do not report navigation sequences', () => {
    const f = fixture()
    const legacy = { id: 'child', parentId: 'parent', name: '', url: 'http://child.test/legacy' }
    f.publish(legacy)
    expect(f.child.url()).toBe(legacy.url)
    f.publish({ ...legacy, url: 'http://child.test/new' })
    expect(f.child.url()).toBe('http://child.test/new')
  })
})
