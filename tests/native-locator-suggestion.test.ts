// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { decodeNativeLocatorSuggestions } from '../src/native-locator-suggestion.js'
import { createNativePage, NativeElementHandle } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

const pins = {
  source: 'controlledSource',
  frame: 'actualFrame',
  session: '',
  loader: 'actualLoader',
  mainIdentity: 'actualMainUnique',
  utilityIdentity: 'actualUtilityUnique',
  mainContext: 4,
  utilityContext: 5,
  documentGeneration: '7',
  backendNodeId: 913,
}
const packet = () => ({
  provenance: { ...pins },
  pointPicking: false,
  suggestions: [
    {
      kind: 'role',
      query: [{ engine: 'role', role: 'button', name: { text: 'Go', exact: true } }],
      code: 'frame.getByRole("button", { name: "Go", exact: true })',
      matches: 1,
      sameNode: true,
    },
  ],
  diagnostics: [
    { kind: 'testId', matches: 2, reason: 'Query is not unique for the selected node' },
  ],
})
function fixture(reply: UntrustedInput = packet()) {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>(() => Promise.resolve(reply))
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled-test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => [{ id: 'actualFrame', parentId: '', name: 'Main', url: 'about:blank' }],
  }
  return { page: createNativePage(source), runAutomation }
}
describe('native locator suggestion transport, controlled DTO only', () => {
  it('preserves native order/recipe/code/diagnostics and returns the ordinary native locator', async () => {
    const { page, runAutomation } = fixture()
    const result = await page.locator('button').suggest({ timeout: 37 })
    expect(runAutomation.mock.calls[0]?.slice(0, 2)).toEqual([
      'suggestLocator',
      expect.objectContaining({
        query: [{ engine: 'css', selector: 'button' }],
        framePath: [],
        timeoutMs: 37,
      }),
    ])
    expect(result.provenance).toEqual(pins)
    expect(result.suggestions[0]?.code).toBe(packet().suggestions[0]?.code)
    expect(result.suggestions[0]?.locator.page()).toBe(page)
    expect(result.diagnostics).toEqual(packet().diagnostics)
    expect(result.pointPicking).toBe(false)
  })
  it('uses the actual retained handle rather than a selector requery', async () => {
    const { page, runAutomation } = fixture({
      ...packet(),
      provenance: { ...pins, retainedHandle: 17 },
    })
    const handle = new NativeElementHandle(page, 17)
    await handle.suggestLocator({ timeout: 23 })
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      nativeHandle: 17,
      query: [],
      timeoutMs: 23,
    })
  })
  it('validates options and cancellation before issuing a command', async () => {
    const { page, runAutomation } = fixture()
    await expect(page.locator('button').suggest({ timeout: -1 })).rejects.toThrow()
    const cancellation = new AbortController()
    cancellation.abort(new Error('actual stop'))
    await expect(page.locator('button').suggest({ signal: cancellation.signal })).rejects.toThrow(
      'actual stop',
    )
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('does not manufacture a frame when the native owner is unavailable', async () => {
    const { page } = fixture({ ...packet(), provenance: { ...pins, frame: 'missingActualFrame' } })
    await expect(page.locator('button').suggest()).rejects.toThrow(
      'frame identity is no longer available',
    )
  })
  it('rejects unverified/different-node/custom or inconsistent recipe packets', () => {
    for (const row of [
      { ...packet().suggestions[0], sameNode: false },
      { ...packet().suggestions[0], matches: 2 },
      { ...packet().suggestions[0], query: [{ engine: 'custom', name: 'author', selector: 'x' }] },
      { ...packet().suggestions[0], query: [{ engine: 'css', selector: 'button' }] },
    ])
      expect(() => decodeNativeLocatorSuggestions({ ...packet(), suggestions: [row] })).toThrow()
  })
  it('does not invent point picking or backend/context receipts', () => {
    for (const changed of [
      { pointPicking: true },
      { provenance: { ...pins, backendNodeId: 0 } },
      { provenance: { ...pins, utilityIdentity: '' } },
    ])
      expect(() => decodeNativeLocatorSuggestions({ ...packet(), ...changed })).toThrow()
  })
})

describe('native point picking transport, no browser hit-test claim', () => {
  it('transports the actual integer point and native point/frame receipt without an SDK hit test', async () => {
    const point = { x: 13, y: 17, inspectorX: 113, inspectorY: 217, inspectorRoot: 'actualFrame' }
    const { page, runAutomation } = fixture({
      ...packet(),
      pointPicking: true,
      provenance: { ...pins, point },
    })
    const result = await page.mainFrame().pickLocator({ x: 13, y: 17 }, { timeout: 29 })
    expect(runAutomation.mock.calls[0]?.slice(0, 2)).toEqual([
      'pickLocator',
      expect.objectContaining({ x: 13, y: 17, frame: 'actualFrame', timeoutMs: 29 }),
    ])
    expect(result.provenance.point).toEqual(point)
    expect(result.pointPicking).toBe(true)
  })
  it('rejects unsupported/fractional points before command admission', async () => {
    const { page, runAutomation } = fixture()
    await expect(page.pickLocator({ x: 1.5, y: 0 })).rejects.toThrow()
    await expect(page.pickLocator({ x: -1, y: 0 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
