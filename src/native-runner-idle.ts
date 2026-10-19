// SPDX-License-Identifier: MIT
// Typed receipt marshaling only. C++ owns the worker lease and future work.
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const UInt = z
  .string()
  .regex(/^[1-9][0-9]{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Count = z
  .union([z.literal('0'), UInt])
  .transform((value) => Number(value))
  .refine((value) => value <= 64)
const Fields = z.tuple([z.literal('WORKER_IDLE'), UInt, UInt, UInt, UInt, UInt, UInt, Count])
export interface NativeIdleSnapshot {
  readonly ready: boolean
  readonly workerFixtures: number
}
export class NativeWorkerIdleCallbacks {
  private nonce = 0n
  private pending: z.infer<typeof Fields> | undefined
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly identity: {
      readonly workerIndex: number
      readonly pid: number
      readonly fileId: () => string | undefined
    },
    private readonly snapshot: () => NativeIdleSnapshot,
    private readonly fatal: (error: Error) => void,
  ) {}
  packet(fields: readonly string[]): boolean {
    if (fields[0] !== 'WORKER_IDLE') return false
    const parsed = Fields.parse(fields)
    if (parsed[1] !== this.identity.fileId() || this.pending || BigInt(parsed[6]) <= this.nonce)
      throw new Error('Native idle request is duplicate or stale')
    this.nonce = BigInt(parsed[6])
    this.pending = parsed
    this.settled()
    return true
  }
  settled(): void {
    const pending = this.pending
    if (!pending) return
    const actual = this.snapshot()
    if (!actual.ready) return
    if (actual.workerFixtures !== pending[7])
      throw new Error('Native idle retained fixture ownership mismatch')
    this.pending = undefined
    void this.send(
      'WORKER_IDLE_READY',
      ...pending.slice(1),
      this.identity.workerIndex,
      this.identity.pid,
    ).catch((error: UntrustedInput) => {
      this.fatal(error instanceof Error ? error : new Error('Native idle receipt delivery failed'))
    })
  }
  beforeAuthorAdmission(): void {
    if (this.pending) throw new Error('Native author work arrived before idle retirement receipt')
  }
}
