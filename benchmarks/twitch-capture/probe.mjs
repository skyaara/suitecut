import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = process.env.MEMORY_DIRECTORY
await mkdir(directory, { recursive: true })
const engine = await launchEngine({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
  width: 1280,
  height: 720,
  framesPerSecond: 30,
})
try {
  const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
  const context = await engine.browser.newContext(proxy ? { proxy: { server: proxy } } : {})
  const page = await context.newPage()
  page.on('console', (message) => {
    if (message.type() === 'error') console.log('console:', message.text().slice(0, 600))
  })
  page.on('requestfailed', (request) =>
    console.log('request failed:', new URL(request.url()).hostname, request.failure()?.errorText),
  )
  await page.goto('https://www.twitch.tv/gofns', { waitUntil: 'domcontentloaded', timeout: 60000 })
  await delay(18000)
  const state = await page.evaluate(() => ({
    title: document.title,
    text: document.body.innerText.slice(0, 14000),
    videos: [...document.querySelectorAll('video')].map((v) => ({
      paused: v.paused,
      currentTime: v.currentTime,
      readyState: v.readyState,
      width: v.videoWidth,
      height: v.videoHeight,
      muted: v.muted,
      error: v.error?.message,
      duration: Number.isFinite(v.duration) ? v.duration : String(v.duration),
    })),
  }))
  await writeFile(join(directory, 'page-state.json'), JSON.stringify(state, null, 2))
  await page.screenshot({ path: join(directory, 'page.png') })
  console.log(JSON.stringify(state, null, 2))
} finally {
  await engine.close()
}
