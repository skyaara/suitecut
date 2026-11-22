import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Follows a guide on MDN: clicks through to it, zooms into a code sample and
// jumps to a section, narrated. Set SUITECUT_NATIVE_EXECUTABLE first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

const run = await record(
  'Follow an MDN guide',
  async ({ page, suitecut }) => {
    await page.goto('https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API')
    await suitecut.narrate('A walkthrough of the Fetch API docs on MDN, written as a script.')
    await suitecut.click(page.locator('main a[href$="/Fetch_API/Using_Fetch"]').first())
    await page.locator('main pre').first().waitFor()
    await suitecut.highlight(page.locator('h1'))
    await suitecut.zoom(page.locator('main pre').first(), { scale: 1.2, holdMs: 1400 })
    await suitecut.narrate('Each step lands in the video: the click, the highlight and this zoom.')
    await suitecut.click(page.locator('main a[href="#making_a_request"]').first())
    await suitecut.highlight(page.locator('#making_a_request'), { label: 'section' })
    await suitecut.narrate('Edit the script and record again, and the video stays up to date.')
    await suitecut.hold(800)
  },
  { native: { width: 1280, height: 720 }, output: { directory: output } },
)

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: join(output, 'mdn.mp4'),
})
