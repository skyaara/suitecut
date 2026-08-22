import { readFile } from 'node:fs/promises'

import { expect, test } from 'suitecut'

async function readPngSize(path: string): Promise<{ width: number; height: number }> {
  const png = await readFile(path)
  return {
    width: png.readUInt32BE(16),
    height: png.readUInt32BE(20),
  }
}

test('captures viewport and full-page checkpoints', async ({ page, suitecut }, testInfo) => {
  await page.setViewportSize({ width: 640, height: 360 })
  await page.setContent(`
    <style>
      html, body { margin: 0; }
      main { height: 900px; }
    </style>
    <main>Checkpoint page</main>
  `)

  await suitecut.checkpoint('Viewport checkpoint')
  await suitecut.checkpoint('Full-page checkpoint', { fullPage: true })

  const checkpoints = testInfo.attachments.filter((attachment) =>
    attachment.name.startsWith('suitecut-checkpoint-'),
  )
  expect(checkpoints).toHaveLength(2)
  expect(checkpoints.map((attachment) => attachment.contentType)).toEqual([
    'image/png',
    'image/png',
  ])

  const viewportPath = checkpoints[0]?.path
  const fullPagePath = checkpoints[1]?.path
  expect(viewportPath).toBeTruthy()
  expect(fullPagePath).toBeTruthy()
  if (viewportPath === undefined || fullPagePath === undefined) return

  await expect(readPngSize(viewportPath)).resolves.toEqual({ width: 640, height: 360 })
  await expect(readPngSize(fullPagePath)).resolves.toEqual({ width: 640, height: 900 })
})
