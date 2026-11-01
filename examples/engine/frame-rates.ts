import { stat } from 'node:fs/promises'
import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Records one headless visit to Apple's iPhone store at 120 frames a second, then
// renders it three times: 24 fps for film, 60 for the web and 120 for slow motion.
// The smooth scrolling shows the difference. Read-only: nothing is added to a bag.
// Set SUITECUT_NATIVE_EXECUTABLE to the built engine first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

const run = await record(
  'Frame rates',
  async ({ page, suitecut }) => {
    await page.goto('https://www.apple.com/shop/buy-iphone', { waitUntil: 'domcontentloaded' })
    await page.getByRole('heading', { name: 'Shopping guides.' }).waitFor()
    await suitecut.hold(800)
    for (const section of ['Shopping guides.', 'Ways to save.', 'Accessories.']) {
      await suitecut.scrollTo(page.getByRole('heading', { name: section }), {
        behavior: 'smooth',
        block: 'start',
        settleMs: 500,
      })
    }
    await suitecut.scrollTop({ behavior: 'smooth', settleMs: 600 })
  },
  {
    native: { width: 1280, height: 720, framesPerSecond: 120 },
    capture: { framesPerSecond: 120 },
    output: { directory: output },
  },
)

for (const framesPerSecond of [24, 60, 120]) {
  const outputPath = join(output, `frame-rates-${framesPerSecond}.mp4`)
  await renderSuiteCut({
    manifestPath: run.manifestPath,
    outputPath,
    config: { output: { framesPerSecond } },
  })
  const { size } = await stat(outputPath)
  console.log(`${framesPerSecond} fps: ${outputPath} (${(size / 1e6).toFixed(1)} MB)`)
}
