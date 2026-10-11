import assert from 'node:assert/strict'
import { createServer } from 'node:http'
import { writeFile, mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
import { launchEngine } from '../../dist/engine.js'
const directory = resolve(process.env.MEMORY_DIRECTORY ?? '.suitecut/rtc-policy')
await mkdir(directory, { recursive: true })
const server = createServer((_, r) => r.end('<!doctype html><title>RTC policy fixture</title>'))
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const url = `http://127.0.0.1:${server.address().port}`
const results = []
try {
  for (const policy of ['default', 'disable-non-proxied-udp']) {
    const engine = await launchEngine({
      executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
      width: 640,
      height: 360,
      framesPerSecond: 30,
      webrtcPolicy: policy,
    })
    try {
      for (const kind of ['root', 'isolated', 'proxied']) {
        const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
        const context =
          kind === 'root'
            ? engine.browser.contexts()[0]
            : await engine.browser.newContext(
                kind === 'proxied' && proxy
                  ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } }
                  : {},
              )
        const page = kind === 'root' ? context.pages()[0] : await context.newPage()
        await page.goto(url)
        const gathered = await page.evaluate(
          () =>
            new Promise(async (resolve, reject) => {
              const peer = new RTCPeerConnection()
              const candidates = []
              let finished = false
              const done = () => {
                if (finished) return
                finished = true
                clearTimeout(timer)
                const state = peer.iceGatheringState
                peer.close()
                resolve({ candidates, gatheringState: state })
              }
              const timer = setTimeout(done, 2500)
              peer.onicecandidate = (e) => {
                if (e.candidate)
                  candidates.push({ type: e.candidate.type, protocol: e.candidate.protocol })
                else done()
              }
              try {
                peer.createDataChannel('policy-check')
                await peer.setLocalDescription(await peer.createOffer())
              } catch (e) {
                clearTimeout(timer)
                peer.close()
                reject(e)
              }
            }),
        )
        if (policy === 'default')
          assert(
            gathered.candidates.some((c) => c.protocol === 'udp'),
            `${kind} default did not expose UDP candidates`,
          )
        else
          assert(
            !gathered.candidates.some((c) => c.protocol === 'udp'),
            `${kind} policy failed to suppress unproxied UDP`,
          )
        results.push({ policy, context: kind, ...gathered, status: 'passed' })
        if (kind !== 'root') await context.close()
      }
    } finally {
      await engine.close()
    }
  }
} finally {
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  await writeFile(
    resolve(directory, 'policy-results.json'),
    JSON.stringify(results, null, 2) + '\n',
  )
}
console.log(JSON.stringify(results, null, 2))
