import { join, resolve } from 'node:path'
import process from 'node:process'

import { launchBrowser } from 'suitecut/browser'

// A Playwright-style shopping run in the headless suitecut engine, with the
// context's video turned on: roles, real typing, a listing read and history, and a
// WebM of all of it. Read-only: no sign-in, nothing added to a cart. The same API
// is published on its own as @suitecut/browser-control.
// Set SUITECUT_NATIVE_EXECUTABLE to the built engine first.
const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the built engine')
const output = resolve(process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples')

const browser = await launchBrowser({ executablePath, headless: true })
try {
  const context = await browser.newContext({
    recordVideo: { dir: output, size: { width: 1280, height: 720 } },
  })
  const page = await context.newPage()
  // 'commit' returns as the page starts arriving; each step below waits for what it needs.
  await page.goto('https://www.ebay.com/', { waitUntil: 'commit' })

  // Search the way a person does: type into the box, then press Enter.
  const search = page.getByRole('combobox', { name: 'Search for anything' })
  await search.pressSequentially('mechanical keyboard', { delay: 80, timeout: 10_000 })
  await page.waitForTimeout(600)
  await page.keyboard.press('Enter')

  // Find the first real keyboard in the results, then open it.
  const listing = page
    .locator('a[href*="/itm/"]')
    .filter({ hasText: /mechanical (gaming )?keyboard/i })
    .filter({ hasNotText: /keypad|key ?caps?|Shop on eBay/i })
    .first()
  await listing.waitFor()
  await listing.scrollIntoViewIfNeeded()
  await page.waitForTimeout(1500)
  const href = await listing.getAttribute('href')
  if (!href) throw new Error('The listing has no link')
  await page.goto(href, { waitUntil: 'commit' })

  // Read its title and price, and look further down the listing.
  await page.locator('.x-price-primary').first().waitFor()
  const title = await page.getByRole('heading', { level: 1 }).first().textContent()
  const price = await page.locator('.x-price-primary').first().textContent()
  console.log(`${title?.trim()}: ${price?.trim()}`)
  await page.waitForTimeout(1500)
  await page.mouse.wheel(0, 500)
  await page.waitForTimeout(1500)

  // And back to the results.
  await page.goBack({ waitUntil: 'commit' })
  await listing.waitFor()
  await page.waitForTimeout(1500)

  const video = page.video()
  await context.close()
  if (video) {
    const path = join(output, 'browser-control.webm')
    await video.saveAs(path)
    console.log(`Video: ${path}`)
  }
} finally {
  await browser.close()
}
