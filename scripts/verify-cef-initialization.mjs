// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { isAbsolute, join, resolve } from 'node:path'
import process from 'node:process'
import { promisify } from 'node:util'

import * as z from 'zod'

const directory = resolve('.suitecut/cef-initialization')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const [normal, instrumented] = process.argv.slice(2)
assert(
  normal && instrumented && isAbsolute(normal) && isAbsolute(instrumented),
  'Pass absolute paths to the normal and sanitizer suitecut-cef-control binaries',
)
const checks = []
const run = promisify(execFile)
const binaries = z
  .array(z.object({ kind: z.enum(['normal', 'instrumented']), executable: z.string() }))
  .parse([
    { kind: 'normal', executable: normal },
    { kind: 'instrumented', executable: instrumented },
  ])
for (const { kind, executable } of binaries) {
  const profile = await mkdtemp(join(tmpdir(), 'suitecut-cef-control-'))
  const args = ['--ozone-platform=headless', '--disable-gpu', `--control-profile=${profile}`]
  const env = { ...process.env }
  if (kind === 'instrumented') {
    env.ASAN_OPTIONS = 'detect_leaks=1:halt_on_error=1'
    env.UBSAN_OPTIONS = 'halt_on_error=1'
  } else {
    delete env.ASAN_OPTIONS
    delete env.UBSAN_OPTIONS
  }
  try {
    const sha256 = createHash('sha256')
      .update(await readFile(executable))
      .digest('hex')
    const result = { exitCode: 0, signal: '', killed: false, stdout: '', stderr: '' }
    try {
      const output = await run(executable, args, {
        env,
        timeout: 60000,
        killSignal: 'SIGKILL',
        encoding: 'utf8',
        maxBuffer: 4 * 1024 * 1024,
      })
      result.stdout = output.stdout
      result.stderr = output.stderr
    } catch (error) {
      const failure = z
        .object({
          code: z.union([z.number(), z.string()]).nullable().optional(),
          signal: z.string().nullable().optional(),
          killed: z.boolean().optional(),
          stdout: z.string().optional(),
          stderr: z.string().optional(),
        })
        .parse(error)
      result.exitCode = typeof failure.code === 'number' ? failure.code : 1
      result.signal = failure.signal ?? ''
      result.killed = failure.killed ?? false
      result.stdout = failure.stdout ?? ''
      result.stderr = failure.stderr ?? ''
    }
    checks.push({
      kind,
      executable,
      sha256,
      args,
      asanOptions: env.ASAN_OPTIONS,
      ubsanOptions: env.UBSAN_OPTIONS,
      ...result,
    })
  } finally {
    await rm(profile, { recursive: true, force: true })
  }
}
const passed = checks.every((check) => check.exitCode === 0 && !check.killed)
const report = {
  platform: process.platform,
  completedAt: new Date().toISOString(),
  passed,
  checks,
  scope: 'CEF initialization and shutdown without SuiteCut automation or media',
  sandbox: 'enabled; no sanitizer suppressions',
}
await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
console.log(JSON.stringify(report, null, 2))
process.exitCode = passed ? 0 : 1
