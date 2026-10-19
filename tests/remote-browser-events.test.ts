// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { createProtocolDialog, type ProtocolDialog } from '../src/browser-protocol-dialog.js'
import { ProtocolDownload } from '../src/browser-protocol-download.js'
import { type BrowserProtocolDriver } from '../src/browser-protocol-types.js'
import { type BrowserWire } from '../src/browser-wire.js'
import { ProtocolPage } from '../src/remote-browser.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const dialogListeners = new Set<(dialog: ProtocolDialog) => void>()
  const stopDialog = vi.fn()
  let downloadListener: ((download: ProtocolDownload) => void) | undefined
  const stopDownload = vi.fn()
  const driver = {
    wire: { isConnected: () => true } as UntrustedInput as BrowserWire,
    preparePage: vi.fn<NonNullable<BrowserProtocolDriver['preparePage']>>().mockResolvedValue(),
    insertText: vi.fn<BrowserProtocolDriver['insertText']>().mockResolvedValue(),
    subscribeDialogs: vi.fn<NonNullable<BrowserProtocolDriver['subscribeDialogs']>>(
      (_id, listener) => {
        dialogListeners.add(listener)
        return () => {
          dialogListeners.delete(listener)
          stopDialog()
        }
      },
    ),
    subscribeDownloads: vi.fn<NonNullable<BrowserProtocolDriver['subscribeDownloads']>>(
      (_id, listener) => {
        downloadListener = listener
        return Promise.resolve(stopDownload)
      },
    ),
  } satisfies Pick<
    BrowserProtocolDriver,
    'wire' | 'preparePage' | 'insertText' | 'subscribeDialogs' | 'subscribeDownloads'
  >
  const page = new ProtocolPage(driver as UntrustedInput as BrowserProtocolDriver, {
    id: 'page',
    contextId: 'context',
    url: 'about:blank',
  })
  function dialog() {
    const answer = vi.fn<(accept: boolean, text: string) => Promise<void>>().mockResolvedValue()
    const control = createProtocolDialog(
      { type: 'prompt', message: 'Value?', defaultValue: '' },
      answer,
    )
    for (const listener of dialogListeners) listener(control.dialog)
    return { answer, dialog: control.dialog }
  }
  return {
    page,
    driver,
    dialog,
    dialogListeners,
    stopDialog,
    stopDownload,
    download(value: ProtocolDownload) {
      downloadListener?.(value)
    },
  }
}

describe('cross-browser page event ownership', () => {
  it.each(['throw', 'reject'] as const)(
    'dismisses a dialog when a user callback fails via %s',
    async (mode) => {
      const f = fixture()
      await f.page.prepare()
      const listener =
        mode === 'throw'
          ? () => {
              throw new Error('user callback failed')
            }
          : () => Promise.reject(new Error('user callback rejected'))
      f.page.on('dialog', listener as UntrustedInput as (value: ProtocolDialog) => void)
      const opened = f.dialog()
      await vi.waitFor(() => expect(opened.answer).toHaveBeenCalledWith(false, ''))
      expect(f.dialogListeners.size).toBe(1)
      f.page.retire()
      expect(f.stopDialog).toHaveBeenCalledTimes(1)
    },
  )

  it('delivers once without prematurely dismissing its dialog, then defaults to dismissal', async () => {
    const f = fixture()
    await f.page.prepare()
    let accepted: Promise<void> | undefined
    const listener = vi.fn((dialog: ProtocolDialog) => {
      accepted = dialog.accept('once value')
    })
    f.page.once('dialog', listener)
    const first = f.dialog()
    await accepted
    expect(first.answer).toHaveBeenCalledExactlyOnceWith(true, 'once value')
    const second = f.dialog()
    await vi.waitFor(() => expect(second.answer).toHaveBeenCalledExactlyOnceWith(false, ''))
    expect(listener).toHaveBeenCalledTimes(1)
    // Removing a page callback must not tear down the transport subscription mid-callback.
    expect(f.stopDialog).not.toHaveBeenCalled()
    f.page.retire()
  })

  it('hands a waited-for dialog to the consumer after removing its temporary event listener', async () => {
    const f = fixture()
    await f.page.prepare()
    const pending = f.page.waitForEvent('dialog', { timeout: 1_000 })
    const first = f.dialog()
    const dialog = await pending
    expect(first.answer).not.toHaveBeenCalled()
    await dialog.accept('waited value')
    expect(first.answer).toHaveBeenCalledExactlyOnceWith(true, 'waited value')
    f.page.retire()
  })

  it('retries failed preparation without retaining duplicate dialog subscriptions', async () => {
    const f = fixture()
    f.driver.preparePage.mockRejectedValueOnce(new Error('temporary attach failure'))
    await expect(f.page.prepare()).rejects.toThrow('temporary attach failure')
    expect(f.dialogListeners.size).toBe(0)
    expect(f.stopDialog).toHaveBeenCalledTimes(1)
    await Promise.all([f.page.prepare(), f.page.prepare()])
    expect(f.driver.preparePage).toHaveBeenCalledTimes(2)
    expect(f.driver.subscribeDialogs).toHaveBeenCalledTimes(2)
    expect(f.dialogListeners.size).toBe(1)
    await expect(f.page.keyboard.type('recovered')).resolves.toBeUndefined()
    f.page.retire()
    expect(f.stopDialog).toHaveBeenCalledTimes(2)
  })

  it('reports failed download setup, preserves ordinary actions, and allows subscription retry', async () => {
    const f = fixture()
    await f.page.prepare()
    f.driver.subscribeDownloads.mockRejectedValueOnce(new Error('temporary download setup failure'))
    await expect(f.page.waitForEvent('download', { timeout: 1_000 })).rejects.toThrow(
      'temporary download setup failure',
    )
    await expect(f.page.keyboard.type('still usable')).resolves.toBeUndefined()
    expect(f.driver.insertText).toHaveBeenCalledWith('page', 'still usable')
    const pending = f.page.waitForEvent('download', { timeout: 1_000 })
    await vi.waitFor(() => expect(f.driver.subscribeDownloads).toHaveBeenCalledTimes(2))
    const download = new ProtocolDownload(
      'https://example.test/file',
      'file.txt',
      '/unused',
      () => Promise.resolve(),
      () => true,
    )
    f.download(download)
    await expect(pending).resolves.toBe(download)
    f.page.retire()
    expect(f.stopDownload).toHaveBeenCalledTimes(1)
  })

  it('rejects outstanding event waits and releases all subscriptions on retirement', async () => {
    const f = fixture()
    await f.page.prepare()
    const pending = f.page.waitForEvent('dialog', { timeout: 0 })
    f.page.retire()
    await expect(pending).rejects.toThrow('closed')
    expect(f.dialogListeners.size).toBe(0)
    expect(f.stopDialog).toHaveBeenCalledTimes(1)
    f.page.retire()
    expect(f.stopDialog).toHaveBeenCalledTimes(1)
  })
})
