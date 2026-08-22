import { expect, test } from '../../dist/index.js'

test.describe('SuiteCut documentation site', () => {
  test('presents the product and reaches every manual section', async ({ page }) => {
    await page.goto('./')

    await expect(page).toHaveTitle('SuiteCut · Playwright tests, cut like films')
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('Tests, cut like films.')
    await expect(page.getByText('Kokoro voice')).toBeVisible()
    const demo = page.getByLabel('SuiteCut field manual demo')
    await expect(demo).toBeVisible()
    await expect(page.getByText('4K · 60 fps')).toBeVisible()

    const videoSource = await page.locator('video source').getAttribute('src')
    expect(videoSource).toBe('/suitecut/demo/suitecut-field-manual.mp4')
    await expect
      .poll(async () =>
        demo.evaluate((element) => {
          if (!(element instanceof HTMLVideoElement)) return { height: 0, width: 0 }
          return { height: element.videoHeight, width: element.videoWidth }
        }),
      )
      .toEqual({ height: 2160, width: 3840 })

    await page.getByRole('link', { name: 'Read the field manual' }).click()
    await expect(page).toHaveURL(/\/suitecut\/docs\/$/u)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('Getting started')

    for (const section of ['Fixture API', 'Kokoro speech', 'Examples']) {
      await page.getByRole('link', { name: section }).click()
      await expect(page.getByRole('heading', { level: 1 })).toHaveText(section)
    }
  })

  test('keeps the mobile layout inside the viewport', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 })
    await page.goto('./')

    const bodyWidth = await page.locator('body').evaluate((body) => body.scrollWidth)
    expect(bodyWidth).toBeLessThanOrEqual(390)
    await expect(page.getByRole('link', { name: 'Read the field manual' })).toBeVisible()
    await expect(page.getByText('Render ledger')).toBeVisible()
  })

  test('has a useful not-found page', async ({ page }) => {
    const response = await page.goto('./missing-reel')

    expect(response?.status()).toBe(404)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('That page is not in the cut.')
  })
})
