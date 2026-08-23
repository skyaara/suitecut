import { expect, test } from 'suitecut'

test('records a narrated flow', async ({ page, suitecut }) => {
  await page.setContent(`
    <main>
      <h1>Example Domain</h1>
      <p>This page is local to the test.</p>
    </main>
  `)
  const narrationCallStartedAt = performance.now()
  await suitecut.narrate('The example page is open.')
  expect(performance.now() - narrationCallStartedAt).toBeLessThan(250)
  await expect(page.getByRole('heading')).toHaveText('Example Domain')
  await suitecut.checkpoint('Example page loaded')
})
