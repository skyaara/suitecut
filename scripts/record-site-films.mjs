// SPDX-License-Identifier: MIT
// Records the package explainer films on the site with suitecut itself: it serves the
// built site, opens each /film/<package> page in the native engine, shows one scene per
// narrated line, renders the run and writes site/public/films/<package>.{mp4,jpg}.
// Usage: pnpm build && pnpm site:build &&
//        node scripts/record-site-films.mjs --executable <suitecut-browser> [--only <package>]
import { execFile } from 'node:child_process'
import { createReadStream } from 'node:fs'
import { mkdir, mkdtemp, rm, stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { extname, join, normalize, resolve } from 'node:path'
import process from 'node:process'
import { parseArgs } from 'node:util'

import * as z from 'zod'

import { record, renderSuiteCut } from '../dist/index.js'

const root = resolve(import.meta.dirname, '..')
const site = join(root, 'site', 'dist')
const films = join(root, 'site', 'public', 'films')
const slugs = ['suitecut', 'browser-control', 'shared-browser', 'native-sdk']

const { values } = parseArgs({
  options: {
    executable: { type: 'string', default: process.env.SUITECUT_NATIVE_EXECUTABLE ?? '' },
    only: { type: 'string', multiple: true, default: [] },
    keep: { type: 'boolean', default: false },
  },
})
if (!values.executable) {
  process.stderr.write('Pass --executable or set SUITECUT_NATIVE_EXECUTABLE\n')
  process.exit(2)
}

const types = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.svg': 'image/svg+xml',
  '.woff2': 'font/woff2',
  '.json': 'application/json',
}

/**
 * Serves the built site on a free local port.
 * @returns {Promise<{ origin: string, close: () => void }>}
 */
async function serve() {
  const server = createServer((request, response) => {
    const path = normalize(decodeURIComponent(new URL(request.url ?? '/', 'http://x').pathname))
    const candidates = [
      join(site, path),
      join(site, path, 'index.html'),
      `${join(site, path)}.html`,
    ]
    const next = async () => {
      for (const file of candidates) {
        if (!file.startsWith(site)) break
        const info = await stat(file).catch(() => undefined)
        if (!info?.isFile()) continue
        response.writeHead(200, {
          'content-type':
            types[/** @type {keyof typeof types} */ (extname(file))] ?? 'application/octet-stream',
        })
        createReadStream(file).pipe(response)
        return
      }
      response.writeHead(404).end()
    }
    void next()
  })
  await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
  const address = z.object({ port: z.number() }).parse(server.address())
  return { origin: `http://127.0.0.1:${address.port}`, close: () => server.close() }
}

/**
 * Runs ffmpeg and fails with its error output.
 * @param {string[]} args
 * @returns {Promise<void>}
 */
function ffmpeg(args) {
  return new Promise((done, fail) => {
    execFile('ffmpeg', ['-v', 'error', '-y', ...args], (error, _stdout, stderr) => {
      if (error) fail(new Error(`ffmpeg failed: ${stderr}`))
      else done()
    })
  })
}

const Cues = z.array(
  z.object({ narration: z.string(), focus: z.string().optional(), kind: z.string() }),
)

const server = await serve()
await mkdir(films, { recursive: true })
try {
  for (const slug of values.only.length ? values.only : slugs) {
    const work = await mkdtemp(join(tmpdir(), `suitecut-film-${slug}-`))
    const started = Date.now()
    const run = await record(
      `film-${slug}`,
      async ({ page, suitecut }) => {
        await page.goto(`${server.origin}/film/${slug}`)
        await page.waitForFunction(
          "document.documentElement.hasAttribute('data-ready') && document.querySelector('canvas[data-ready]') !== null",
        )
        const cues = Cues.parse(await page.evaluate('window.film.cues'))
        for (const [index, cue] of cues.entries()) {
          await page.evaluate(`window.film.show(${index})`)
          // Let the scene arrive and a diagram finish drawing before the narrator starts.
          await suitecut.hold(cue.kind === 'doodle' ? 1900 : cue.kind === 'title' ? 1300 : 900)
          await suitecut.narrate(cue.narration)
          // The film about suitecut shows its own effects on the line that calls them.
          if (slug === 'suitecut' && cue.kind === 'code') {
            const code = page.locator(`[data-scene="${index}"] .code`)
            await suitecut.highlight(code, { label: 'suitecut.highlight()', durationMs: 1600 })
            await suitecut.zoom(code, { scale: 1.2, holdMs: 1200 })
          }
          await suitecut.hold(cue.kind === 'end' ? 1800 : 500)
        }
      },
      {
        native: {
          executablePath: values.executable,
          width: 1920,
          height: 1080,
          framesPerSecond: 60,
        },
        capture: { framesPerSecond: 30 },
        output: { directory: work },
      },
    )
    const raw = join(work, `${slug}.mp4`)
    await renderSuiteCut({
      manifestPath: run.manifestPath,
      outputPath: raw,
      config: { output: { framesPerSecond: 30 } },
    })
    // Web copies: H.264 that starts playing before it has fully loaded, and a poster.
    const video = join(films, `${slug}.mp4`)
    await ffmpeg([
      '-i',
      raw,
      '-c:v',
      'libx264',
      '-preset',
      'slow',
      '-crf',
      '24',
      '-pix_fmt',
      'yuv420p',
      '-c:a',
      'aac',
      '-b:a',
      '128k',
      '-movflags',
      '+faststart',
      video,
    ])
    await ffmpeg([
      '-ss',
      '2.6',
      '-i',
      video,
      '-frames:v',
      '1',
      '-vf',
      'scale=1280:-1',
      '-q:v',
      '3',
      join(films, `${slug}.jpg`),
    ])
    const { size } = await stat(video)
    process.stdout.write(
      `${slug}: ${(size / 1024 / 1024).toFixed(1)} MiB in ${Math.round((Date.now() - started) / 1000)} s\n`,
    )
    if (!values.keep) await rm(work, { recursive: true, force: true })
  }
} finally {
  server.close()
}
