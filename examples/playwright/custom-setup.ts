import { createServer } from 'node:http'

import { defineSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  browserName: 'chromium',
  capture: {
    viewport: { width: 960, height: 540 },
    framesPerSecond: 30,
  },
  output: {
    directory: '.suitecut/playwright-custom-setup',
    manifestPath: '.suitecut/playwright-custom-setup.json',
    pathKind: 'manifest-relative',
  },
  setup: async ({ onCleanup }) => {
    const server = createServer((_request, response) => {
      response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' })
      response.end(`
        <!doctype html>
        <html lang="en">
          <head>
            <meta charset="utf-8" />
            <title>SuiteCut setup example</title>
            <style>
              body { min-height: 100vh; display: grid; place-items: center; margin: 0; color: #172033; background: #eef2ff; font: 18px system-ui; }
              main { width: 620px; padding: 40px; border: 1px solid #c7d2fe; background: white; }
              button { padding: 12px 18px; font: inherit; }
              #status { color: #166534; font-weight: 700; }
            </style>
          </head>
          <body>
            <main>
              <h1>Server started by setup()</h1>
              <p>The recording callback receives this server URL as a typed value.</p>
              <button type="button">Run import</button>
              <p id="status">Waiting.</p>
              <script>
                document.querySelector('button').addEventListener('click', () => {
                  document.querySelector('#status').textContent = 'Import complete.'
                })
              </script>
            </main>
          </body>
        </html>
      `)
    })

    await new Promise<void>((resolveListen, rejectListen) => {
      const onError = (error: Error): void => rejectListen(error)
      server.once('error', onError)
      server.listen(0, '127.0.0.1', () => {
        server.off('error', onError)
        resolveListen()
      })
    })

    onCleanup(
      () =>
        new Promise<void>((resolveClose, rejectClose) => {
          server.close((error) => {
            if (error === undefined) resolveClose()
            else rejectClose(error)
          })
        }),
    )

    const address = server.address()
    if (address === null || typeof address === 'string') {
      throw new Error('Example server did not bind to a TCP port')
    }
    return { appUrl: `http://127.0.0.1:${String(address.port)}` }
  },
})

const result = await record('typed setup and cleanup', async ({ appUrl, page, suitecut }) => {
  await page.goto(appUrl)
  const runImport = page.getByRole('button', { name: 'Run import' })
  await suitecut.highlight(runImport, { durationMs: 700 })
  await suitecut.click(runImport)
  await page.getByText('Import complete.').waitFor()
  await suitecut.checkpoint('Import complete')
})

console.log(`Manifest: ${result.manifestPath}`)
