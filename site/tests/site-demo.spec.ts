import { expect, test } from '../../dist/test.js'

test.use({
  suitecutCapture: {
    size: { width: 1280, height: 720 },
    framesPerSecond: 60,
    quality: 100,
  },
})

test('records the minimal SuiteCut site', async ({ page, suitecut }) => {
  test.setTimeout(240_000)
  await page.goto('./')
  await suitecut.narrate('Record Playwright. Render the saved run.', {
    voice: 'af_heart',
  })

  const script = page.locator('.home-reference').first()
  await suitecut.checkpoint('SuiteCut home')
  await suitecut.zoom(script, { scale: 1.12, holdMs: 900 })

  const docs = page.getByRole('link', { name: 'Docs', exact: true })
  await suitecut.highlight(docs, { borderColor: '#ffffff', durationMs: 900 })
  await suitecut.click(docs)
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Getting started')
  await suitecut.checkpoint('Getting started')
  await suitecut.hold(500)
})
