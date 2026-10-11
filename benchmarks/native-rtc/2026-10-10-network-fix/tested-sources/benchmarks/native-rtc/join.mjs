import { mkdir, readFile, writeFile, mkdtemp, rm } from 'node:fs/promises'
import { resolve } from 'node:path'
import { tmpdir } from 'node:os'
import { chromium } from 'playwright'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.MEMORY_DIRECTORY)
await mkdir(directory, { recursive: true })
const result = {
  requestedUrl: 'https://meet.jit.si/ColourfulProsperitiesSignalTotally',
  startedAt: new Date().toISOString(),
  backend: 'SuiteCut CEF via private Playwright adapter',
  status: 'starting',
  actions: [],
}
let engine, page
async function snapshot(name) {
  await page.screenshot({ path: resolve(directory, name + '.png'), timeout: 10000 })
  const state = await page.evaluate(() => ({
    title: document.title,
    text: document.body.innerText.slice(0, 18000),
    buttons: [...document.querySelectorAll('button,[role=button]')]
      .filter((e) => e.getClientRects().length)
      .map((e) => ({ text: e.innerText, aria: e.getAttribute('aria-label'), id: e.id })),
    inputs: [...document.querySelectorAll('input')]
      .filter((e) => e.getClientRects().length)
      .map((e) => ({
        type: e.type,
        placeholder: e.placeholder,
        aria: e.getAttribute('aria-label'),
        id: e.id,
      })),
  }))
  await writeFile(resolve(directory, name + '.json'), JSON.stringify(state, null, 2) + '\n')
  console.log(JSON.stringify({ snapshot: name, ...state }))
}
try {
  const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
  result.webrtcPolicy = process.env.SUITECUT_WEBRTC_POLICY ?? 'default'
  if (process.env.SUITECUT_RTC_BACKEND === 'chromium-reference') {
    const profile = await mkdtemp(resolve(tmpdir(), 'suitecut-rtc-reference-'))
    await mkdir(resolve(profile, 'Default'))
    await writeFile(
      resolve(profile, 'Default/Preferences'),
      JSON.stringify({ webrtc: { ip_handling_policy: 'disable_non_proxied_udp' } }),
    )
    const reference = await chromium.launchPersistentContext(profile, {
      executablePath: chromium.executablePath(),
      headless: true,
      chromiumSandbox: true,
      viewport: { width: 1280, height: 720 },
      ...(proxy ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } } : {}),
      args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    })
    result.backend =
      'Standalone Chromium reference, explicit proxy and sandbox enabled; not SuiteCut'
    result.browserVersion = reference.browser().version()
    engine = {
      browser: { newContext: async () => reference },
      close: async () => {
        await reference.close()
        await rm(profile, { recursive: true, force: true })
      },
    }
  } else {
    engine = await launchEngine({
      executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
      width: 1280,
      height: 720,
      framesPerSecond: 30,
      graphics: 'swiftshader',
      webrtcPolicy: result.webrtcPolicy,
    })
    result.cefVersion = engine.source.cefVersion
  }
  const context = await engine.browser.newContext(
    proxy ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } } : {},
  )
  page = await context.newPage()
  await page.addInitScript(() => {
    window.suitecutPeers = []
    window.suitecutIceErrors = []
    const Original = window.RTCPeerConnection
    window.RTCPeerConnection = new Proxy(Original, {
      construct(target, args, newTarget) {
        const peer = Reflect.construct(target, args, newTarget)
        window.suitecutPeers.push(peer)
        peer.addEventListener('icecandidateerror', (e) =>
          window.suitecutIceErrors.push({
            code: e.errorCode,
            text: e.errorText,
            url: e.url?.split('?')[0],
          }),
        )
        return peer
      },
    })
  })
  page.on('requestfailed', (request) => {
    const url = new URL(request.url())
    result.lastRequestFailure = {
      origin: url.origin,
      path: url.pathname,
      error: request.failure()?.errorText,
    }
  })
  await page.goto(result.requestedUrl, { waitUntil: 'domcontentloaded', timeout: 60000 })
  await delay(10000)
  await snapshot('initial')
  const name = page.locator('#premeeting-name-input')
  if (await name.isVisible()) {
    await name.fill(process.env.SUITECUT_RTC_NAME ?? 'SuiteCut RTC test')
    await page.getByRole('button', { name: 'Join meeting', exact: true }).click()
    result.actions.push({ type: 'join', name: 'SuiteCut RTC test', at: new Date().toISOString() })
    await delay(10000)
    await snapshot('joined-screen')
  }
  result.status = 'loaded'
  await writeFile(resolve(directory, 'result.json'), JSON.stringify(result, null, 2) + '\n')
  console.log('WAITING_FOR_JOIN_ACTION')
  const began = performance.now()
  let lastAction = ''
  let nextSample = 0
  while (performance.now() - began < Number(process.env.SUITECUT_RTC_DURATION_MS ?? 240000)) {
    let action
    try {
      action = JSON.parse(await readFile(resolve(directory, 'action.json'), 'utf8'))
    } catch {}
    if (action && action.id !== lastAction) {
      lastAction = action.id
      if (action.type === 'finish') break
      if (action.type === 'fill')
        await page.locator(action.selector).fill(action.value, { timeout: 10000 })
      if (action.type === 'click') await page.locator(action.selector).click({ timeout: 10000 })
      result.actions.push({
        id: action.id,
        type: action.type,
        selector: action.selector,
        at: new Date().toISOString(),
      })
      await delay(4000)
      await snapshot('action-' + action.id)
    }
    if (performance.now() - began >= nextSample) {
      const peers = await page.evaluate(async () =>
        Promise.all(
          window.suitecutPeers.map(async (p) => {
            const s = await p.getStats()
            const stats = []
            for (const r of s.values()) {
              if (
                [
                  'transport',
                  'candidate-pair',
                  'inbound-rtp',
                  'outbound-rtp',
                  'local-candidate',
                  'remote-candidate',
                ].includes(r.type)
              ) {
                const safe = {}
                for (const k of [
                  'id',
                  'type',
                  'kind',
                  'state',
                  'selectedCandidatePairId',
                  'localCandidateId',
                  'remoteCandidateId',
                  'candidateType',
                  'protocol',
                  'relayProtocol',
                  'nominated',
                  'bytesSent',
                  'bytesReceived',
                  'packetsSent',
                  'packetsReceived',
                  'packetsLost',
                  'jitter',
                  'currentRoundTripTime',
                  'framesDecoded',
                  'framesEncoded',
                  'totalSamplesReceived',
                  'audioLevel',
                  'dtlsState',
                  'iceState',
                ])
                  if (r[k] !== undefined) safe[k] = r[k]
                stats.push(safe)
              }
            }
            return {
              connectionState: p.connectionState,
              iceConnectionState: p.iceConnectionState,
              signalingState: p.signalingState,
              iceTransportPolicy: p.getConfiguration().iceTransportPolicy,
              servers: p.getConfiguration().iceServers?.map((s) => ({ urls: s.urls })),
              stats,
            }
          }),
        ),
      )
      result.iceErrors = await page.evaluate(() => window.suitecutIceErrors)
      result.lastPeers = peers
      result.status = peers.some((p) => p.connectionState === 'connected')
        ? 'rtc-connected'
        : 'loaded'
      await writeFile(resolve(directory, 'result.json'), JSON.stringify(result, null, 2) + '\n')
      await writeFile(
        resolve(directory, 'rtc-' + Math.round((performance.now() - began) / 1000) + '.json'),
        JSON.stringify(peers, null, 2) + '\n',
      )
      console.log(JSON.stringify({ status: result.status, peerCount: peers.length }))
      nextSample += 20000
    }
    await delay(1000)
  }
  await snapshot('final')
} catch (error) {
  result.status = 'failed'
  result.error = error.message
  if (page) await snapshot('error').catch(() => {})
  console.log(JSON.stringify(result))
} finally {
  await engine?.close()
  result.finishedAt = new Date().toISOString()
  await writeFile(resolve(directory, 'result.json'), JSON.stringify(result, null, 2) + '\n')
}
