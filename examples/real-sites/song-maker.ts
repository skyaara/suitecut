import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Writes a short tune in Chrome Music Lab's Song Maker and plays it. The page's
// own Web Audio is recorded with the video. Set SUITECUT_NATIVE_EXECUTABLE first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

// Rows from the bottom of the default two-octave C major grid, one note per beat.
const tune = [0, 2, 4, 5, 4, 2, 1, 0]

const run = await record(
  'Write a tune in Song Maker',
  async ({ page, suitecut }) => {
    await page.goto('https://musiclab.chromeexperiments.com/Song-Maker/')
    const grid = page.locator('#instrument-canvas')
    await grid.waitFor({ state: 'visible', timeout: 60_000 })
    await suitecut.narrate('Chrome Music Lab, live. suitecut writes a tune note by note.')
    const box = await grid.boundingBox()
    if (!box) throw new Error('Song Maker grid is not visible')
    for (const [beat, row] of tune.entries()) {
      const x = box.x + (box.width * (beat * 4 + 0.5)) / 32
      const y = box.y + (box.height * (13 - row + 0.5)) / 14
      await page.mouse.move(x, y, { steps: 6 })
      await page.mouse.click(x, y)
    }
    await suitecut.click(page.getByRole('button', { name: 'Play', exact: true }))
    await suitecut.narrate('The video carries the page’s own sound, straight from the renderer.')
    await suitecut.hold(4000)
    await page.getByRole('button', { name: 'Stop', exact: true }).click()
  },
  {
    native: { width: 1920, height: 1080 },
    capture: { audio: true },
    output: { directory: output },
  },
)

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: join(output, 'song-maker.mp4'),
})
