import { expect, test } from '../../dist/test.js'

test.describe('SuiteCut documentation site', () => {
  test('opens with the standalone recorder and keeps the v1.0 API copyable', async ({ page }) => {
    await page.goto('./')

    await expect(page).toHaveTitle('SuiteCut · Product videos from Playwright')
    await expect(page.locator('h1.visually-hidden')).toHaveText(
      'Turn a Playwright recording into a product video.',
    )
    await expect(page.locator('video')).toHaveCount(0)
    await expect(page.locator('.home-reference-code')).toContainText(
      "import { record } from 'suitecut'",
    )
    await expect(page.locator('.home-reference-code')).not.toContainText('suitecut/test')
    const syntaxColors = await page
      .locator('.home-reference-code .astro-code span[style*="color"]')
      .evaluateAll((tokens) => [...new Set(tokens.map((token) => getComputedStyle(token).color))])
    expect(syntaxColors.length).toBeGreaterThan(2)

    const logo = page.getByRole('link', { name: 'SuiteCut home' })
    await expect(logo.locator('.logo-mark')).toBeVisible()
    await expect(logo.locator('.logo-mark')).toHaveAttribute('viewBox', '0 0 32 32')
    await expect(logo.getByText('SuiteCut', { exact: true })).toBeVisible()

    const colors = await page.locator('body').evaluate((body) => {
      const styles = getComputedStyle(body)
      return { background: styles.backgroundColor, text: styles.color }
    })
    expect(colors).toEqual({ background: 'rgb(17, 17, 17)', text: 'rgb(245, 245, 245)' })

    await page.goto('./docs/api')
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('API and CLI')
    for (const section of [
      'Public exports',
      'Recording options',
      'Playwright Test',
      'Fixture methods',
      'Capture options',
      'Reporter options',
      'Renderer',
      'Audio plugin helpers',
      'Command-line flags',
    ]) {
      await expect(page.getByRole('heading', { name: section, exact: true })).toBeVisible()
    }
    for (const method of [
      'selectPage(page): void',
      'narrate(text, options?): Promise<void>',
      'scrollTop(options?): Promise<void>',
    ]) {
      await expect(page.getByRole('heading', { name: method, exact: true })).toBeVisible()
    }
    const codeBlockCount = await page.locator('.code-block').count()
    expect(codeBlockCount).toBe(11)
    await expect(page.getByRole('button', { name: /^Copy /u })).toHaveCount(codeBlockCount)

    await page.getByRole('link', { name: 'Docs', exact: true }).click()
    await expect(page).toHaveURL(/\/docs$/u)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('Getting started')
    await expect(
      page.getByText("import { defineSuiteCut } from 'suitecut'", { exact: false }),
    ).toBeVisible()

    const docsPresentation = await page.locator('.docs-shell').evaluate((shell) => {
      const navigation = shell.querySelector('.docs-nav')
      const typeScriptBlock = shell.querySelector(
        '.code-block[aria-label="TypeScript"] .astro-code',
      )
      const tokenColors = typeScriptBlock
        ? [...typeScriptBlock.querySelectorAll('span[style*="color"]')].map(
            (token) => getComputedStyle(token).color,
          )
        : []

      return {
        columns: getComputedStyle(shell).gridTemplateColumns,
        navigationPosition: navigation ? getComputedStyle(navigation).position : null,
        language: typeScriptBlock?.getAttribute('data-language'),
        tokenColors: [...new Set(tokenColors)],
      }
    })
    expect(docsPresentation.columns).not.toBe('none')
    expect(docsPresentation.navigationPosition).toBe('sticky')
    expect(docsPresentation.language).toBe('typescript')
    expect(docsPresentation.tokenColors.length).toBeGreaterThan(2)

    const subcategoryCounts = await page
      .locator('.docs-nav-category')
      .evaluateAll((categories) =>
        categories.map((category) => category.querySelectorAll('ul a').length),
      )
    expect(subcategoryCounts).toEqual([6, 10, 5, 6, 4])

    for (const section of ['Live streaming', 'API and CLI', 'Kokoro speech', 'Audio plugins']) {
      await page
        .getByLabel('Documentation sections')
        .getByRole('link', { name: section, exact: true })
        .click()
      await expect(page.getByRole('heading', { level: 1 })).toHaveText(section)
    }

    await page
      .getByRole('navigation', { name: 'Primary navigation' })
      .getByRole('link', { name: 'Examples', exact: true })
      .click()
    await expect(page).toHaveURL(/\/examples$/u)
    await expect(page.locator('.docs-nav')).toHaveCount(0)
    await expect(page.getByRole('navigation', { name: 'Example sections' })).toBeVisible()
    await expect(page.getByRole('link', { name: 'examples/playwright.ts' })).toBeVisible()
    await expect(
      page.getByText("import { defineSuiteCut } from 'suitecut'", { exact: false }),
    ).toBeVisible()

    await page
      .getByLabel('Example sections')
      .getByRole('link', { name: 'Browser matrix', exact: true })
      .click()
    await expect(page).toHaveURL(/\/examples#browser-matrix$/u)
    await expect(
      page.getByRole('heading', { name: 'Playwright Test in three browsers' }),
    ).toBeVisible()
  })

  test('keeps the homepage and API reference inside a mobile viewport', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 })
    await page.goto('./')

    expect(await page.locator('body').evaluate((body) => body.scrollWidth)).toBeLessThanOrEqual(390)
    await expect(page.locator('h1.visually-hidden')).toBeAttached()
    await expect(page.locator('video')).toHaveCount(0)
    await expect(page.locator('.home-reference-code pre')).toBeVisible()
    await expect(page.getByRole('link', { name: 'Docs', exact: true })).toBeVisible()
    await expect(page.getByRole('link', { name: 'GitHub', exact: true })).toBeVisible()

    await page.goto('./docs/api')
    const apiSizes = await page.locator('body').evaluate((body) => {
      const code = body.querySelector('.docs-article pre')
      return {
        body: body.scrollWidth,
        codeClient: code?.clientWidth ?? 0,
        codeScroll: code?.scrollWidth ?? 0,
      }
    })
    expect(apiSizes.body).toBeLessThanOrEqual(390)
    expect(apiSizes.codeScroll).toBeGreaterThanOrEqual(apiSizes.codeClient)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('API and CLI')

    await page.goto('./examples')
    const examplesSizes = await page.locator('body').evaluate((body) => ({
      body: body.scrollWidth,
      indexDisplay: getComputedStyle(body.querySelector('.page-index') ?? body).display,
    }))
    expect(examplesSizes.body).toBeLessThanOrEqual(390)
    expect(examplesSizes.indexDisplay).toBe('flex')

    await page.goto('./docs/examples')
    await expect(page).toHaveURL(/\/examples$/u)
  })

  test('serves voice samples beside their exact settings', async ({ page, request }) => {
    await page.goto('./docs/kokoro')

    const kokoroSources = await page
      .locator('audio')
      .evaluateAll((audio) => audio.map((element) => element.getAttribute('src')))
    expect(kokoroSources).toEqual(['/audio/kokoro-af-heart.wav', '/audio/kokoro-af-bella.wav'])
    await expect
      .poll(() =>
        page.locator('audio').evaluateAll((audio) =>
          audio.every((element) => {
            const duration = (element as HTMLAudioElement).duration
            return Number.isFinite(duration) && duration > 0
          }),
        ),
      )
      .toBe(true)
    await expect(page.getByText("voice: 'af_bella'", { exact: false }).first()).toBeVisible()

    await page.goto('./docs/audio-plugins')
    const piperSource = await page.locator('audio').getAttribute('src')
    expect(piperSource).toBe('/audio/piper-amy.wav')
    await expect
      .poll(() =>
        page.locator('audio').evaluate((audio) => {
          const duration = (audio as HTMLAudioElement).duration
          return Number.isFinite(duration) && duration > 0
        }),
      )
      .toBe(true)
    await expect(page.getByText("voice: 'default'", { exact: false }).first()).toBeVisible()
    await expect(
      page.getByText('speakerIds: { narrator: 2 }', { exact: false }).first(),
    ).toBeVisible()

    for (const source of [...kokoroSources, piperSource]) {
      expect(source).not.toBeNull()
      const response = await request.get(source ?? '')
      expect(response.ok()).toBe(true)
      expect(response.headers()['content-type']).toContain('audio/wav')
    }
  })

  test('has a plain not-found page', async ({ page }) => {
    const response = await page.goto('./missing-page')

    expect(response?.status()).toBe(404)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText('Page not found.')
    await expect(page.getByRole('link', { name: 'Return home' })).toBeVisible()
  })
})
