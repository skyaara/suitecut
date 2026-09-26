import { defineSuiteCut } from 'suitecut/playwright'
import { renderSuiteCut } from 'suitecut/render'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  capture: {
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 2,
    size: { width: 3840, height: 2160 },
    framesPerSecond: 60,
    quality: 100,
  },
  output: {
    directory: '.suitecut/cursor-zoom-demo/capture',
    manifestPath: '.suitecut/cursor-zoom-demo/manifest.json',
    pathKind: 'manifest-relative',
  },
})

const result = await record('macOS cursors and action zoom', async ({ page, suitecut }) => {
  await page.setContent(`
    <style>
      * { box-sizing: border-box; }
      body { margin: 0; background: #f4f3f0; color: #232323; font: 24px/1.5 system-ui; }
      .shell { width: 1480px; margin: 66px auto; }
      header { display: flex; align-items: center; justify-content: space-between; }
      .brand { font-size: 27px; font-weight: 750; letter-spacing: -1px; }
      .brand b { color: #7255db; }
      .tag { font-size: 18px; color: #78756f; }
      .intro { margin: 65px 0 36px; }
      .eyebrow { color: #7255db; font-size: 17px; letter-spacing: 2px; font-weight: 650; }
      h1 { font-size: 48px; letter-spacing: -2px; margin: 12px 0 8px; line-height: 1.2; }
      #description { color: #77746d; margin: 0; font-size: 23px; }
      .card { display: grid; grid-template-columns: 1fr 1fr; background: white; border: 1px solid #e4e1db; border-radius: 24px; overflow: hidden; }
      .form { padding: 50px 55px; }
      h2 { font-size: 28px; margin: 0 0 30px; letter-spacing: -.7px; }
      label { display: block; font-size: 19px; color: #716d66; margin-bottom: 10px; }
      input { display: block; width: 100%; padding: 16px 20px; font: inherit; border: 1px solid #d9d5cf; border-radius: 10px; outline-color: #7255db; }
      .buttons { display: flex; gap: 15px; margin-top: 28px; }
      button { font: 600 21px system-ui; padding: 17px 23px; border: 0; border-radius: 10px; cursor: pointer; }
      #publish { background: #7255db; color: white; }
      #publish:hover { background: #6042ca; }
      button:disabled { color: #b1ada6; background: #f1efeb; cursor: default; }
      .preview { background: #f9f8f5; border-left: 1px solid #ebe8e2; padding: 50px 55px; }
      .status { display: inline-block; font-size: 16px; border: 1px solid #dfdcd5; border-radius: 99px; padding: 5px 13px; color: #77746d; }
      .status.published { color: #207c5b; background: #e6f4eb; border-color: #c6e4d3; }
      .preview h3 { font-size: 30px; margin: 25px 0 6px; }
      .preview p { color: #888278; font-size: 20px; margin: 0; }
      .lines { margin-top: 25px; display: grid; gap: 13px; }
      .lines i { display: block; height: 10px; background: #e8e4dc; border-radius: 6px; }
      footer { display: flex; align-items: center; justify-content: space-between; margin-top: 35px; }
      code { padding: 14px 20px; border: 1px solid #e4e1db; border-radius: 10px; font: 18px ui-monospace, monospace; background: #eeece7; }
      #step { font-size: 18px; color: #8a857c; }
    </style>
    <div class="shell">
      <header><div class="brand">suite<b>cut</b></div><div class="tag">Cursor & camera demo</div></header>
      <section class="intro"><div class="eyebrow">SMALL DETAILS. CLEARER DEMOS.</div><h1 id="title">A cursor that follows the interaction.</h1><p id="description">The macOS arrow changes shape as it moves between controls.</p></section>
      <section class="card">
        <div class="form"><h2>Publish a project</h2><label for="name">Project name</label><input id="name" value="Untitled project"><div class="buttons"><button id="publish">Publish project</button><button disabled>Schedule</button></div></div>
        <div class="preview"><span class="status">DRAFT</span><h3 id="project">Untitled project</h3><p id="message">Your project is ready for a final review.</p><div class="lines"><i></i><i style="width:85%"></i><i style="width:65%"></i></div></div>
      </section>
      <footer><code id="code">await suitecut.hover(button)</code><span id="step">01 / 04</span></footer>
    </div>
    <script>
      document.querySelector('input').addEventListener('input', e => document.querySelector('#project').textContent = e.target.value)
      document.querySelector('#publish').addEventListener('click', () => {
        document.querySelector('.status').textContent = 'PUBLISHED';
        document.querySelector('.status').classList.add('published');
        document.querySelector('#message').textContent = 'Your project is live. Ready to share.';
      })
    </script>
  `)
  const chapter = async (title: string, description: string, code: string, step: string) => {
    await page.evaluate(
      ({ title, description, code, step }) => {
        for (const [selector, text] of Object.entries({
          '#title': title,
          '#description': description,
          '#code': code,
          '#step': step,
        })) {
          const element = document.querySelector(selector)
          if (element === null) throw new Error(`Missing demo element: ${selector}`)
          element.textContent = text
        }
      },
      { title, description, code, step },
    )
  }
  await suitecut.hold(1200)
  await suitecut.hover(page.locator('h2'), { settleMs: 800 })
  await suitecut.hover(page.locator('#publish'), { moveDurationMs: 800, settleMs: 1300 })
  await suitecut.hover(page.getByRole('button', { name: 'Schedule' }), { settleMs: 1000 })
  await chapter(
    'Text fields get a text cursor.',
    'The same small cursor adapts to editing, too.',
    "await suitecut.type(field, 'Launch notes')",
    '02 / 04',
  )
  await suitecut.hover(page.locator('#name'), { moveDurationMs: 700, settleMs: 1100 })
  await suitecut.type(page.locator('#name'), 'Launch notes', { delayMs: 110, settleMs: 900 })
  await chapter(
    'Bring the important detail closer.',
    'A highlight can zoom in, hold on the target, then ease back out.',
    'await suitecut.highlight(field, { zoom: true })',
    '03 / 04',
  )
  await suitecut.hold(1000)
  await suitecut.highlight(page.locator('#name'), {
    durationMs: 2200,
    paddingPx: 10,
    borderWidthPx: 3,
    zoom: { scale: 1.25, enter: { durationMs: 650 }, exit: { durationMs: 650 } },
  })
  await chapter(
    'Zoom in. Click. Show the result.',
    'The camera stays close while the real interaction happens.',
    'await suitecut.click(button, { zoom: true })',
    '04 / 04',
  )
  await suitecut.hold(900)
  await suitecut.click(page.locator('#publish'), {
    moveDurationMs: 1000,
    settleMs: 1400,
    zoom: { scale: 1.25, enter: { durationMs: 650 }, exit: { durationMs: 650 } },
  })
  await suitecut.hold(1800)
})

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/cursor-zoom-demo/demo.mp4',
  selection: { testId: result.testId },
  config: {
    output: { container: 'mp4', quality: 'high', width: 1920, height: 1080, framesPerSecond: 60 },
  },
})
console.log('Demo: .suitecut/cursor-zoom-demo/demo.mp4')
