import { expect, test } from '../../dist/index.js'

test.use({
  suitecutCapture: {
    size: { width: 1280, height: 720 },
    framesPerSecond: 60,
    quality: 100,
  },
})

test('tours the SuiteCut field manual', async ({ page, suitecut }) => {
  await page.goto('./')
  await suitecut.narrate('SuiteCut turns real Playwright tests into composed product films.', {
    provider: 'kokoro',
    voice: 'af_heart',
    caption: 'Real Playwright tests, composed as product films.',
  })

  const manualLink = page.getByRole('link', { name: 'Read the field manual' })
  await suitecut.highlight(manualLink, { borderColor: '#bc9f60', durationMs: 1_200 })
  await manualLink.click()
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Getting started')

  await suitecut.narrate(
    'The field manual covers setup, the fixture API, local speech, and examples.',
    {
      provider: 'kokoro',
      voice: 'af_heart',
    },
  )
  await suitecut.checkpoint('Getting started')

  const kokoroLink = page.getByRole('link', { name: 'Kokoro speech' })
  await suitecut.highlight(kokoroLink, { borderColor: '#71352e', durationMs: 1_000 })
  await kokoroLink.click()
  await expect(page.getByRole('heading', { level: 1 })).toHaveText('Kokoro speech')
  await suitecut.narrate(
    'Kokoro runs locally through WebAssembly, with no hosted speech service.',
    {
      provider: 'kokoro',
      voice: 'af_heart',
    },
  )
  await suitecut.zoom(page.getByRole('heading', { name: 'Choose a voice' }), {
    scale: 1.12,
    holdMs: 850,
  })
  await suitecut.checkpoint('Kokoro speech')
  await suitecut.hold(600)
})
