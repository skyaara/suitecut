// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { spawn } from 'node:child_process'
import { isAbsolute } from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

import * as z from 'zod'

import {
  nativeDiscoveryArguments,
  nativeDiscoveryShape,
  type NativeSuiteDiscoveryOptions,
} from './native-runner-discovery.js'

const absolutePath = z.string().min(1).refine(isAbsolute, 'Native runner paths must be absolute')
const testId = z
  .string()
  .regex(/^[1-9][0-9]{0,19}$/)
  .pipe(
    z
      .string()
      .refine((value) => BigInt(value) <= 18446744073709551615n, 'Native test IDs must fit uint64'),
  )
const optionsSchema = z.strictObject({
  ...nativeDiscoveryShape,
  executablePath: absolutePath,
  suitePath: absolutePath.optional(),
  testDirectory: absolutePath
    .refine(
      (value) => !value.includes('\0') && Buffer.from(value, 'utf8').toString('utf8') === value,
      'Native testDirectory requires scalar filesystem UTF8',
    )
    .optional(),
  reportPath: absolutePath,
  reporter: z.enum(['json', 'none']).optional(),
  list: z.boolean().optional(),
  passWithNoTests: z.boolean().optional(),
  preserveOutput: z.enum(['always', 'never', 'failures-only']).optional(),
  grep: z
    .string()
    .max(4096)
    .refine(
      (value) => !value.includes('\0') && Buffer.from(value, 'utf8').toString('utf8') === value,
      'Native grep CLI patterns require scalar UTF8; use \\uXXXX escapes for lone UTF16 units',
    )
    .optional(),
  grepInvert: z
    .string()
    .max(4096)
    .refine(
      (value) => !value.includes('\0') && Buffer.from(value, 'utf8').toString('utf8') === value,
      'Native grep CLI patterns require scalar UTF8; use \\uXXXX escapes for lone UTF16 units',
    )
    .optional(),
  outputDirectory: absolutePath
    .refine(
      (value) =>
        !value.includes('\0') &&
        Buffer.byteLength(value) <= 7900 &&
        Buffer.from(value, 'utf8').toString('utf8') === value,
      'Native outputDirectory must be bounded filesystem UTF8 without NUL',
    )
    .optional(),
  junitPath: absolutePath.optional(),
  htmlPath: absolutePath.optional(),
  testIds: z.array(testId).max(4096).optional(),
  projects: z.array(z.string().min(1).max(256)).max(128).optional(),
  noDeps: z.boolean().optional(),
  workers: z.int().min(1).max(8).optional(),
  forbidOnly: z.boolean().optional(),
  failOnFlakyTests: z.boolean().optional(),
  repeatEach: z.number().int().min(0).max(Number.MAX_SAFE_INTEGER).optional(),
  maxFailures: z.int().min(0).max(4096).optional(),
  globalTimeout: z.int().min(0).max(2147483647).optional(),
  shard: z
    .strictObject({
      current: z.int().min(1).max(65536),
      total: z.int().min(1).max(65536),
    })
    .refine((value) => value.current <= value.total, 'Shard current must not exceed total')
    .optional(),
})

export interface NativeSuiteRunOptions extends NativeSuiteDiscoveryOptions {
  /** Separately built Linux C++ suitecut-runner executable. */
  executablePath: string
  /** User module exporting a defineNativeSuite declaration. */
  suitePath?: string
  /** Canonical root for the single suite's relative grep filename; default suite parent. */
  testDirectory?: string
  /** Native listing JSON when list is true; execution JSON otherwise. */
  reportPath: string
  /** Final native JSON on stdout or quiet summaries; default keeps native console output. */
  reporter?: 'json' | 'none'
  /** Collect declarations and apply native selectors without author callbacks or fixtures. */
  list?: boolean
  /** Permit an empty selection before sharding; empty shards already succeed. */
  passWithNoTests?: boolean
  /** Bounded native UTF16 CLI regex; bare patterns are Unicode17 legacy case-insensitive. */
  grep?: string
  /** Exclude titles matching this native CLI regex after applying grep. */
  grepInvert?: string
  /** Optional native-created parent for fresh per-attempt roots; no host file IO. */
  outputDirectory?: string
  /** Native assigned output-tree retention per attempt. Separate committed attachment roots are retained. Default: always. */
  preserveOutput?: 'always' | 'never' | 'failures-only'
  junitPath?: string
  /** Optional standalone native HTML report file. */
  htmlPath?: string
  /** Exact original declaration IDs, before sharding. Native C++ validates membership. */
  testIds?: readonly string[]
  /** Native ASCII case-insensitive project selectors; * matches any name bytes. */
  projects?: readonly string[]
  /** Ignore project dependencies, teardown inclusion and dependency failure gates. */
  noDeps?: boolean
  /** Native worker pool, 1..8; explicit parallel regions split tests into native work groups. */
  workers?: number
  /** Refuse focused declarations before admitting callbacks or fixtures. */
  forbidOnly?: boolean
  /** Fail the final native run when an actual test passed only after a retry. */
  failOnFlakyTests?: boolean
  /** Stop after this many final unexpected test outcomes; 0 means unlimited. */
  maxFailures?: number
  /** Native repetition; zero retains configuration while scheduling one iteration. */
  repeatEach?: number
  /** Original native suite budget in milliseconds, including loading and cleanup; 0 disables it. */
  globalTimeout?: number
  /** One-based balanced contiguous partition of selected primary tests; required project support runs in full. */
  shard?: { readonly current: number; readonly total: number }
}
export interface NativeSuiteRunResult {
  /** Actual native process exit code; null means it terminated from a signal. */
  exitCode: number | null
  signal: NodeJS.Signals | null
}

/** Launches the native scheduler once; it owns callbacks, deadlines and retries. */
export function runNativeSuite(options: NativeSuiteRunOptions): Promise<NativeSuiteRunResult> {
  const config = optionsSchema.parse(options)
  if ((config.suitePath === undefined) === (config.testDir === undefined))
    throw new Error('Choose exactly one suitePath or testDir')
  const args = [
    '--node',
    process.execPath,
    '--host-script',
    fileURLToPath(new URL('./native-runner-host.js', import.meta.url)),
    ...(config.suitePath === undefined ? [] : ['--suite', config.suitePath]),
    ...nativeDiscoveryArguments(config),
    ...(config.testDirectory === undefined ? [] : ['--test-directory', config.testDirectory]),
    '--report',
    config.reportPath,
    ...(config.reporter === undefined ? [] : ['--reporter', config.reporter]),
    ...(config.list ? ['--list'] : []),
    ...(config.passWithNoTests ? ['--pass-with-no-tests'] : []),
    ...(config.preserveOutput === undefined ? [] : ['--preserve-output', config.preserveOutput]),
    ...(config.grep === undefined ? [] : ['--grep', config.grep]),
    ...(config.grepInvert === undefined ? [] : ['--grep-invert', config.grepInvert]),
    ...(config.outputDirectory === undefined ? [] : ['--output-directory', config.outputDirectory]),
    ...(config.junitPath === undefined ? [] : ['--junit', config.junitPath]),
    ...(config.htmlPath === undefined ? [] : ['--html', config.htmlPath]),
    ...(config.testIds ?? []).flatMap((id) => ['--test-id', id]),
    ...(config.projects ?? []).flatMap((name) => ['--project', name]),
    ...(config.noDeps ? ['--no-deps'] : []),
    ...(config.workers === undefined ? [] : ['--workers', String(config.workers)]),
    ...(config.forbidOnly ? ['--forbid-only'] : []),
    ...(config.failOnFlakyTests ? ['--fail-on-flaky-tests'] : []),
    ...(config.repeatEach === undefined ? [] : ['--repeat-each', String(config.repeatEach)]),
    ...(config.maxFailures === undefined ? [] : ['--max-failures', String(config.maxFailures)]),
    ...(config.globalTimeout === undefined
      ? []
      : ['--global-timeout', String(config.globalTimeout)]),
    ...(config.shard === undefined
      ? []
      : ['--shard', `${config.shard.current}/${config.shard.total}`]),
  ]
  return new Promise((resolve, reject) => {
    const child = spawn(config.executablePath, args, {
      stdio: 'inherit',
      shell: false,
    })
    child.once('error', reject)
    child.once('close', (exitCode, signal) => resolve({ exitCode, signal }))
  })
}
