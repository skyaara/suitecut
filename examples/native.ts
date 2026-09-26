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
    await page.goto('https://example.com')
    await suitecut.highlight(page.locator('h1'))
    await suitecut.narrate('Welcome to SuiteCut 2.0 and the embedded browser.')
    await suitecut.checkpoint('Opening screen')
  },
  options,
)

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/native-tour.mp4',
})
