import { expect, test } from 'suitecut/test'

test('records browser-rendered direction and application animation', async ({ page, suitecut }) => {
  test.setTimeout(120_000)
  await page.setContent(`
    <style>
      body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #07111f; }
      button {
        padding: 14px 20px;
        border: 0;
        border-radius: 12px;
        color: white;
        background: #7c3aed;
        transition: transform 180ms ease-out;
      }
      button:hover { transform: scale(1.04); }
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
    provider: 'kokoro',
    caption: 'SuiteCut follows the browser clock.',
  })
  await expect(page.locator('[data-suitecut-caption]')).toBeVisible({ timeout: 90_000 })
  const captionPresentation = await page.locator('[data-suitecut-caption]').evaluate((caption) => {
    const style = getComputedStyle(caption)
    return {
      animationCount: caption.getAnimations().length,
      backgroundColor: style.backgroundColor,
      opacity: style.opacity,
    }
  })
  expect(captionPresentation).toEqual({
    animationCount: 0,
    backgroundColor: 'rgb(2, 6, 23)',
    opacity: '1',
  })
  await narration
  await expect(page.locator('[data-suitecut-caption]')).toHaveCount(0)

  const button = page.getByRole('button', { name: 'Reveal result' })
  const hoverStartedAt = Date.now()
  await suitecut.hover(button, { moveDurationMs: 0, settleMs: 0, animationTimeoutMs: 1_000 })
  expect(Date.now() - hoverStartedAt).toBeGreaterThanOrEqual(150)
  await expect(button).toHaveCSS('transform', 'matrix(1.04, 0, 0, 1.04, 0, 0)')

  const highlight = suitecut.highlight(button, {
    borderColor: '#2DD4BF',
    label: 'Reveal the result',
    durationMs: 360,
    enter: { type: 'fade-scale', durationMs: 120 },
    exit: { type: 'fade', durationMs: 100 },
  })
  await expect(page.locator('[data-suitecut-highlight]')).toBeVisible()
  await expect(page.locator('[data-suitecut-highlight-label]')).toHaveText('Reveal the result')
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

test('types into a form field one character at a time', async ({ page, suitecut }) => {
  await page.setContent(`
    <label>
      Project name
      <input value="Draft" />
    </label>
    <script>
      window.typedValues = []
      document.querySelector('input').addEventListener('input', (event) => {
        window.typedValues.push(event.currentTarget.value)
      })
    </script>
  `)

  const projectName = page.getByLabel('Project name')
  await suitecut.type(projectName, 'SuiteCut', {
    settleMs: 0,
  })

  await expect(projectName).toHaveValue('SuiteCut')
  expect(
    await page.evaluate(() => (window as typeof window & { typedValues: string[] }).typedValues),
  ).toEqual(['', 'S', 'Su', 'Sui', 'Suit', 'Suite', 'SuiteC', 'SuiteCu', 'SuiteCut'])
})

test('keeps presentation bounds aligned through document CSS zoom', async ({ page, suitecut }) => {
  await page.setViewportSize({ width: 800, height: 500 })
  await page.setContent(`
    <style>
      html { zoom: 1.65; }
      body { margin: 0; }
      #target {
        position: absolute;
        left: 100px;
        top: 80px;
        width: 200px;
        height: 40px;
        background: #7c3aed;
      }
    </style>
    <button id="target" type="button">Zoomed target</button>
  `)

  const target = page.locator('#target')
  const highlight = suitecut.highlight(target, {
    durationMs: 500,
    paddingPx: 12,
    borderWidthPx: 3,
    enter: { type: 'fade-scale', durationMs: 120 },
    exit: { type: 'none' },
  })
  await expect(page.locator('[data-suitecut-highlight]')).toBeVisible()

  const bounds = await page.evaluate(() => {
    const host = document.documentElement.querySelector<HTMLElement>('[data-suitecut-presentation]')
    const targetElement = document.querySelector<HTMLElement>('#target')
    const highlightElement = host?.shadowRoot?.querySelector<HTMLElement>(
      '[data-suitecut-highlight]',
    )
    if (host === null || targetElement === null || highlightElement == null) {
      throw new Error('Expected the target and SuiteCut highlight to be present')
    }
    const targetRect = targetElement.getBoundingClientRect()
    const highlightRect = highlightElement.getBoundingClientRect()
    return {
      target: {
        x: targetRect.x,
        y: targetRect.y,
        width: targetRect.width,
        height: targetRect.height,
      },
      highlight: {
        x: highlightRect.x,
        y: highlightRect.y,
        width: highlightRect.width,
        height: highlightRect.height,
      },
      presentationZoom: Number.parseFloat(getComputedStyle(host).zoom),
    }
  })

  expect(bounds.highlight.x).toBeCloseTo(bounds.target.x - 12, 4)
  expect(bounds.highlight.y).toBeCloseTo(bounds.target.y - 12, 4)
  expect(bounds.highlight.width).toBeCloseTo(bounds.target.width + 24, 4)
  expect(bounds.highlight.height).toBeCloseTo(bounds.target.height + 24, 4)
  expect(bounds.presentationZoom).toBeCloseTo(1 / 1.65, 5)
  await highlight
})

test('can highlight tight rendered content instead of a full-width block', async ({
  page,
  suitecut,
}) => {
  await page.setViewportSize({ width: 800, height: 500 })
  await page.setContent(`
    <style>
      html { zoom: 1.65; }
      body { min-height: 2200px; margin: 0; }
      h2 { width: 720px; margin: 1000px 40px 0; font: 700 32px/1.25 system-ui; }
    </style>
    <h2>Short heading</h2>
  `)

  const heading = page.getByRole('heading', { name: 'Short heading' })
  await heading.scrollIntoViewIfNeeded()
  const highlight = suitecut.highlight(heading, {
    geometry: 'content',
    durationMs: 400,
    paddingPx: 10,
    borderWidthPx: 3,
    borderColor: '#ef4444',
    label: 'Tight content',
    enter: { type: 'none' },
    exit: { type: 'none' },
  })
  await expect(page.locator('[data-suitecut-highlight]')).toBeVisible()

  const widths = await page.evaluate(() => {
    const host = document.documentElement.querySelector<HTMLElement>('[data-suitecut-presentation]')
    const headingElement = document.querySelector<HTMLElement>('h2')
    const highlightElement = host?.shadowRoot?.querySelector<HTMLElement>(
      '[data-suitecut-highlight]',
    )
    if (headingElement === null || highlightElement == null) throw new Error('Missing test element')
    const range = document.createRange()
    range.selectNodeContents(headingElement)
    const contentWidth = range.getBoundingClientRect().width
    range.detach()
    return {
      heading: headingElement.getBoundingClientRect().width,
      content: contentWidth,
      highlight: highlightElement.getBoundingClientRect().width,
    }
  })

  expect(widths.heading).toBeCloseTo(720 * 1.65, 4)
  expect(widths.highlight).toBeCloseTo(widths.content + 20, 4)
  await highlight
  await suitecut.hold(200)
})

test('scrolls an off-screen zoom target into the viewport', async ({ page, suitecut }) => {
  test.setTimeout(120_000)
  await page.setViewportSize({ width: 640, height: 360 })
  await page.setContent(`
    <style>
      body { margin: 0; }
      #spacer { height: 1000px; }
      #target { width: 120px; height: 40px; }
    </style>
    <div id="spacer"></div>
    <button id="target" type="button">Zoom target</button>
  `)

  const target = page.locator('#target')
  expect((await target.boundingBox())?.y).toBeGreaterThan(360)

  await suitecut.zoom(target, {
    scale: 1.25,
    holdMs: 0,
    enter: { type: 'none' },
    exit: { type: 'none' },
  })

  const box = await target.boundingBox()
  expect(box).not.toBeNull()
  if (box === null) throw new Error('Expected the target to have a bounding box')
  expect(box.y).toBeGreaterThanOrEqual(0)
  expect(box.y + box.height).toBeLessThanOrEqual(360)
  expect(await page.evaluate(() => window.scrollY)).toBeGreaterThan(0)
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
