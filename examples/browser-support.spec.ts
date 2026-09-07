import { expect, test } from 'suitecut/test'

test.use({
  suitecutCapture: {
    viewport: { width: 960, height: 540 },
    framesPerSecond: 30,
    quality: 85,
  },
})

test('records a SuiteCut flow in every supported browser', async ({ page, suitecut }) => {
  await page.setContent(`
    <main style="display:grid;place-content:center;min-height:100vh;font:20px system-ui">
      <button type="button" style="padding:16px 24px">Create report</button>
      <p role="status">Waiting</p>
    </main>
    <script>
      document.querySelector('button').addEventListener('click', () => {
        document.querySelector('[role=status]').textContent = 'Report created'
      })
    </script>
  `)

  const createReport = page.getByRole('button', { name: 'Create report' })
  await suitecut.checkpoint('Browser ready')
  await suitecut.highlight(createReport, { durationMs: 450, label: 'Create report' })
  await suitecut.click(createReport, {
    moveDurationMs: 120,
    settleMs: 80,
    waitForAnimations: false,
  })
  await expect(page.getByRole('status')).toHaveText('Report created')
  await suitecut.hold(200)
})
