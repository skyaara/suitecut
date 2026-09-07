import { expect, test } from '../../dist/test.js'

test.use({
  suitecutCapture: {
    viewport: { width: 1920, height: 1080 },
    size: { width: 3840, height: 2160 },
    framesPerSecond: 60,
    quality: 95,
  },
})

const markup = String.raw`<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>SuiteCut zoom motion test</title>
    <style>
      * { box-sizing: border-box; }
      html, body { width: 1920px; height: 1080px; margin: 0; }
      body {
        overflow: hidden;
        color: #f8fafc;
        background:
          linear-gradient(rgba(148, 163, 184, 0.16) 1px, transparent 1px),
          linear-gradient(90deg, rgba(148, 163, 184, 0.16) 1px, transparent 1px),
          #07111f;
        background-size: 48px 48px;
        font: 600 18px/1.25 ui-monospace, SFMono-Regular, Menlo, monospace;
      }
      main { position: relative; width: 1920px; height: 1080px; }
      .readout {
        position: absolute;
        top: 36px;
        left: 50%;
        width: 440px;
        transform: translateX(-50%);
        text-align: center;
      }
      #phase { color: #7dd3fc; font-size: 24px; letter-spacing: 0.08em; }
      #clock { margin-top: 10px; font-size: 42px; font-variant-numeric: tabular-nums; }
      .target {
        position: absolute;
        display: grid;
        place-items: center;
        width: 360px;
        height: 200px;
        border: 4px solid #38bdf8;
        border-radius: 24px;
        background: rgba(14, 116, 144, 0.28);
        box-shadow: 0 0 0 2px #07111f, 0 0 48px rgba(56, 189, 248, 0.2);
        font-size: 28px;
        text-align: center;
      }
      #center { left: 50%; top: 52%; transform: translate(-50%, -50%); }
      #top-left { left: 48px; top: 48px; border-color: #a78bfa; background: rgba(109, 40, 217, 0.25); }
      #bottom-right { right: 48px; bottom: 48px; border-color: #4ade80; background: rgba(21, 128, 61, 0.25); }
      .crosshair::before, .crosshair::after {
        content: '';
        position: absolute;
        pointer-events: none;
        background: rgba(248, 250, 252, 0.62);
      }
      .crosshair::before { left: 50%; top: 0; width: 1px; height: 100%; }
      .crosshair::after { left: 0; top: 50%; width: 100%; height: 1px; }
      #sweep {
        position: absolute;
        left: 0;
        bottom: 18px;
        width: 96px;
        height: 10px;
        border-radius: 999px;
        background: #f8fafc;
        box-shadow: 0 0 18px #7dd3fc;
        animation: sweep 1.4s linear infinite alternate;
      }
      @keyframes sweep { from { transform: translateX(0); } to { transform: translateX(1824px); } }
    </style>
  </head>
  <body>
    <main class="crosshair">
      <div class="readout">
        <div id="phase">SOURCE MOTION ACTIVE</div>
        <div id="clock">000000</div>
      </div>
      <div class="target" id="top-left">EDGE TARGET<br />TOP LEFT</div>
      <div class="target" id="center">CENTER TARGET<br />LINEAR ZOOM</div>
      <div class="target" id="bottom-right">EDGE TARGET<br />BOTTOM RIGHT</div>
      <div id="sweep"></div>
    </main>
    <script>
      const startedAt = performance.now();
      const clock = document.querySelector('#clock');
      const tick = (now) => {
        clock.textContent = String(Math.round(now - startedAt)).padStart(6, '0');
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    </script>
  </body>
</html>`

test('renders zoom-only camera motion without frozen transform frames', async ({
  page,
  suitecut,
}) => {
  test.setTimeout(180_000)

  await page.goto(`data:text/html;charset=utf-8,${encodeURIComponent(markup)}`, {
    waitUntil: 'domcontentloaded',
  })
  suitecut.selectPage(page)
  await expect(page.locator('#clock')).not.toHaveText('000000')
  await suitecut.hold(500)

  await page.locator('#phase').evaluate((element) => {
    element.textContent = 'TEST 1 / CENTER / LINEAR'
  })
  await suitecut.zoom(page.locator('#center'), {
    scale: 1.25,
    paddingPx: 64,
    enter: { type: 'scale', durationMs: 800, easing: 'linear' },
    holdMs: 450,
    exit: { type: 'scale', durationMs: 800, easing: 'linear' },
  })
  await suitecut.hold(350)

  await page.locator('#phase').evaluate((element) => {
    element.textContent = 'TEST 2 / TOP LEFT / EASED'
  })
  await suitecut.zoom(page.locator('#top-left'), {
    scale: 1.24,
    paddingPx: 48,
    enter: { type: 'scale', durationMs: 700, easing: 'ease-out' },
    holdMs: 450,
    exit: { type: 'scale', durationMs: 700, easing: 'ease-in-out' },
  })
  await suitecut.hold(350)

  await page.locator('#phase').evaluate((element) => {
    element.textContent = 'TEST 3 / BOTTOM RIGHT / FAST'
  })
  await suitecut.zoom(page.locator('#bottom-right'), {
    scale: 1.24,
    paddingPx: 48,
    enter: { type: 'scale', durationMs: 320, easing: 'ease-out' },
    holdMs: 400,
    exit: { type: 'scale', durationMs: 320, easing: 'ease-in-out' },
  })
  await suitecut.hold(650)
})
