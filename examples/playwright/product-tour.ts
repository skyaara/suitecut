import { defineSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: { colorScheme: 'dark' },
  capture: {
    viewport: { width: 1280, height: 720 },
    size: { width: 1280, height: 720 },
    framesPerSecond: 30,
    quality: 100,
    narrationTailMs: 250,
  },
  output: {
    directory: '.suitecut/playwright-product-tour',
    manifestPath: '.suitecut/playwright-product-tour.json',
    pathKind: 'manifest-relative',
  },
})

const result = await record('plain Playwright product tour', async ({ page, suitecut }) => {
  await page.setContent(`
    <!doctype html>
    <html lang="en">
      <head>
        <meta charset="utf-8" />
        <title>Northstar workspace</title>
        <style>
          * { box-sizing: border-box; }
          body { margin: 0; color: #e8eefc; background: #07111f; font: 15px/1.45 system-ui; }
          .shell { display: grid; grid-template-columns: 220px 1fr; min-height: 100vh; }
          aside { padding: 28px 20px; border-right: 1px solid #1d2d43; background: #091523; }
          .brand { margin-bottom: 34px; font-size: 20px; font-weight: 800; }
          nav { display: grid; gap: 8px; color: #8fa4bf; }
          nav span { padding: 10px 12px; }
          nav .active { color: white; background: #14243a; }
          main { padding: 34px 42px 80px; }
          header { display: flex; align-items: center; justify-content: space-between; }
          h1 { margin: 0; font-size: 31px; }
          .sub, .label { color: #8fa4bf; }
          button { padding: 12px 17px; border: 0; border-radius: 10px; color: white; background: #6d28d9; font: inherit; font-weight: 700; }
          .stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 18px; margin: 30px 0; }
          .card { padding: 20px; border: 1px solid #21344d; border-radius: 16px; background: #0c1929; }
          .value { margin-top: 6px; font-size: 28px; font-weight: 800; }
          .workspace { min-height: 430px; padding: 24px; }
          .bars { height: 250px; display: flex; align-items: end; gap: 14px; margin-top: 28px; }
          .bar { flex: 1; border-radius: 7px 7px 2px 2px; background: linear-gradient(#8b5cf6, #4c1d95); }
          #report { display: none; margin-top: 28px; padding: 22px; border-color: #2dd4bf66; background: #0f2b32; }
          #report.visible { display: flex; justify-content: space-between; }
          #report strong { color: #99f6e4; font-size: 18px; }
        </style>
      </head>
      <body>
        <div class="shell">
          <aside>
            <div class="brand">Northstar</div>
            <nav><span class="active">Overview</span><span>Projects</span><span>Reports</span></nav>
          </aside>
          <main>
            <header>
              <div><h1>Good morning, Aakash</h1><p class="sub">Your workspace is up 18.4% this month.</p></div>
              <button id="create" type="button">Create report</button>
            </header>
            <section class="stats">
              <div class="card"><span class="label">Active projects</span><div class="value">24</div></div>
              <div class="card"><span class="label">Review time</span><div class="value">3.2h</div></div>
              <div class="card"><span class="label">Approval rate</span><div class="value">94%</div></div>
            </section>
            <section class="card workspace">
              <strong>Weekly output</strong>
              <div class="bars">
                <div class="bar" style="height: 42%"></div><div class="bar" style="height: 58%"></div>
                <div class="bar" style="height: 48%"></div><div class="bar" style="height: 72%"></div>
                <div class="bar" style="height: 66%"></div><div class="bar" style="height: 84%"></div>
                <div class="bar" style="height: 94%"></div>
              </div>
            </section>
            <section id="report" class="card">
              <div><strong>August performance report</strong><br /><span class="label">12 pages, ready to share</span></div>
              <span>Generated</span>
            </section>
          </main>
        </div>
        <script>
          document.querySelector('#create').addEventListener('click', () => {
            document.querySelector('#report').classList.add('visible')
          })
        </script>
      </body>
    </html>
  `)

  await suitecut.narrate('This workspace keeps the current project numbers in one place.')
  const create = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(create, { borderColor: '#2DD4BF', durationMs: 900 })
  await suitecut.click(create)

  const report = page.locator('#report')
  await report.waitFor({ state: 'visible' })
  await suitecut.scrollTo(report)
  await suitecut.narrate('The finished report is ready to share.')
  await suitecut.zoom(report, { scale: 1.15, holdMs: 900 })
  await suitecut.highlight(report, {
    borderColor: '#2DD4BF',
    fillColor: '#2DD4BF',
    fillOpacity: 0.08,
    durationMs: 900,
  })
  await suitecut.checkpoint('Generated report')
})

console.log(`Manifest: ${result.manifestPath}`)
