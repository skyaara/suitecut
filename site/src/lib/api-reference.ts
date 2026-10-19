export const recordExample = `import { record, renderSuiteCut } from 'suitecut'

const result = await record('Native tour', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.highlight(page.locator('h1'))
  await suitecut.narrate('Welcome to the tour.')
  await suitecut.checkpoint('Opening screen')
}, {
  native: { width: 1920, height: 1080, framesPerSecond: 120 },
  capture: { framesPerSecond: 60, audio: true },
})

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/tour.mp4',
  config: { output: { framesPerSecond: 60 } },
})`

export const streamExample = `import { launchBrowser, createBroadcast } from 'suitecut'

const source = await launchBrowser({ framesPerSecond: 120 })
let broadcast
try {
  await source.navigate('https://example.com')
  broadcast = await createBroadcast({
    source,
    stream: { url: process.env.RTMP_URL!, framesPerSecond: 60, audio: true },
  })
  await broadcast.ready()
  await Promise.race([source.failure, broadcast.failure, applicationStopSignal])
} finally {
  await broadcast?.stop()
  await source.close()
}`

export const renderExample = `import { renderSuiteCut } from 'suitecut/render'

await renderSuiteCut({
  manifestPath: '.suitecut/tour.json',
  outputPath: '.suitecut/tour.mp4',
  config: {
    output: { width: 1920, height: 1080, framesPerSecond: 60, quality: 'high' },
    sourceAudioVolume: 0.75,
    narrationVolume: 1,
  },
})`
