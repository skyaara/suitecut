// SPDX-License-Identifier: MIT
import { resolve } from 'node:path'
import process from 'node:process'

import { launchBrowser } from './native-browser-controller.js'
import { createBrowserViewer } from './native-viewer.js'

export const NATIVE_VIEWER_USAGE = `Usage: suitecut view [http(s)://url] [options]
  --executable <path>  Native browser binary (default .suitecut/native-build/suitecut-browser).
  --port <0..65535>    Local viewer port; default picks a free port.
  --width <pixels>    Browser viewport width; default 1280.
  --height <pixels>   Browser viewport height; default 720.
  --resolution <WxH|auto>  Initial viewport or automatic fit during human control.
  --fps <1..120>      Viewer maximum frame rate; default 30.
  --quality <20..95>  JPEG quality; default 75.
  --proxy <url>       Browser proxy (defaults to HTTPS_PROXY / HTTP_PROXY).
Open the printed private link in your browser. Ctrl+C closes the viewer and helper.
`

export async function runNativeViewerCli(args: readonly string[]): Promise<number> {
  if (args.includes('--help') || args.includes('-h')) {
    process.stdout.write(NATIVE_VIEWER_USAGE)
    return 0
  }
  let executablePath =
    process.env.SUITECUT_NATIVE_EXECUTABLE ?? resolve('.suitecut/native-build/suitecut-browser')
  let url: string | undefined
  let proxy = process.env.HTTPS_PROXY ?? process.env.HTTP_PROXY
  let resolutionMode: 'fixed' | 'auto' = 'fixed'
  const values = { port: 0, width: 1280, height: 720, fps: 30, quality: 75 }
  for (let index = 0; index < args.length; ++index) {
    const name = args[index]
    if (!name) throw new Error('Invalid viewer argument')
    if (!name.startsWith('--')) {
      if (url) throw new Error('Specify one browser URL')
      url = new URL(name).href
      if (!['http:', 'https:'].includes(new URL(url).protocol))
        throw new Error('Viewer URL requires HTTP(S)')
      continue
    }
    const value = args[++index]
    if (!value || value.startsWith('--')) throw new Error(`${name} requires a value`)
    if (name === '--executable') executablePath = resolve(value)
    else if (name === '--proxy') proxy = value
    else if (name === '--resolution') {
      if (value === 'auto') resolutionMode = 'auto'
      else {
        const parsed = /^(\d+)x(\d+)$/iu.exec(value)
        const width = Number(parsed?.[1]),
          height = Number(parsed?.[2])
        if (
          !parsed ||
          !Number.isInteger(width) ||
          width < 2 ||
          width > 3840 ||
          !Number.isInteger(height) ||
          height < 2 ||
          height > 2160
        )
          throw new Error('--resolution requires auto or WxH within 3840x2160')
        values.width = width
        values.height = height
        resolutionMode = 'fixed'
      }
    } else {
      const key = name.slice(2)
      if (!(key in values)) throw new Error(`Unknown viewer option: ${name}`)
      const integer = Number(value)
      const bounds: Record<string, [number, number]> = {
        port: [0, 65535],
        width: [2, 3840],
        height: [2, 2160],
        fps: [1, 120],
        quality: [20, 95],
      }
      const range = bounds[key]
      if (!range || !Number.isInteger(integer) || integer < range[0] || integer > range[1])
        throw new Error(`Invalid value for ${name}`)
      values[key as keyof typeof values] = integer
    }
  }
  const browser = await launchBrowser({
    executablePath: resolve(executablePath),
    headless: true,
    width: values.width,
    height: values.height,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
  })
  let viewer: Awaited<ReturnType<typeof createBrowserViewer>> | undefined
  const shutdown = (): void => {
    void browser.close().catch(() => undefined)
  }
  process.once('SIGINT', shutdown)
  process.once('SIGTERM', shutdown)
  try {
    const bypass = process.env.NO_PROXY ?? process.env.no_proxy ?? 'localhost,127.0.0.1,[::1]'
    const page = await browser.newPage(proxy ? { proxy: { server: proxy, bypass } } : {})
    await page.goto(url ?? 'about:blank', { timeout: 30000, waitUntil: 'domcontentloaded' })
    viewer = await createBrowserViewer({
      browser,
      page,
      port: values.port,
      framesPerSecond: values.fps,
      quality: values.quality,
      resolutionMode,
    })
    process.stdout.write(
      `SuiteCut browser viewer: ${viewer.url}\nKeep this link private. Use Take control to interact.\n`,
    )
    await Promise.race([browser.closed, viewer.closed])
    return 0
  } finally {
    process.removeListener('SIGINT', shutdown)
    process.removeListener('SIGTERM', shutdown)
    try {
      await viewer?.close()
    } finally {
      await browser.close()
    }
  }
}
