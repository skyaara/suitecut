import { expect, test } from 'suitecut'

test('records browser-rendered direction and application animation', async ({ page, suitecut }) => {
  await page.setContent(`
    <style>
      body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #07111f; }
      button { padding: 14px 20px; border: 0; border-radius: 12px; color: white; background: #7c3aed; }
      #panel {
        margin-top: 24px;
        padding: 28px;
        border-radius: 16px;
        color: white;
        background: #0f766e;
        opacity: 0;
        transform: translateY(28px) scale(.96);
        transition: opacity 320ms ease, transform 320ms cubic-bezier(.22,.8,.22,1);
      }
      #panel.visible { opacity: 1; transform: translateY(0) scale(1); }
    </style>
    <main>
      <button type="button">Reveal result</button>
      <section id="panel">This transition is rendered by the application.</section>
    </main>
    <script>
      document.querySelector('button').addEventListener('click', () => {
        document.querySelector('#panel').classList.add('visible')
      })
    </script>
  `)

  const narration = suitecut.narrate('SuiteCut follows the browser clock.', {
    provider: 'macos-say',
    caption: 'SuiteCut follows the browser clock.',
  })
  await expect(page.locator('[data-suitecut-caption]')).toBeVisible()
  await narration
  await expect(page.locator('[data-suitecut-caption]')).toHaveCount(0)

  const button = page.getByRole('button', { name: 'Reveal result' })
  const highlight = suitecut.highlight(button, {
    borderColor: '#2DD4BF',
    durationMs: 360,
    enter: { type: 'fade-scale', durationMs: 120 },
    exit: { type: 'fade', durationMs: 100 },
  })
  await expect(page.locator('[data-suitecut-highlight]')).toBeVisible()
  await highlight
  await expect(page.locator('[data-suitecut-highlight]')).toHaveCount(0)

  const actionStartedAt = Date.now()
  await suitecut.click(button, {
    moveDurationMs: 180,
    settleMs: 40,
    animationTimeoutMs: 1_000,
  })
  expect(Date.now() - actionStartedAt).toBeGreaterThanOrEqual(480)
  await expect(page.locator('#panel')).toHaveCSS('opacity', '1')
  await suitecut.hold(180)
})

test('records native element and page scrolling', async ({ page, suitecut }) => {
  await page.setViewportSize({ width: 800, height: 500 })
  await page.setContent(`
    <style>
      body { margin: 0; background: #f8fafc; color: #0f172a; }
      section { min-height: 1200px; display: grid; place-items: center; }
      #target { padding: 40px; border-radius: 20px; background: #facc15; font: 700 32px system-ui; }
    </style>
    <header>Top of page</header>
    <section><div id="target">Native scroll target</div></section>
  `)

  const target = page.locator('#target')
  await suitecut.scrollTo(target, { settleMs: 0 })
  await expect(target).toBeInViewport()
  expect(await page.evaluate(() => window.scrollY)).toBeGreaterThan(300)

  await suitecut.scrollTop({ settleMs: 0 })
  expect(await page.evaluate(() => window.scrollY)).toBe(0)
})

test('allows a recorded click to replace the document', async ({ page, suitecut }) => {
  await page.setContent(`
    <button type="button" onclick="location.href='about:blank?suitecut-navigation'">
      Continue
    </button>
  `)

  await suitecut.click(page.getByRole('button', { name: 'Continue' }))
  await expect(page).toHaveURL('about:blank?suitecut-navigation')
})
