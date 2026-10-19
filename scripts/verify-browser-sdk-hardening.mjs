#!/usr/bin/env node
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-argument -- Controlled fuzz/process-model harness selects emitted modules and deliberately malformed protocol values; production SDK validation is tested separately. */
import { spawn } from 'node:child_process'
import console from 'node:console'
// Heavy deterministic SDK campaigns. Native component sanitizers are opt-in;
// live browser stress needs its own installed-binary/supervisor configuration.
import { closeSync, openSync } from 'node:fs'
import { access, lstat, mkdir, readFile, realpath, symlink, writeFile } from 'node:fs/promises'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const arguments_ = process.argv.slice(2)
if (arguments_.includes('--help')) {
  console.log(`Usage: node scripts/verify-browser-sdk-hardening.mjs [output-directory] [--native]
Sequential browser SDK unit/property/fuzz/memory campaigns; no repository dist wipe.
Requires Linux and the existing proof supervisor. Override its installed path with
SUITECUT_PROOF_SUPERVISOR=/absolute/suitecut-proof-supervisor.
--native additionally runs owned CEF-independent ASan/UBSan/LSan component checks.
Live browser stress is separate (set absolute paths):
  SUITECUT_STRESS_SDK=/output/sdk-dist/browser.js
  SUITECUT_NATIVE_EXECUTABLE=/installed/suitecut-browser
  SUITECUT_STRESS_OUTPUT=/output/live
  $SUITECUT_PROOF_SUPERVISOR --report /output/live-supervisor.json
    --timeout-ms 900000 --cleanup-grace-ms 1000 --physical-ms 6000 --
    node --max-old-space-size=512 --expose-gc scripts/sdk-browser-stress.mjs
Export those three environment variables before running the supervisor command.
Custom CEF and whole-browser heaps are not instrumented.`)
  process.exit(0)
}
const positional = arguments_.filter((argument) => !argument.startsWith('--'))
if (
  positional.length > 1 ||
  arguments_.some((argument) => argument.startsWith('--') && argument !== '--native')
)
  throw new Error('Expected [output-directory] [--native]; see --help')
if (process.platform !== 'linux')
  throw new Error('The descendant-reaping proof supervisor requires Linux')
let output = resolve(positional[0] ?? join(root, '.suitecut/sdk-hardening'))
function protectSource(directory) {
  const path = relative(root, directory)
  if (
    !path ||
    ['src', 'dist', 'tests', 'native', 'scripts', 'node_modules', 'plugins', '.git'].some(
      (name) => path === name || path.startsWith(name + sep),
    )
  )
    throw new Error('Hardening output must be outside repository source/build/package directories')
}
protectSource(output)
await mkdir(output, { recursive: true })
output = await realpath(output)
protectSource(output)
const sdk = join(output, 'sdk-dist')
try {
  if ((await lstat(sdk)).isSymbolicLink()) throw new Error('Refusing a symlinked sdk-dist output')
} catch (error) {
  if (error.code !== 'ENOENT') throw error
}
await mkdir(sdk, { recursive: true })
const dependencies = await realpath(join(root, 'node_modules'))
try {
  await symlink(dependencies, join(output, 'node_modules'), 'dir')
} catch (error) {
  if (error.code !== 'EEXIST' || (await realpath(join(output, 'node_modules'))) !== dependencies)
    throw error
}
const packageFile = join(output, 'package.json')
try {
  if (JSON.parse(await readFile(packageFile, 'utf8')).type !== 'module')
    throw new Error('Output package.json must have type: module')
} catch (error) {
  if (error.code !== 'ENOENT') throw error
  await writeFile(packageFile, '{"private":true,"type":"module"}\n', { flag: 'wx' })
}
const supervisor = resolve(
  process.env.SUITECUT_PROOF_SUPERVISOR ??
    join(root, '.suitecut/native-build/native-runner/proof-tools/suitecut-proof-supervisor'),
)
await access(supervisor)
const config = join(output, 'tsconfig.browser.json')
await writeFile(
  config,
  JSON.stringify(
    {
      extends: join(root, 'tsconfig.json'),
      compilerOptions: { rootDir: join(root, 'src'), outDir: sdk },
      include: [join(root, 'src/browser.ts')],
      exclude: [],
    },
    null,
    2,
  ) + '\n',
)
const report = {
  startedAt: new Date().toISOString(),
  status: 'running',
  node: process.version,
  root,
  output,
  supervisor,
  stages: [],
  scope:
    'Browser control SDK checks, seeded property campaigns and JS/Buffer retention; optional owned native components. No automatic live CEF stress or whole-browser sanitizer claim.',
}
const reportFile = join(output, 'report.json')
const save = () => writeFile(reportFile, JSON.stringify(report, null, 2) + '\n')
let active
let interrupted
const signals = ['SIGINT', 'SIGTERM', 'SIGHUP']
const handlers = signals.map((signal) => {
  const handler = () => {
    interrupted ??= signal
    active?.kill(signal)
  }
  process.on(signal, handler)
  return handler
})
async function run(name, args, timeoutMs, environment = {}) {
  if (interrupted) throw new Error(`Interrupted by ${interrupted}`)
  const stdout = join(output, name + '.stdout.log')
  const stderr = join(output, name + '.stderr.log')
  const supervisorReport = join(output, name + '.supervisor.json')
  const command = [process.execPath, ...args]
  const supervisorArgs = [
    '--report',
    supervisorReport,
    '--timeout-ms',
    String(timeoutMs),
    '--cleanup-grace-ms',
    '1000',
    '--physical-ms',
    '6000',
    '--',
    ...command,
  ]
  const entry = {
    name,
    command,
    environment,
    supervisorCommand: [supervisor, ...supervisorArgs],
    timeoutMs,
    stdout,
    stderr,
    startedAt: new Date().toISOString(),
  }
  report.stages.push(entry)
  await save()
  const started = performance.now()
  const out = openSync(stdout, 'w'),
    err = openSync(stderr, 'w')
  try {
    if (interrupted) throw new Error(`Interrupted by ${interrupted}`)
    active = spawn(supervisor, supervisorArgs, {
      cwd: root,
      env: { ...process.env, ...environment },
      stdio: ['ignore', out, err],
    })
    const exit = await new Promise((resolveExit, reject) => {
      active.once('error', reject)
      active.once('close', (code, signal) => resolveExit({ code, signal }))
    })
    entry.exit = exit
    entry.receipt = JSON.parse(await readFile(supervisorReport, 'utf8'))
    if (
      exit.code !== 0 ||
      entry.receipt.timeout ||
      entry.receipt.interruption ||
      entry.receipt.cleanupFailed ||
      !entry.receipt.descendantsRetired ||
      entry.receipt.error ||
      entry.receipt.childExit?.code !== 0 ||
      entry.receipt.childExit?.signaled
    )
      throw new Error(`${name} failed; inspect ${stderr} and ${supervisorReport}`)
  } finally {
    active = undefined
    closeSync(out)
    closeSync(err)
    entry.durationMs = performance.now() - started
    await save()
  }
}
const node512 = ['--max-old-space-size=512', '--expose-gc']
try {
  await run(
    'emit',
    ['--max-old-space-size=512', join(root, 'node_modules/typescript/bin/tsc'), '-p', config],
    180_000,
  )
  await run(
    'unit',
    [
      '--max-old-space-size=768',
      join(root, 'node_modules/vitest/vitest.mjs'),
      'run',
      '--config',
      join(root, 'scripts/sdk-hardening.vitest.config.mjs'),
      '--maxWorkers=1',
      '--reporter=json',
      '--outputFile=' + join(output, 'unit-tests.json'),
    ],
    300_000,
  )
  await run(
    'protocol',
    [
      ...node512,
      join(root, 'scripts/sdk-protocol-hardening.mjs'),
      join(sdk, 'native-protocol.js'),
      join(output, 'protocol.json'),
    ],
    180_000,
  )
  await run(
    'values',
    [
      ...node512,
      join(root, 'scripts/sdk-values-hardening.mjs'),
      sdk,
      '120000',
      join(output, 'values.json'),
    ],
    180_000,
  )
  await run(
    'routes',
    [...node512, join(root, 'scripts/sdk-routes-fuzz.mjs'), sdk, String(0x57a9c21)],
    300_000,
  )
  await writeFile(
    join(output, 'routes.json'),
    JSON.stringify(JSON.parse(await readFile(join(output, 'routes.stdout.log'), 'utf8')), null, 2) +
      '\n',
  )
  await run(
    'lifecycle',
    [...node512, '--experimental-transform-types', join(root, 'scripts/sdk-lifecycle-fuzz.mjs')],
    300_000,
    { SUITECUT_LIFECYCLE_OUTPUT: join(output, 'lifecycle.json'), SUITECUT_LIFECYCLE_SOURCE: root },
  )
  if (arguments_.includes('--native'))
    await run(
      'native',
      [
        ...node512,
        join(root, 'scripts/sdk-native-hardening.mjs'),
        '1000000',
        join(output, 'native'),
      ],
      1_800_000,
    )
  report.status = 'passed'
} catch (error) {
  report.status = 'failed'
  report.error = error.message
  process.exitCode = interrupted ? 130 : 1
  console.error(error.message)
} finally {
  for (let index = 0; index < signals.length; index++)
    process.removeListener(signals[index], handlers[index])
  report.completedAt = new Date().toISOString()
  await save()
  console.log(`Browser SDK hardening ${report.status}: ${reportFile}`)
}
