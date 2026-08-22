import { expect, test } from 'suitecut'

test('records a popup and returns to its opener', async ({ page, suitecut }) => {
  await page.setViewportSize({ width: 960, height: 540 })
  const popupHtml = `
    <style>
      body { margin:0; min-height:100vh; display:grid; place-items:center; color:#f8fafc; background:linear-gradient(145deg,#172554,#312e81); font:18px system-ui; }
      article { width:620px; padding:42px; border:1px solid #818cf855; border-radius:24px; background:#0f172acc; box-shadow:0 30px 90px #02061799; }
      h1 { margin:0 0 12px; font-size:36px; } p { color:#c7d2fe; }
      .total { margin-top:28px; padding:20px; border-radius:14px; background:#312e81; font-size:28px; font-weight:800; }
    </style>
    <article><h1>Share preview</h1><p>This clean preview opened in a second page.</p><div class="total">$48,200 projected</div></article>
  `
  await page.setContent(`
    <style>
      body { margin:0; min-height:100vh; display:grid; place-items:center; color:#e2e8f0; background:#07111f; font:18px system-ui; }
      main { width:640px; padding:44px; border:1px solid #334155; border-radius:24px; background:#0f172a; }
      h1 { font-size:40px; margin:0 0 12px; } p { color:#94a3b8; }
      a { display:inline-block; margin-top:20px; padding:13px 18px; border-radius:11px; color:white; background:#7c3aed; text-decoration:none; font-weight:750; }
    </style>
    <main><h1>Campaign forecast</h1><p>Open the share view without leaving the editor.</p><a target="_blank" href="about:blank">Open preview</a></main>
  `)

  suitecut.narrate('The editor opens its share preview in a second page.')
  const [popup] = await Promise.all([
    page.waitForEvent('popup'),
    page.getByRole('link', { name: 'Open preview' }).click(),
  ])
  await popup.setContent(popupHtml)
  suitecut.selectPage(popup)
  suitecut.narrate('SuiteCut follows the popup and records it on the same timeline.')
  const total = popup.locator('.total')
  await expect(total).toContainText('$48,200')
  await suitecut.highlight(total, { borderColor: '#A5B4FC', durationMs: 1_000 })
  await suitecut.checkpoint('Popup forecast')
  await popup.close()

  suitecut.narrate('Closing the preview returns the recording to the editor.')
  await expect(page.getByRole('heading')).toHaveText('Campaign forecast')
  await suitecut.checkpoint('Returned to editor')
})
