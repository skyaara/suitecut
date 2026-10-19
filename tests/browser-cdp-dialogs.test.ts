// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { createCDPDialogControls } from '../src/browser-cdp-dialogs.js'
import { type ProtocolDialog } from '../src/browser-protocol-dialog.js'
import { type CDPPageRequest } from '../src/browser-protocol-types.js'
import { type BrowserWire } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  type Handler = (event: Record<string, UntrustedInput>, sessionId?: string) => void
  const events = new Map<string, Set<Handler>>()
  let resolveClosed!: () => void
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const request = vi.fn<BrowserWire['request']>().mockResolvedValue({})
  const wire = {
    request,
    closed,
    isConnected: () => true,
    on(method: string, listener: Handler) {
      const group = events.get(method) ?? new Set<Handler>()
      events.set(method, group)
      group.add(listener)
      return () => {
        group.delete(listener)
      }
    },
  } as UntrustedInput as BrowserWire
  const sessions = new Map([
    ['s1', 'p1'],
    ['s2', 'p2'],
  ])
  const controls = createCDPDialogControls(wire, vi.fn<CDPPageRequest>(), {
    pageForSession: (id) => sessions.get(id),
  })
  function emit(method: string, event: Record<string, UntrustedInput>, sessionId?: string) {
    for (const listener of events.get(method) ?? []) listener(event, sessionId)
  }
  function opening(sessionId = 's1') {
    emit(
      'Page.javascriptDialogOpening',
      {
        type: 'prompt',
        message: 'Enter a value',
        defaultPrompt: 'initial',
      },
      sessionId,
    )
  }
  return { controls, request, sessions, emit, opening, resolveClosed, events }
}

async function flush() {
  for (let i = 0; i < 6; i++) await Promise.resolve()
}

describe('CDP browser dialogs', () => {
  it('routes by originating session and preserves a subscribed dialog until answered', async () => {
    const f = fixture()
    let dialog: ProtocolDialog | undefined
    const unrelated = vi.fn()
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.controls.subscribe('p2', unrelated)
    f.opening()
    await flush()
    expect(unrelated).not.toHaveBeenCalled()
    expect(f.request).not.toHaveBeenCalled()
    expect(dialog?.type()).toBe('prompt')
    expect(dialog?.message()).toBe('Enter a value')
    expect(dialog?.defaultValue()).toBe('initial')
    await dialog?.accept('chosen')
    expect(f.request).toHaveBeenCalledWith(
      'Page.handleJavaScriptDialog',
      {
        accept: true,
        promptText: 'chosen',
      },
      expect.objectContaining({ sessionId: 's1' }),
    )
    await expect(dialog?.dismiss()).rejects.toThrow('already handled')
    f.controls.dispose()
  })

  it('dismisses unsubscribed dialogs and safely handles throwing and rejecting listeners', async () => {
    const f = fixture()
    f.opening()
    await flush()
    expect(f.request).toHaveBeenLastCalledWith(
      'Page.handleJavaScriptDialog',
      {
        accept: false,
        promptText: '',
      },
      expect.objectContaining({ sessionId: 's1' }),
    )
    f.emit('Page.javascriptDialogClosed', {}, 's1')
    const stop = f.controls.subscribe('p1', () => {
      throw new Error('listener failed')
    })
    f.opening()
    await flush()
    expect(f.request).toHaveBeenCalledTimes(2)
    stop()
    f.emit('Page.javascriptDialogClosed', {}, 's1')
    f.controls.subscribe('p1', () => Promise.reject(new Error('async listener failed')))
    f.opening()
    await flush()
    expect(f.request).toHaveBeenCalledTimes(3)
    f.controls.dispose()
  })

  it('does not overwrite an answer when a second listener throws', async () => {
    const f = fixture()
    let answer: Promise<void> | undefined
    f.controls.subscribe('p1', (dialog) => {
      answer = dialog.accept('answer')
    })
    f.controls.subscribe('p1', () => {
      throw new Error('other listener failed')
    })
    f.opening()
    await answer
    expect(f.request).toHaveBeenCalledTimes(1)
    expect(f.request.mock.calls[0]?.[1]).toEqual({ accept: true, promptText: 'answer' })
    f.controls.dispose()
  })

  it('allows retries after a failed answer while the dialog is still open', async () => {
    const f = fixture()
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening()
    f.request.mockRejectedValueOnce(new Error('temporary protocol failure'))
    await expect(dialog.accept()).rejects.toThrow('temporary protocol failure')
    await dialog.dismiss()
    expect(f.request).toHaveBeenCalledTimes(2)
    f.controls.dispose()
  })

  it('rejects duplicate concurrent answers and accepts a normal closed event before the response', async () => {
    const f = fixture()
    let resolve!: (value: Record<string, UntrustedInput>) => void
    f.request.mockImplementationOnce(
      () =>
        new Promise((done) => {
          resolve = done
        }),
    )
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening()
    const answer = dialog.accept()
    await flush()
    await expect(dialog.dismiss()).rejects.toThrow('already handled')
    f.emit('Page.javascriptDialogClosed', {}, 's1')
    resolve({})
    await answer
    await expect(dialog.accept()).rejects.toThrow('already handled')
    f.controls.dispose()
  })

  it('invalidates closed/replaced handles and rejects in-flight work on page close', async () => {
    const f = fixture()
    const dialogs: ProtocolDialog[] = []
    f.controls.subscribe('p1', (value) => {
      dialogs.push(value)
    })
    f.opening()
    f.opening()
    await expect(dialogs[0]?.accept()).rejects.toThrow('replaced')
    f.request.mockImplementationOnce(() => new Promise(() => undefined))
    const answer = dialogs[1]!.accept()
    await flush()
    f.emit('Target.targetDestroyed', { targetId: 'p1' })
    await expect(answer).rejects.toThrow('closed')
    await expect(dialogs[1]?.dismiss()).rejects.toThrow('closed')
    f.controls.dispose()
  })

  it('rejects on session detachment even after the session-to-page mapping has been erased', async () => {
    const f = fixture()
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening()
    f.sessions.delete('s1')
    f.emit('Target.detachedFromTarget', { sessionId: 's1' })
    await expect(dialog.accept()).rejects.toThrow('detached')
    f.controls.dispose()
  })

  it('dismisses a pending dialog on last unsubscribe and releases all wire listeners on close', async () => {
    const f = fixture()
    const stop = f.controls.subscribe('p1', () => undefined)
    f.opening()
    stop()
    await flush()
    expect(f.request).toHaveBeenCalledTimes(1)
    f.resolveClosed()
    await flush()
    expect([...f.events.values()].every((group) => group.size === 0)).toBe(true)
    expect(() => f.controls.subscribe('p1', () => undefined)).toThrow('closed')
  })

  it('preserves page subscriptions when an iframe process swaps to another session', () => {
    const f = fixture()
    const listener = vi.fn()
    f.controls.subscribe('p1', listener)
    f.sessions.set('child', 'p1')
    f.opening('child')
    f.sessions.delete('child')
    f.emit('Target.detachedFromTarget', { sessionId: 'child' })
    f.opening('s1')
    expect(listener).toHaveBeenCalledTimes(2)
    f.controls.dispose()
  })

  it('aborts the actual transport answer on teardown', async () => {
    const f = fixture()
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening()
    f.request.mockImplementationOnce(() => new Promise(() => undefined))
    const answer = dialog.accept()
    await flush()
    const signal = f.request.mock.calls[0]?.[2]?.signal
    expect(signal?.aborted).toBe(false)
    f.controls.dispose()
    await expect(answer).rejects.toThrow('closed')
    expect(signal?.aborted).toBe(true)
  })

  it('keeps an outstanding reply owned after the dialog closes until the wire responds', async () => {
    const f = fixture()
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening()
    f.request.mockImplementationOnce(() => new Promise(() => undefined))
    const answer = dialog.accept()
    await flush()
    f.emit('Page.javascriptDialogClosed', {}, 's1')
    f.emit('Target.targetDestroyed', { targetId: 'p1' })
    await expect(answer).rejects.toThrow('closed')
    expect(f.request.mock.calls[0]?.[2]?.signal?.aborted).toBe(true)
    f.controls.dispose()
  })

  it('ignores unknown sessions and rejects invalid prompt text without answering', async () => {
    const f = fixture()
    let dialog!: ProtocolDialog
    f.controls.subscribe('p1', (value) => {
      dialog = value
    })
    f.opening('unknown')
    expect(f.request).not.toHaveBeenCalled()
    f.opening()
    await expect(dialog.accept(123 as never)).rejects.toThrow('string')
    expect(f.request).not.toHaveBeenCalled()
    await dialog.dismiss()
    f.controls.dispose()
  })
})
