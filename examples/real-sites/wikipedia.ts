import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Reads a live Wikipedia article: a highlight, a zoom and a real click, narrated.
// Set SUITECUT_NATIVE_EXECUTABLE to the built engine first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

const run = await record(
  'Read a Wikipedia article',
  async ({ page, suitecut }) => {
    await page.goto('https://en.wikipedia.org/wiki/Chromium_(web_browser)')
    await suitecut.narrate('This is a live Wikipedia article, recorded as the page paints.')
    await suitecut.highlight(page.locator('#firstHeading'), { label: 'highlight' })
    await suitecut.zoom(page.locator('.infobox-image').first(), { scale: 1.2, holdMs: 1200 })
    await suitecut.narrate('Point at anything, and zoom in on the part that matters.')
    await suitecut.click(page.locator('.infobox a[href*="Blink_"]'))
    await page.locator('#firstHeading').filter({ hasText: 'Blink' }).waitFor()
    await suitecut.narrate('That click was real input, and the cursor is drawn into the video.')
    await suitecut.hold(800)
  },
  { native: { width: 1280, height: 720 }, output: { directory: output } },
)

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: join(output, 'wikipedia.mp4'),
})
