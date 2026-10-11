import { mkdtemp, writeFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import {
  chromium,
  type CDPSession,
  type BrowserContext,
  type BrowserContextOptions,
  type LaunchOptions,
  type Page,
} from 'playwright'

import { raceWithAbort } from './abort.js'
import { createLiveAudioTransport, type LiveAudioTransport } from './live-audio.js'
import { tabAudioOffscreen, tabAudioWorker, tabAudioWorklet } from './tab-audio-extension.js'

interface TabAudio {
  transport: LiveAudioTransport
  failure: Promise<never>
  select(page: Page): Promise<void>
  pause(paused: boolean): void
  close(): Promise<void>
}
const captures = new WeakMap<BrowserContext, TabAudio>()

export function getTabAudio(context: BrowserContext): TabAudio | undefined {
  return captures.get(context)
}

/** Resolve the foreground tab in the selected page window, including duplicate URLs and popups. */
async function tabTargetForPage(page: Page, cdp: CDPSession): Promise<string> {
  const pageSession = await page.context().newCDPSession(page)
  let pageTarget: string
  try {
    pageTarget = (await pageSession.send('Target.getTargetInfo')).targetInfo.targetId
  } finally {
    await pageSession.detach()
  }
  const { windowId } = await cdp.send('Browser.getWindowForTarget', { targetId: pageTarget })
  const { targetInfos } = await cdp.send('Target.getTargets', {
    filter: [{ type: 'tab', exclude: false }],
  })
  for (const target of targetInfos) {
    if (
      target.type !== 'tab' ||
      !(target as typeof target & { embedderData?: { tabActive?: boolean } }).embedderData
        ?.tabActive
    )
      continue
    const window = await cdp.send('Browser.getWindowForTarget', { targetId: target.targetId })
    if (window.windowId === windowId) return target.targetId
  }
  throw new Error('SuiteCut could not identify the selected audio tab')
}

/** Owns an isolated Chromium profile and its tab-capture extension. */
export async function launchTabAudioContext(
  launch: LaunchOptions,
  options: BrowserContextOptions,
): Promise<BrowserContext> {
  if (
    launch.executablePath ||
    (launch.channel && launch.channel !== 'chromium') ||
    launch.ignoreDefaultArgs === true ||
    options.serviceWorkers === 'block'
  ) {
    throw new Error(
      'SuiteCut tab audio requires bundled Chromium, default launch arguments, and service workers',
    )
  }
  const { storageState, ...contextOptions } = options
  const directory = await mkdtemp(join(tmpdir(), 'suitecut-tab-audio-'))
  let context: BrowserContext | undefined
  let transport: LiveAudioTransport | undefined
  try {
    let generation = 0
    let paused = false
    let acceptAfter = Date.now()
    let closed = false
    let selected: Page | undefined
    let pending = Promise.resolve()
    let rejectFailure: (error: Error) => void = () => undefined
    const failure = new Promise<never>((_, reject) => {
      rejectFailure = reject
    })
    void failure.catch(() => undefined)
    const audioTransport = await createLiveAudioTransport(
      (source, timestamp, pcm) => {
        if (source !== generation || paused || closed || timestamp < acceptAfter) return
        audioTransport.buffer.push(performance.now() + timestamp - Date.now(), pcm)
      },
      (source) => {
        // Chromium may end the media track just before Playwright reports tab closure.
        setTimeout(() => {
          if (source === generation && !closed && !selected?.isClosed())
            rejectFailure(new Error('SuiteCut tab audio capture ended unexpectedly'))
        }, 100).unref()
      },
    )
    transport = audioTransport
    await Promise.all(
      Object.entries({
        'manifest.json': JSON.stringify({
          manifest_version: 3,
          name: 'SuiteCut tab audio',
          version: '1.0',
          permissions: ['tabCapture', 'offscreen', 'activeTab'],
          host_permissions: ['http://127.0.0.1/*'],
          action: {},
          background: { service_worker: 'worker.js' },
        }),
        'worker.js': tabAudioWorker.replace(
          '__SUITECUT_AUDIO_URL__',
          JSON.stringify(audioTransport.url),
        ),
        'offscreen.html': '<!doctype html><script src="offscreen.js"></script>',
        'offscreen.js': tabAudioOffscreen,
        'pcm.js': tabAudioWorklet,
      }).map(([name, source]) => writeFile(join(directory, name), source)),
    )
    context = await chromium.launchPersistentContext('', {
      ...launch,
      ...contextOptions,
      channel: 'chromium',
      ignoreDefaultArgs: [
        ...(Array.isArray(launch.ignoreDefaultArgs) ? launch.ignoreDefaultArgs : []),
        '--mute-audio',
      ],
      args: [
        ...(launch.args ?? []),
        '--enable-unsafe-extension-debugging',
        '--autoplay-policy=no-user-gesture-required',
        `--disable-extensions-except=${directory}`,
        `--load-extension=${directory}`,
      ],
    })
    if (storageState !== undefined) await context.setStorageState(storageState)
    const worker =
      context.serviceWorkers()[0] ??
      (await context.waitForEvent('serviceworker', { timeout: 10_000 }))
    // Wait for the initially debugger-paused worker to install its action listener.
    await worker.evaluate(() => undefined)
    const id = new URL(worker.url()).hostname
    context.on('serviceworker', (restarted) => {
      if (new URL(restarted.url()).hostname === id)
        void restarted.evaluate(() => undefined).catch(() => undefined)
    })
    const browser = context.browser()
    if (!browser) throw new Error('SuiteCut audio browser is unavailable')
    const cdp = await browser.newBrowserCDPSession()
    const capture: TabAudio = {
      transport: audioTransport,
      failure,
      select: (page) => {
        if (page === selected) return pending
        selected = page
        const current = ++generation
        acceptAfter = Date.now()
        audioTransport.buffer.clear()
        pending = pending
          .then(async () => {
            if (closed || current !== generation || page.isClosed()) return
            await page.bringToFront()
            const windowType = await worker.evaluate(async () => {
              const api = globalThis as typeof globalThis & {
                chrome: { windows: { getLastFocused: () => Promise<{ type: string }> } }
              }
              return (await api.chrome.windows.getLastFocused()).type
            })
            if (windowType === 'popup')
              throw new Error(
                'SuiteCut tab audio does not support separate popup windows; open the popup as a tab instead',
              )
            const targetId = await tabTargetForPage(page, cdp)
            const activation = raceWithAbort(
              audioTransport.activate(current),
              AbortSignal.timeout(10_000),
            )
            void activation.catch(() => undefined)
            await cdp.send('Extensions.triggerAction', { id, targetId })
            await activation
          })
          .catch((error: Error) => {
            if (!closed && current === generation && !page.isClosed()) {
              rejectFailure(
                new Error(
                  error.message.startsWith(
                    'SuiteCut tab audio does not support separate popup windows',
                  )
                    ? error.message
                    : 'SuiteCut tab audio activation failed. Install the latest Playwright Chromium with Extensions.triggerAction support.',
                ),
              )
              throw error
            }
          })
        return pending
      },
      pause: (value) => {
        paused = value
        acceptAfter = Date.now()
        audioTransport.buffer.clear()
      },
      close: async () => {
        if (closed) return
        closed = true
        generation++
        await audioTransport.close()
        await cdp.detach().catch(() => undefined)
        await rm(directory, { recursive: true, force: true })
      },
    }
    captures.set(context, capture)
    context.once('close', () => {
      void capture.close().catch(() => undefined)
    })
    return context
  } catch (error) {
    await context?.close().catch(() => undefined)
    await transport?.close().catch(() => undefined)
    await rm(directory, { recursive: true, force: true })
    throw error
  }
}
