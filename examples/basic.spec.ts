import { expect, test } from 'suitecut/test'

test('records a narrated flow', async ({ page, suitecut }) => {
  test.setTimeout(120_000)
  await page.setContent(`
    <main>
      <h1>Example Domain</h1>
      <p>This page is local to the test.</p>
    </main>
  `)
  await suitecut.narrate('The example page is open.')
  await expect(page.getByRole('heading')).toHaveText('Example Domain')
  await suitecut.checkpoint('Example page loaded')
})
