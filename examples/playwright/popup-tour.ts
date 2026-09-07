import { defineSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  browserName: 'chromium',
  capture: {
    viewport: { width: 960, height: 540 },
    size: { width: 960, height: 540 },
    framesPerSecond: 30,
  },
  output: {
    directory: '.suitecut/playwright-popup-tour',
    manifestPath: '.suitecut/playwright-popup-tour.json',
    pathKind: 'manifest-relative',
  },
})

const result = await record('popup and opener recording', async ({ page, suitecut }) => {
  await page.setContent(`
    <!doctype html>
    <html lang="en">
      <head>
        <meta charset="utf-8" />
        <title>Campaign forecast</title>
        <style>
          body { min-height: 100vh; display: grid; place-items: center; margin: 0; color: #e2e8f0; background: #07111f; font: 18px system-ui; }
          main { width: 640px; padding: 44px; border: 1px solid #334155; border-radius: 24px; background: #0f172a; }
          h1 { margin: 0 0 12px; font-size: 40px; }
          p { color: #94a3b8; }
          a { display: inline-block; margin-top: 20px; padding: 13px 18px; border-radius: 11px; color: white; background: #7c3aed; text-decoration: none; font-weight: 750; }
        </style>
      </head>
      <body>
        <main>
          <h1>Campaign forecast</h1>
          <p>Open the share view without leaving the editor.</p>
          <a target="_blank" href="about:blank">Open preview</a>
        </main>
      </body>
    </html>
  `)

  const popupPromise = page.waitForEvent('popup')
  await suitecut.click(page.getByRole('link', { name: 'Open preview' }))
  const popup = await popupPromise
  await popup.setContent(`
    <!doctype html>
    <html lang="en">
      <head>
        <meta charset="utf-8" />
        <title>Share preview</title>
        <style>
          body { min-height: 100vh; display: grid; place-items: center; margin: 0; color: #f8fafc; background: linear-gradient(145deg, #172554, #312e81); font: 18px system-ui; }
          article { width: 620px; padding: 42px; border: 1px solid #818cf855; border-radius: 24px; background: #0f172acc; }
          h1 { margin: 0 0 12px; font-size: 36px; }
          p { color: #c7d2fe; }
          .total { margin-top: 28px; padding: 20px; border-radius: 14px; background: #312e81; font-size: 28px; font-weight: 800; }
        </style>
      </head>
      <body>
        <article>
          <h1>Share preview</h1>
          <p>SuiteCut records this popup as a second page on the same timeline.</p>
          <div class="total">$48,200 projected</div>
        </article>
      </body>
    </html>
  `)

  suitecut.selectPage(popup)
  const total = popup.locator('.total')
  await suitecut.highlight(total, { borderColor: '#A5B4FC', durationMs: 900 })
  await suitecut.checkpoint('Popup forecast')
  await popup.close()

  suitecut.selectPage(page)
  await suitecut.hold(400)
  await suitecut.checkpoint('Returned to editor')
})

console.log(`Manifest: ${result.manifestPath}`)
