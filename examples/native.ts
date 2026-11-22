import { record, renderSuiteCut, type SuiteCutRecordOptions } from 'suitecut'

// Build the pinned CEF browser and set SUITECUT_NATIVE_EXECUTABLE first.
const options = {
  native: { width: 1920, height: 1080, framesPerSecond: 60 },
  capture: { audio: true },
  output: { manifestPath: '.suitecut/native-tour.json' },
} satisfies SuiteCutRecordOptions

const result = await record(
  'Embedded browser tour',
  async ({ page, suitecut }) => {
    await page.goto('https://www.apple.com/shop/buy-iphone', { waitUntil: 'domcontentloaded' })
    await suitecut.highlight(page.getByRole('heading', { level: 1 }).first())
    await suitecut.narrate('This is suitecut, recording a live store in the embedded browser.')
    await suitecut.checkpoint('Opening screen')
  },
  options,
)

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/native-tour.mp4',
})
