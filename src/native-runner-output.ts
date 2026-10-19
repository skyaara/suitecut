// SPDX-License-Identifier: MIT
// Native admission metadata and bounded lexical path marshaling only.
import { Buffer } from 'node:buffer'
import { isAbsolute, relative, resolve, sep } from 'node:path'

import * as z from 'zod'

export interface NativeTestOutput {
  /** Actual native-created per-attempt directory; arbitrary child files are not implied. */
  readonly outputDir: string
  /** Lexical path within the real assigned directory. Creates no child directories or files. */
  readonly outputPath: (...segments: readonly string[]) => string
}
const UInt = z
  .string()
  .regex(/^[1-9][0-9]{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Nonnegative = z.union([z.literal('0'), UInt])
const PathWire = z
  .string()
  .min(2)
  .max(16384)
  .regex(/^(?:[a-f\d]{2})+$/u)
const Segment = z
  .string()
  .max(4096)
  .refine(
    (value) => !value.includes('\0') && !isAbsolute(value),
    'Native output path segments must be relative and contain no NUL',
  )
interface OutputContext {
  readonly id: string
  readonly testId: string
  readonly generation: string
  readonly value: NativeTestOutput
}
export class NativeOutputCallbacks {
  private current?: OutputContext
  private lastId = 0n
  private lastGeneration = 0n
  constructor(private readonly send: (...fields: (string | number)[]) => Promise<void>) {}
  async packet(fields: readonly string[], workerIndex: number, pid: number): Promise<boolean> {
    if (fields[0] !== 'OUTPUT_CONTEXT') return false
    const [, id, testId, generation, worker, actualPid, pathWire] = z
      .tuple([
        z.literal('OUTPUT_CONTEXT'),
        UInt,
        UInt,
        UInt,
        z.coerce.number().int().min(1),
        z.coerce.number().int().min(1).max(2147483647),
        PathWire,
        Nonnegative,
        UInt,
      ])
      .parse(fields)
    if (
      worker !== workerIndex ||
      actualPid !== pid ||
      BigInt(id) <= this.lastId ||
      BigInt(generation) <= this.lastGeneration
    )
      throw new Error('Stale native output directory admission owner')
    const bytes = Buffer.from(pathWire, 'hex'),
      outputDir = bytes.toString('utf8')
    if (
      !Buffer.from(outputDir, 'utf8').equals(bytes) ||
      !isAbsolute(outputDir) ||
      outputDir.includes('\0') ||
      resolve(outputDir) !== outputDir
    )
      throw new Error('Invalid native output directory path')
    const value: NativeTestOutput = Object.freeze({
      outputDir,
      outputPath: (...segments: readonly string[]) => {
        const actual = z.array(Segment).max(32).parse(segments)
        const path = resolve(outputDir, ...actual),
          suffix = relative(outputDir, path)
        if (
          path.length > 8192 ||
          Buffer.byteLength(path) > 8192 ||
          suffix === '..' ||
          suffix.startsWith('..' + sep) ||
          isAbsolute(suffix)
        )
          throw new Error('Native outputPath must stay within the assigned native output directory')
        return path
      },
    })
    this.current = Object.freeze({ id, testId, generation, value })
    this.lastId = BigInt(id)
    this.lastGeneration = BigInt(generation)
    await this.send('OUTPUT_CONTEXT_READY', id, testId, generation)
    return true
  }
  snapshot(testId: string): NativeTestOutput {
    if (this.current?.testId !== testId)
      throw new Error('Native callback has no actual output directory admission')
    return this.current.value
  }
}
