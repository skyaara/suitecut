import { join } from 'node:path'
import process from 'node:process'

import { record, renderSuiteCut } from 'suitecut'

// Shops for a keyboard on eBay: a search typed into the real box, a highlight on
// the results, a listing opened, its photo zoomed and its price pointed at, all
// narrated. Read-only: no sign-in, nothing added to a cart.
// Set SUITECUT_NATIVE_EXECUTABLE to the built engine first.
const output = process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples'

const run = await record(
  'Shop on eBay',
  async ({ page, suitecut }) => {
    await page.goto('https://www.ebay.com/', { waitUntil: 'domcontentloaded' })
    const search = page.getByRole('combobox', { name: 'Search for anything' })
    await search.waitFor()
    await suitecut.narrate('A live eBay store, driven from a script.')
    await suitecut.type(search, 'mechanical keyboard')
    await page.keyboard.press('Enter')

    // The first real keyboard: not a "Shop on eBay" placeholder, a keypad or keycaps.
    const listing = page
      .locator('a[href*="/itm/"]')
      .filter({ hasText: /mechanical (gaming )?keyboard/i })
      .filter({ hasNotText: /keypad|key ?caps?|Shop on eBay/i })
      .first()
    await listing.waitFor()
    await suitecut.highlight(page.locator('h1').first(), { label: 'results' })
    await suitecut.narrate('Real typing and a real search. Now open a listing.')
    const href = await listing.getAttribute('href')
    if (!href) throw new Error('The listing has no link')
    await suitecut.hover(listing)
    await page.goto(href, { waitUntil: 'domcontentloaded' })

    const price = page.locator('.x-price-primary').first()
    await price.waitFor()
    await suitecut.zoom(page.locator('.ux-image-carousel-item img').first(), {
      scale: 1.25,
      holdMs: 1400,
    })
    await suitecut.highlight(price, { label: 'price' })
    await suitecut.narrate('Zoom in on the photo, and point at the price.')
    await suitecut.hold(800)
  },
  { native: { width: 1280, height: 720 }, output: { directory: output } },
)

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: join(output, 'ebay.mp4'),
})
