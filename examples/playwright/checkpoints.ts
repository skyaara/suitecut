import { record } from 'suitecut'

const result = await record(
  'video checkpoints',
  async ({ page, suitecut }) => {
    await page.setContent(`
      <!doctype html>
      <html lang="en">
        <head>
          <meta charset="utf-8" />
          <title>SuiteCut checkpoint example</title>
          <style>
            html, body { margin: 0; }
            body { font: 18px system-ui; color: #172033; background: #f5f7fb; }
            header { padding: 48px; color: white; background: #172033; }
            main { min-height: 900px; padding: 48px; }
            section { max-width: 720px; padding: 28px; background: white; border: 1px solid #d9dfeb; }
          </style>
        </head>
        <body>
          <header><h1>Video checkpoints</h1></header>
          <main>
            <section>
              <h2>Recorded evidence</h2>
              <p>Each checkpoint is cut from the same page screencast used by the final render.</p>
            </section>
          </main>
        </body>
      </html>
    `)

    await suitecut.checkpoint('Default checkpoint')
    await suitecut.checkpoint('Short checkpoint', { durationMs: 250 })
  },
  {
    browserName: 'chromium',
    capture: {
      viewport: { width: 640, height: 360 },
      size: { width: 640, height: 360 },
      framesPerSecond: 30,
    },
    output: {
      directory: '.suitecut/playwright-checkpoints',
      manifestPath: '.suitecut/playwright-checkpoints.json',
      pathKind: 'manifest-relative',
    },
  },
)

const attempt = result.manifest.tests[0]?.attempts[0]
const checkpoints = attempt?.artifacts.filter((artifact) => artifact.role === 'checkpoint') ?? []
if (
  checkpoints.length !== 2 ||
  checkpoints.some((artifact) => artifact.contentType !== 'video/webm')
) {
  throw new Error('Expected two WebM checkpoint artifacts')
}

console.log(`Manifest: ${result.manifestPath}`)
console.log(`Checkpoint videos: ${checkpoints.map((artifact) => artifact.path).join(', ')}`)
