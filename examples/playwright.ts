import { defineSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: { colorScheme: 'light' },
  capture: {
    viewport: { width: 1280, height: 720 },
    size: { width: 1280, height: 720 },
    framesPerSecond: 30,
    quality: 90,
  },
  output: {
    directory: '.suitecut/playwright-example',
    manifestPath: '.suitecut/playwright-example.json',
    pathKind: 'manifest-relative',
  },
})

const result = await record('plain Playwright recording', async ({ page, suitecut }) => {
  await page.setContent(`
    <!doctype html>
    <html lang="en">
      <head>
        <meta charset="utf-8" />
        <title>SuiteCut plain Playwright example</title>
        <style>
          body { font: 18px system-ui; margin: 0; padding: 64px; }
          main { max-width: 720px; margin: 0 auto; }
          button { font: inherit; padding: 12px 18px; }
          #status { margin-top: 24px; }
        </style>
      </head>
      <body>
        <main>
          <h1>Record a browser flow</h1>
          <p>This script owns Playwright's browser lifecycle. There is no test runner.</p>
          <button type="button">Create report</button>
          <p id="status" aria-live="polite">No report yet.</p>
          <script>
            document.querySelector('button').addEventListener('click', () => {
              document.querySelector('#status').textContent = 'Report created.'
            })
          </script>
        </main>
      </body>
    </html>
  `)

  const create = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(create, { durationMs: 700 })
  await suitecut.click(create)
  await suitecut.hold(400)
  await suitecut.checkpoint('Report created')
})

console.log(`Manifest: ${result.manifestPath}`)
