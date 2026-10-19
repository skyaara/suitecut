/* eslint-disable @typescript-eslint/no-unsafe-assignment -- Controlled fuzz/process-model harness selects emitted modules and deliberately malformed protocol values; production SDK validation is tested separately. */
import { execFile } from 'node:child_process'
import console from 'node:console'
// SPDX-License-Identifier: MIT
// Repeatable deterministic native sanitizer campaign. CEF is deliberately not
// linked: this instruments the owned protocol/lifecycle data structures only.
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { promisify } from 'node:util'

const run = promisify(execFile)
const count = Number(process.argv[2] ?? 1_000_000)
if (!Number.isSafeInteger(count) || count < 1 || count > 100_000_000)
  throw new Error('Expected iteration count from 1 to 100000000')
const directory = resolve(process.argv[3] ?? '.suitecut/native-hardening')
await mkdir(directory, { recursive: true })
const paths = [
  'control_test.cc',
  'control_schedule_fuzz_test.cc',
  'command_framer.h',
  'command_credits.h',
  'operation_registry.h',
  'action_pauses.h',
].map((name) => `native/browser/${name}`)
const sourceHashes = await Promise.all(
  paths.map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
const report = {
  iterationsPerFamily: count,
  startedAt: new Date().toISOString(),
  sources: sourceHashes,
  checks: [],
  scope:
    'Owned CEF-independent command framing, reply credits, operation identity/deadline and pause lifecycle; stock CEF/Chromium and whole-browser heap are not instrumented.',
}
const flags = [
  '-std=c++20',
  '-O1',
  '-g',
  '-UNDEBUG',
  '-fsanitize=address,undefined',
  '-fno-omit-frame-pointer',
  '-pthread',
]
for (const [name, source, seed] of [
  ['control', paths[0], '0x5c17c0de'],
  ['schedules', paths[1], '0x41c0ffee'],
]) {
  const executable = resolve(directory, name)
  const entry = { name, seed, flags: [...flags, source, '-o', executable] }
  report.checks.push(entry)
  try {
    const compile = await run('g++', entry.flags, { timeout: 180_000, maxBuffer: 1024 * 1024 })
    await writeFile(`${directory}/${name}-compile.log`, compile.stdout + compile.stderr)
    const started = performance.now()
    const result = await run(executable, [String(count)], {
      timeout: 600_000,
      maxBuffer: 1024 * 1024,
      env: {
        ...process.env,
        ASAN_OPTIONS: 'detect_leaks=1:halt_on_error=1',
        UBSAN_OPTIONS: 'halt_on_error=1',
      },
    })
    entry.seconds = (performance.now() - started) / 1000
    entry.exitCode = 0
    await writeFile(`${directory}/${name}.log`, result.stdout + result.stderr)
  } catch (error) {
    entry.exitCode = typeof error.code === 'number' ? error.code : 1
    entry.signal = error.signal ?? null
    await writeFile(
      `${directory}/${name}.log`,
      `${error.stdout ?? ''}${error.stderr ?? ''}${error.message}\n`,
    )
    process.exitCode = 1
  }
  report.completedAt = new Date().toISOString()
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
}
console.log(JSON.stringify(report, null, 2))
