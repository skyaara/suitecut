// SPDX-License-Identifier: MIT
// Records the home page demos by running the real-site examples in
// examples/real-sites, then writes 720p web copies and posters to site/public/demo.
// Usage: pnpm build &&
//        SUITECUT_NATIVE_EXECUTABLE=<suitecut-browser> node scripts/record-site-demos.mjs [--only <name>]
import { execFile } from 'node:child_process'
import { mkdir, mkdtemp, rm, stat } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { parseArgs } from 'node:util'

const root = resolve(import.meta.dirname, '..')
const demos = join(root, 'site', 'public', 'demo')
const names = ['ebay', 'song-maker']

const { values } = parseArgs({
  options: { only: { type: 'string', multiple: true, default: [] } },
})
if (!process.env.SUITECUT_NATIVE_EXECUTABLE) {
  process.stderr.write('Set SUITECUT_NATIVE_EXECUTABLE to the built engine\n')
  process.exit(2)
}

/**
 * Runs a command and fails with its error output.
 * @param {string} command
 * @param {string[]} args
 * @param {NodeJS.ProcessEnv} [env]
 * @returns {Promise<void>}
 */
function run(command, args, env = process.env) {
  return new Promise((done, fail) => {
    execFile(
      command,
      args,
      { cwd: root, env, maxBuffer: 64 * 1024 * 1024 },
      (error, _stdout, stderr) => {
        if (error) fail(new Error(`${command} ${args.join(' ')} failed:\n${stderr.slice(-4000)}`))
        else done()
      },
    )
  })
}

await mkdir(demos, { recursive: true })
for (const name of values.only.length ? values.only : names) {
  const work = await mkdtemp(join(tmpdir(), `suitecut-demo-${name}-`))
  const started = Date.now()
  await run(process.execPath, ['--experimental-strip-types', `examples/real-sites/${name}.ts`], {
    ...process.env,
    SUITECUT_EXAMPLE_OUTPUT: work,
  })
  const video = join(demos, `${name}.mp4`)
  await run('ffmpeg', [
    '-v',
    'error',
    '-y',
    '-i',
    join(work, `${name}.mp4`),
    '-vf',
    'scale=1280:-2',
    '-c:v',
    'libx264',
    '-preset',
    'slow',
    '-crf',
    '25',
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
  await run('ffmpeg', [
    '-v',
    'error',
    '-y',
    '-ss',
    '1.5',
    '-i',
    video,
    '-frames:v',
    '1',
    '-q:v',
    '3',
    join(demos, `${name}.jpg`),
  ])
  const { size } = await stat(video)
  process.stdout.write(
    `${name}: ${(size / 1024 / 1024).toFixed(1)} MiB in ${Math.round((Date.now() - started) / 1000)} s\n`,
  )
  await rm(work, { recursive: true, force: true })
}
