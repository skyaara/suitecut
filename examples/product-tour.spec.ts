import { expect, test } from 'suitecut'

test('renders a polished product tour', async ({ page, suitecut }) => {
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.setContent(`
    <style>
      * { box-sizing: border-box; }
      body {
        margin: 0;
        min-height: 100vh;
        color: #e8eefc;
        background: #07111f;
        font: 15px/1.45 Inter, ui-sans-serif, system-ui, -apple-system, sans-serif;
      }
      .shell { display: grid; grid-template-columns: 230px 1fr; min-height: 100vh; }
      aside { padding: 28px 20px; border-right: 1px solid #1d2d43; background: #091523; }
      .brand { display: flex; align-items: center; gap: 10px; margin-bottom: 36px; font-size: 18px; font-weight: 750; }
      .mark { width: 30px; height: 30px; border-radius: 9px; background: linear-gradient(135deg, #7c3aed, #2dd4bf); box-shadow: 0 8px 28px #7c3aed55; }
      nav { display: grid; gap: 8px; color: #8fa4bf; }
      nav div { padding: 10px 12px; border-radius: 9px; }
      nav .active { color: #fff; background: #14243a; }
      main { padding: 34px 42px; background: radial-gradient(circle at 90% 0%, #172554 0, transparent 35%); }
      header { display: flex; align-items: center; justify-content: space-between; }
      h1 { margin: 0; font-size: 31px; letter-spacing: -0.04em; }
      .sub { margin: 7px 0 0; color: #8fa4bf; }
      button {
        border: 0; border-radius: 11px; padding: 12px 17px; color: white; cursor: pointer;
        background: linear-gradient(135deg, #7c3aed, #5b21b6); font: inherit; font-weight: 700;
        box-shadow: 0 10px 32px #7c3aed55;
      }
      .stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 18px; margin: 30px 0 20px; }
      .card { border: 1px solid #21344d; border-radius: 16px; background: #0c1929cc; box-shadow: 0 18px 55px #02061766; }
      .stat { padding: 20px; }
      .label { color: #8fa4bf; font-size: 13px; }
      .value { margin-top: 7px; font-size: 28px; font-weight: 760; letter-spacing: -0.03em; }
      .delta { color: #5eead4; font-size: 12px; }
      .workspace { display: grid; grid-template-columns: 1.4fr 0.8fr; gap: 18px; }
      .chart { min-height: 330px; padding: 22px; }
      .chart-head { display: flex; justify-content: space-between; align-items: center; }
      .bars { height: 220px; display: flex; align-items: end; gap: 12px; padding-top: 28px; }
      .bar { flex: 1; border-radius: 7px 7px 2px 2px; background: linear-gradient(#8b5cf6, #4c1d95); opacity: .85; }
      .activity { padding: 22px; }
      .row { display: flex; gap: 12px; padding: 17px 0; border-bottom: 1px solid #1d2d43; }
      .dot { width: 9px; height: 9px; margin-top: 6px; border-radius: 50%; background: #2dd4bf; box-shadow: 0 0 18px #2dd4bf; }
      #report { display: none; margin-top: 18px; padding: 18px 20px; border: 1px solid #2dd4bf66; border-radius: 14px; background: #0f2b32; }
      #report.visible { display: flex; align-items: center; justify-content: space-between; }
      #report strong { color: #99f6e4; font-size: 17px; }
      .pill { padding: 5px 9px; border-radius: 999px; color: #99f6e4; background: #134e4a; font-size: 12px; }
    </style>
    <div class="shell">
      <aside>
        <div class="brand"><div class="mark"></div>Northstar</div>
        <nav><div class="active">Overview</div><div>Projects</div><div>Reports</div><div>Automations</div></nav>
      </aside>
      <main>
        <header>
          <div><h1>Good morning, Aakash</h1><p class="sub">Your workspace is up 18.4% this month.</p></div>
          <button id="create">Create report</button>
        </header>
        <section class="stats">
          <div class="card stat"><div class="label">Active projects</div><div class="value">24</div><div class="delta">+4 this week</div></div>
          <div class="card stat"><div class="label">Review time</div><div class="value">3.2h</div><div class="delta">18% faster</div></div>
          <div class="card stat"><div class="label">Approval rate</div><div class="value">94%</div><div class="delta">Best month yet</div></div>
        </section>
        <section class="workspace">
          <div class="card chart">
            <div class="chart-head"><strong>Weekly output</strong><span class="pill">Live</span></div>
            <div class="bars">
              <div class="bar" style="height:42%"></div><div class="bar" style="height:58%"></div>
              <div class="bar" style="height:48%"></div><div class="bar" style="height:72%"></div>
              <div class="bar" style="height:66%"></div><div class="bar" style="height:84%"></div>
              <div class="bar" style="height:94%"></div>
            </div>
          </div>
          <div class="card activity"><strong>Recent activity</strong>
            <div class="row"><span class="dot"></span><div>Homepage approved<br><span class="label">2 minutes ago</span></div></div>
            <div class="row"><span class="dot"></span><div>Campaign exported<br><span class="label">18 minutes ago</span></div></div>
            <div class="row"><span class="dot"></span><div>Three comments resolved<br><span class="label">1 hour ago</span></div></div>
          </div>
        </section>
        <section id="report"><div><strong>August performance report</strong><br><span class="label">12 pages, ready to share</span></div><span class="pill">Generated</span></section>
      </main>
    </div>
    <script>
      document.querySelector('#create').addEventListener('click', () => {
        document.querySelector('#report').classList.add('visible')
      })
    </script>
  `)

  suitecut.narrate('This workspace keeps the important numbers in one place.', {
    provider: 'kokoro',
    voice: 'af_heart',
  })
  const createButton = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(createButton, {
    borderColor: '#2DD4BF',
    borderWidthPx: 4,
    durationMs: 1_100,
  })
  await createButton.hover()
  await createButton.click()
  suitecut.hold(650)

  const report = page.locator('#report')
  await expect(report).toBeVisible()
  suitecut.narrate('The finished report appears immediately and is ready to share.', {
    provider: 'kokoro',
    voice: 'af_heart',
  })
  await suitecut.zoom(report, { scale: 1.15, holdMs: 1_200 })
  await suitecut.highlight(report, {
    borderColor: '#2DD4BF',
    fillColor: '#2DD4BF',
    fillOpacity: 0.08,
    durationMs: 1_200,
  })
  await suitecut.checkpoint('Generated report')
})
