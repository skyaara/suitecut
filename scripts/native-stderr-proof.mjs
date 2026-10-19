// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { appendFileSync } from 'node:fs'
import { readFile, writeFile } from 'node:fs/promises'
import { join } from 'node:path'

import * as z from 'zod'

/**
 * Retains bounded diagnostic callback text without filtering native assertions.
 * @param {string} directory
 */
export async function captureNativeStderr(directory) {
  const file = join(directory, 'native-stderr.log')
  await writeFile(file, '')
  const bound = 2 * 1024 * 1024
  let observedBytes = 0,
    retainedBytes = 0,
    chunks = 0,
    overflow = false
  /**
   * Record each actual native diagnostic without filtering.
   * @type {(message:string)=>void}
   */
  const observe = (message) => {
    const data = Buffer.from(z.string().parse(message), 'utf8')
    observedBytes += data.length
    ++chunks
    const retained = data.subarray(0, Math.max(0, bound - retainedBytes))
    appendFileSync(file, retained)
    retainedBytes += retained.length
    if (retained.length !== data.length) {
      overflow = true
      throw new Error('Actual native diagnostic text exceeded its 2 MiB proof bound')
    }
  }
  const summary = async () => {
    const text = await readFile(file, 'utf8')
    const assertions = text
      .split(/\r?\n/u)
      .filter((line) => /NOTREACHED|CHECK failed|\bFATAL\b/u.test(line))
    return {
      observedUtf8Bytes: observedBytes,
      retainedUtf8Bytes: retainedBytes,
      chunks,
      overflow,
      assertionLines: assertions.length,
      strictStderrClean: !overflow && observedBytes === 0,
      textFile: file,
    }
  }
  const finish = async () => {
    const diagnostics = await summary()
    await writeFile(
      join(directory, 'native-stderr-summary.json'),
      JSON.stringify(diagnostics, null, 2) + '\n',
    )
    for (const name of ['report.json', 'failure.json']) {
      const path = join(directory, name)
      try {
        const report = z
          .record(z.string(), z.unknown())
          .parse(JSON.parse(await readFile(path, 'utf8')))
        report.nativeStderr = diagnostics
        if (!diagnostics.strictStderrClean) {
          report.behaviorCompleted = report.completed
          report.completed = false
          report.diagnosticFailure =
            'Actual native process emitted diagnostic text; see native-stderr.log'
        }
        await writeFile(path, JSON.stringify(report, null, 2) + '\n')
      } catch (error) {
        if (!(error instanceof Error && 'code' in error && error.code === 'ENOENT')) throw error
      }
    }
    assert.equal(
      diagnostics.observedUtf8Bytes,
      0,
      'Actual native process emitted diagnostic text; see native-stderr.log',
    )
    return diagnostics
  }
  return { observe, summary, finish }
}

/**
 * Retains real shutdown and diagnostic failures without hiding a behavior error.
 * @param {{close(): Promise<void>}} source
 * @param {Awaited<ReturnType<typeof captureNativeStderr>>} diagnostics
 * @param {string} directory
 * @param {readonly unknown[]} behaviorErrors
 */
export async function finishNativeProof(source, diagnostics, directory, behaviorErrors = []) {
  /** @type {unknown[]} */
  const failures = []
  /** @type {{origin:string,message:string,stack:string|undefined}[]} */
  const evidence = []
  /**
   * Records one exact thrown value and its serializable evidence.
   * @param {string} origin
   * @param {unknown} error
   */
  const record = (origin, error) => {
    failures.push(error)
    evidence.push({
      origin,
      message: error instanceof Error ? error.message : String(error),
      stack: error instanceof Error ? error.stack : undefined,
    })
  }
  try {
    await source.close()
  } catch (error) {
    record('source.close', error)
  }
  try {
    await diagnostics.finish()
  } catch (error) {
    record('native diagnostic finalization', error)
  }
  if (!failures.length) return
  await writeFile(
    join(directory, 'native-finalization-errors.json'),
    JSON.stringify({ completed: false, failures: evidence }, null, 2) + '\n',
  )
  for (const name of ['report.json', 'failure.json']) {
    const file = join(directory, name)
    try {
      const report = z
        .record(z.string(), z.unknown())
        .parse(JSON.parse(await readFile(file, 'utf8')))
      if (report.completed === true) report.behaviorCompleted = true
      report.completed = false
      report.nativeFinalizationFailures = evidence
      await writeFile(file, JSON.stringify(report, null, 2) + '\n')
    } catch (error) {
      if (!(error instanceof Error && 'code' in error && error.code === 'ENOENT'))
        record('proof evidence persistence', error)
    }
  }
  const all = [...behaviorErrors, ...failures]
  if (all.length === 1) throw all[0]
  throw new AggregateError(
    all,
    'Actual native proof behavior or shutdown failed; see retained evidence',
  )
}
