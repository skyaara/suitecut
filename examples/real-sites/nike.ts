import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Shops on nike.com without an account: searches, picks a fit, a colour and a
// size, adds the shoe to the bag and changes the quantity. Nothing is bought;
// the bag goes away with the recording's throwaway profile.
// Set SUITECUT_NATIVE_EXECUTABLE first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

const run = await record(
  'Fill a bag on nike.com',
  async ({ page, suitecut }) => {
    await page.goto('https://www.nike.com/w/mens-shoes-nik1zy7ok', {
      waitUntil: 'domcontentloaded',
    })
    // Nike asks visitors outside the US which store to use; this keeps the US one.
    const stay = page.locator('button.current-country-button')
    await stay.waitFor({ timeout: 8_000 }).catch(() => undefined)
    if (await stay.isVisible()) await suitecut.click(stay)

    await suitecut.narrate('This is nike.com, live, with no account signed in.')
    await suitecut.click(page.locator('#nav-search-icon'))
    const search = page.locator('#gn-search-input')
    await suitecut.type(search, 'pegasus')
    await search.press('Enter')
    const card = page.locator(
      'a[data-testid="product-card__img-link-overlay"][href*="pegasus-42-mens"]',
    )
    await suitecut.click(card.first())

    const sizes = page.locator('[data-testid="pdp-grid-selector-item"]')
    await sizes.first().waitFor()
    await suitecut.narrate('Search, open a shoe, then choose the fit, a colour and a size.')
    await suitecut.click(page.locator('label[for="id-Wide"]'))
    await suitecut.click(page.locator('label[for="id-Regular"]'))
    await suitecut.click(page.locator('[data-testid="colorway-link-IB1873-005"]'))
    await page.waitForFunction(() => location.pathname.endsWith('IB1873-005'))
    await sizes.first().waitFor()
    await suitecut.click(
      page.locator('input[name="grid-selector-input"]:not([aria-disabled="true"]) + label').first(),
    )
    await suitecut.click(page.locator('[data-testid="atb-button"]'))
    await suitecut.hold(2000)
    console.log(
      await page.evaluate(() =>
        Array.from(document.querySelectorAll('[role="dialog"], [aria-modal="true"]'))
          .map((e) => e.outerHTML.replace(/<svg.*?<\/svg>/gs, '<svg/>').slice(0, 3000))
          .join('\n\n=====\n\n'),
      ),
    )
  },
  { native: { width: 1280, height: 720 }, output: { directory: output } },
)

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: join(output, 'nike.mp4'),
})
