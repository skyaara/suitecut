// SPDX-License-Identifier: MIT
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { promisify } from 'node:util'

import * as z from 'zod'

const iterations = z.coerce
  .number()
  .int()
  .min(1)
  .max(100000000)
  .parse(process.argv[2] ?? 1000000)
const directory = resolve(process.env.SUITECUT_NATIVE_FUZZ_OUTPUT ?? '.suitecut/native-fuzz')
await mkdir(directory, { recursive: true })
const executable = resolve(directory, 'control-fuzz')
const run = promisify(execFile)
const sources = [
  'native/browser/control_test.cc',
  'native/browser/command_framer.h',
  'native/browser/operation_registry.h',
  'native/browser/command_credits.h',
]
const sourceHashes = []
for (const path of sources)
  sourceHashes.push({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })
const flags = [
  '-std=c++20',
  '-O1',
  '-g',
  '-fsanitize=address,undefined',
  '-fno-omit-frame-pointer',
  '-pthread',
  sources[0],
  '-o',
  executable,
]
await run('g++', flags)
const startedAt = new Date().toISOString()
let exitCode = 0
let output
try {
  const result = await run(executable, [String(iterations)], {
    env: {
      ...process.env,
      ASAN_OPTIONS: 'detect_leaks=1:halt_on_error=1',
      UBSAN_OPTIONS: 'halt_on_error=1',
    },
    maxBuffer: 1024 * 1024,
  })
  output = result.stdout + result.stderr
} catch (error) {
  const failure = z
    .object({
      code: z.union([z.number(), z.string()]).optional(),
      stdout: z.string().optional(),
      stderr: z.string().optional(),
    })
    .parse(error)
  exitCode = typeof failure.code === 'number' ? failure.code : 1
  output = (failure.stdout ?? '') + (failure.stderr ?? '')
}
await writeFile(`${directory}/output.log`, output)
const compiler = await run('g++', ['--version'])
const report = {
  scope: 'CEF-independent command framing, operation registry, and concurrent reply credits',
  iterations,
  seed: '0x5c17c0de',
  startedAt,
  completedAt: new Date().toISOString(),
  exitCode,
  compiler: compiler.stdout.split('\n')[0],
  flags,
  sourceHashes,
  addressSanitizer: true,
  undefinedBehaviorSanitizer: true,
  leakDetection: true,
  note: 'Deterministic boundary/property fuzzing. Does not instrument the prebuilt CEF runtime or establish whole-engine leak freedom.',
}
await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
console.log(output)
console.log(JSON.stringify(report, null, 2))
process.exitCode = exitCode
