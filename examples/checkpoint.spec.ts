import { expect, test } from 'suitecut/test'

test('captures checkpoint video windows', async ({ page, suitecut }) => {
  await page.setViewportSize({ width: 640, height: 360 })
  await page.setContent(`
    <style>
      html, body { margin: 0; }
      main { height: 900px; }
    </style>
    <main>Checkpoint page</main>
  `)

  const startedAt = Date.now()
  await suitecut.checkpoint('Default checkpoint')
  await suitecut.checkpoint('Short checkpoint', { durationMs: 250 })
  expect(Date.now() - startedAt).toBeGreaterThanOrEqual(700)
})
