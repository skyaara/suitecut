import { expect, test } from 'suitecut/test'

test('zooms around highlights and clicks while preserving real interactions', async ({
  page,
  suitecut,
}) => {
  await page.setViewportSize({ width: 800, height: 500 })
  await page.setContent(`
    <style>
      body { margin: 0; background: #f8fafc; font: 18px system-ui; }
      main { margin: 140px 240px; width: 320px; }
      button { padding: 16px 24px; background: #7c3aed; color: white; border: 0; border-radius: 10px; }
    </style>
    <main><h2>Ready to publish</h2><button onclick="document.querySelector('h2').textContent='Published'">Publish project</button></main>
  `)
  const button = page.getByRole('button')
  const originalBounds = await button.boundingBox()
  const highlight = suitecut.highlight(button, {
    durationMs: 500,
    zoom: { scale: 1.25, enter: { durationMs: 200 }, holdMs: 0, exit: { durationMs: 100 } },
  })
  await expect(page.locator('[data-suitecut-highlight]')).toBeVisible()
  expect(await button.boundingBox()).toEqual(originalBounds)
  await highlight
  await expect(page.locator('[data-suitecut-highlight]')).toHaveCount(0)
  await suitecut.click(button, { zoom: true, moveDurationMs: 100, settleMs: 20 })
  await expect(page.getByRole('heading')).toHaveText('Published')
  expect(await button.boundingBox()).toEqual(originalBounds)
  await suitecut.hold(250)
})

test('keeps action zoom opt-in', async ({ page, suitecut }) => {
  await page.setContent('<button>Continue</button>')
  await suitecut.highlight(page.getByRole('button'), { durationMs: 100, zoom: false })
  await suitecut.click(page.getByRole('button'), { moveDurationMs: 0, settleMs: 0 })
})

test('allows a zoomed click to navigate', async ({ page, suitecut }) => {
  await page.setContent('<button onclick="location.href=\'about:blank?zoomed\'">Continue</button>')
  await suitecut.click(page.getByRole('button'), { zoom: true, moveDurationMs: 0 })
  await expect(page).toHaveURL('about:blank?zoomed')
})
