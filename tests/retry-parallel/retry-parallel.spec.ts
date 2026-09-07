import { mkdir, readdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'

import { expect, test } from '../../src/test.js'

const cases = [
  { name: 'ruby', color: '#dc2626' },
  { name: 'azure', color: '#2563eb' },
] as const

test.use({
  suitecutCapture: {
    viewport: { width: 320, height: 180 },
    framesPerSecond: 30,
    quality: 90,
  },
})

test.describe.configure({ mode: 'parallel' })

for (const testCase of cases) {
  test(`${testCase.name} attempt stays isolated`, async ({ page, suitecut }, testInfo) => {
    const bodyStartedAtEpochMs = Date.now()
    const marker = {
      caseName: testCase.name,
      color: testCase.color,
      retry: testInfo.retry,
      workerIndex: testInfo.workerIndex,
      parallelIndex: testInfo.parallelIndex,
    }
    const sentinel = [
      marker.caseName,
      `retry=${String(marker.retry)}`,
      `worker=${String(marker.workerIndex)}`,
      `parallel=${String(marker.parallelIndex)}`,
    ].join(':')

    await page.setContent(`
      <!doctype html>
      <html>
        <head>
          <style>
            html, body { width: 100%; height: 100%; margin: 0; background: ${testCase.color}; }
            body { color: white; font: 14px/1.2 system-ui; }
            output { position: fixed; inset: 8px auto auto 8px; }
          </style>
        </head>
        <body><output>${sentinel}</output></body>
      </html>
    `)

    await suitecut.checkpoint(sentinel)

    const barrierDirectory = resolve(
      import.meta.dirname,
      '../../.suitecut/retry-parallel-barrier',
      String(testInfo.retry),
    )
    await mkdir(barrierDirectory, { recursive: true })
    await writeFile(join(barrierDirectory, `${testCase.name}.ready`), sentinel, 'utf8')
    await expect
      .poll(
        async () =>
          (await readdir(barrierDirectory)).filter((name) => name.endsWith('.ready')).length,
        {
          message: `both retry ${String(testInfo.retry)} attempts must enter the worker barrier`,
          timeout: 10_000,
        },
      )
      .toBe(cases.length)
    const barrierReleasedAtEpochMs = Date.now()

    await suitecut.hold(400)
    const bodyEndedAtEpochMs = Date.now()

    const markerName = `attempt-marker-${testCase.name}-retry-${String(testInfo.retry)}.json`
    const markerPath = testInfo.outputPath(markerName)
    await writeFile(
      markerPath,
      `${JSON.stringify(
        {
          ...marker,
          sentinel,
          bodyStartedAtEpochMs,
          barrierReleasedAtEpochMs,
          bodyEndedAtEpochMs,
        },
        null,
        2,
      )}\n`,
      'utf8',
    )
    await testInfo.attach(markerName, { path: markerPath, contentType: 'application/json' })

    expect(testInfo.retry, 'the first attempt must fail so Playwright performs a real retry').toBe(
      1,
    )
  })
}
