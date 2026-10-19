#!/usr/bin/env node
// SPDX-License-Identifier: MIT
import console from 'node:console'
import { constants } from 'node:os'
import process from 'node:process'
import { parseArgs } from 'node:util'

import * as z from 'zod'

import { runNativeSuite } from './native-runner.js'
import { toError, type UntrustedInput } from './untrusted.js'

try {
  const { values } = parseArgs({
    options: {
      executable: { type: 'string' },
      suite: { type: 'string' },
      'test-dir': { type: 'string' },
      'test-match': { type: 'string', multiple: true },
      'test-ignore': { type: 'string', multiple: true },
      'file-filter': { type: 'string', multiple: true },
      'test-directory': { type: 'string' },
      report: { type: 'string' },
      reporter: { type: 'string' },
      list: { type: 'boolean' },
      'pass-with-no-tests': { type: 'boolean' },
      grep: { type: 'string', short: 'g' },
      'grep-invert': { type: 'string', short: 'G' },
      'output-directory': { type: 'string' },
      'preserve-output': { type: 'string' },
      junit: { type: 'string' },
      html: { type: 'string' },
      'test-id': { type: 'string', multiple: true },
      project: { type: 'string', multiple: true },
      'no-deps': { type: 'boolean' },
      workers: { type: 'string' },
      shard: { type: 'string' },
      'forbid-only': { type: 'boolean' },
      'fail-on-flaky-tests': { type: 'boolean' },
      'max-failures': { type: 'string' },
      'repeat-each': { type: 'string' },
      'global-timeout': { type: 'string' },
      help: { type: 'boolean' },
    },
    strict: true,
    allowPositionals: false,
  })
  if (values.help)
    console.log(
      'suitecut-test --executable /absolute/suitecut-runner (--suite /absolute/tests.mjs | --test-dir /absolute/tests) --report /absolute/result.json [--reporter json|none] [--list] [--pass-with-no-tests] [--test-directory /absolute/root] [--grep REGEX] [--grep-invert REGEX] [--output-directory /absolute/output-root] [--preserve-output always|never|failures-only] [--junit /absolute/result.xml] [--html /absolute/result.html] [--test-id ID ...] [--project PATTERN ...] [--no-deps] [--workers N] [--shard CURRENT/TOTAL] [--forbid-only] [--fail-on-flaky-tests] [--repeat-each N] [--max-failures N] [--global-timeout MS]\n--list writes native declaration JSON without fixtures, hooks or test bodies; importing the module still executes its top-level code.\nNative workers are bounded to1..8; parallel regions use native project or suite-file work groups.',
    )
  else {
    const config = z
      .object({
        executable: z.string(),
        suite: z.string().optional(),
        'test-dir': z.string().optional(),
        'test-match': z.array(z.string()).optional(),
        'test-ignore': z.array(z.string()).optional(),
        'file-filter': z.array(z.string()).optional(),
        'test-directory': z.string().optional(),
        report: z.string(),
        reporter: z.enum(['json', 'none']).optional(),
        list: z.boolean().optional(),
        'pass-with-no-tests': z.boolean().optional(),
        grep: z.string().optional(),
        'grep-invert': z.string().optional(),
        'output-directory': z.string().optional(),
        'preserve-output': z.enum(['always', 'never', 'failures-only']).optional(),
        junit: z.string().optional(),
        html: z.string().optional(),
        'test-id': z.array(z.string()).optional(),
        project: z.array(z.string()).optional(),
        'no-deps': z.boolean().optional(),
        workers: z
          .string()
          .regex(/^[1-8]$/)
          .optional(),
        'forbid-only': z.boolean().optional(),
        'fail-on-flaky-tests': z.boolean().optional(),
        'global-timeout': z
          .string()
          .regex(/^[0-9]+$/)
          .optional(),
        'repeat-each': z
          .string()
          .regex(/^[0-9]+$/)
          .refine((value) => Number.isSafeInteger(Number(value)))
          .optional(),
        'max-failures': z
          .string()
          .regex(/^[0-9]+$/)
          .optional(),
        shard: z
          .string()
          .regex(/^[1-9][0-9]*\/[1-9][0-9]*$/)
          .optional(),
      })
      .parse(values)
    const actual = await runNativeSuite({
      executablePath: config.executable,
      ...(config.suite === undefined ? {} : { suitePath: config.suite }),
      ...(config['test-dir'] === undefined ? {} : { testDir: config['test-dir'] }),
      ...(config['test-match'] === undefined ? {} : { testMatch: config['test-match'] }),
      ...(config['test-ignore'] === undefined ? {} : { testIgnore: config['test-ignore'] }),
      ...(config['file-filter'] === undefined ? {} : { fileFilters: config['file-filter'] }),
      ...(config['test-directory'] === undefined
        ? {}
        : { testDirectory: config['test-directory'] }),
      reportPath: config.report,
      ...(config.reporter === undefined ? {} : { reporter: config.reporter }),
      ...(config.list === undefined ? {} : { list: config.list }),
      ...(config['pass-with-no-tests'] === undefined
        ? {}
        : { passWithNoTests: config['pass-with-no-tests'] }),
      ...(config.grep === undefined ? {} : { grep: config.grep }),
      ...(config['grep-invert'] === undefined ? {} : { grepInvert: config['grep-invert'] }),
      ...(config['output-directory'] === undefined
        ? {}
        : { outputDirectory: config['output-directory'] }),
      ...(config['preserve-output'] === undefined
        ? {}
        : { preserveOutput: config['preserve-output'] }),
      ...(config.junit === undefined ? {} : { junitPath: config.junit }),
      ...(config.html === undefined ? {} : { htmlPath: config.html }),
      ...(config.project === undefined ? {} : { projects: config.project }),
      ...(config['no-deps'] === undefined ? {} : { noDeps: config['no-deps'] }),
      ...(config.workers === undefined ? {} : { workers: Number(config.workers) }),
      ...(config['test-id'] === undefined ? {} : { testIds: config['test-id'] }),
      ...(config['forbid-only'] === undefined ? {} : { forbidOnly: config['forbid-only'] }),
      ...(config['fail-on-flaky-tests'] === undefined
        ? {}
        : { failOnFlakyTests: config['fail-on-flaky-tests'] }),
      ...(config['global-timeout'] === undefined
        ? {}
        : { globalTimeout: Number(config['global-timeout']) }),
      ...(config['repeat-each'] === undefined ? {} : { repeatEach: Number(config['repeat-each']) }),
      ...(config['max-failures'] === undefined
        ? {}
        : { maxFailures: Number(config['max-failures']) }),
      ...(config.shard === undefined
        ? {}
        : {
            shard: {
              current: Number(config.shard.split('/')[0]),
              total: Number(config.shard.split('/')[1]),
            },
          }),
    })
    if (actual.signal !== null) {
      const signalNumber = z
        .number()
        .int()
        .positive()
        .max(127)
        .parse(Reflect.get(constants.signals, actual.signal))
      console.error('Native runner terminated from actual signal ' + actual.signal)
      process.exitCode = 128 + signalNumber
    } else process.exitCode = actual.exitCode ?? 1
  }
} catch (error) {
  console.error(toError(error as UntrustedInput).message)
  process.exitCode = 2
}
