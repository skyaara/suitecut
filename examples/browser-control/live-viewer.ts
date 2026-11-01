import { once } from 'node:events'
import process from 'node:process'

import { createBrowserViewer, launchBrowser } from 'suitecut/browser'

// Streams a headless browser to any device on your network. An agent searches eBay
// while you watch from a phone, tablet or another computer; tap Take control to
// drive it yourself, then Resume AI to hand it back. Read-only: no sign-in, nothing
// added to a cart. Ctrl+C ends it.
// Set SUITECUT_NATIVE_EXECUTABLE to the built engine first.
const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the built engine')

// The viewer streams the engine's own frames, so the browser captures BGRA pixels.
const browser = await launchBrowser({ executablePath, headless: true, pixelFormat: 'bgra' })
try {
  const page = await browser.newPage()
  await page.goto('about:blank')

  // network: true listens on this machine's network addresses as well as loopback.
  // 'auto' fits the page to whichever screen takes control, so a phone gets eBay's
  // phone layout.
  const viewer = await createBrowserViewer({
    browser,
    page,
    network: true,
    resolutionMode: 'auto',
  })
  console.log(`Open this on your phone or another computer:\n  ${viewer.url}`)
  console.log('The link carries its own key, so keep it private.')

  // Each agent step waits while a person has control, then carries on from there.
  await viewer.runAgent(async () => {
    await page.goto('https://www.ebay.com/', { waitUntil: 'commit' })
    await page
      .getByRole('combobox', { name: 'Search for anything' })
      .pressSequentially('mechanical keyboard', { delay: 80, timeout: 10_000 })
    await page.keyboard.press('Enter')
  })
  await viewer.runAgent(async () => {
    const listing = page
      .locator('a[href*="/itm/"]')
      .filter({ hasText: /mechanical (gaming )?keyboard/i })
      .filter({ hasNotText: /keypad|key ?caps?|Shop on eBay/i })
      .first()
    await listing.waitFor()
    await listing.scrollIntoViewIfNeeded()
  })
  console.log('The agent is done; take control from any device. Ctrl+C here to finish.')

  await Promise.race([viewer.closed, browser.closed, once(process, 'SIGINT')])
  await viewer.close()
} finally {
  await browser.close()
}
