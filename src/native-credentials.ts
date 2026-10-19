/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase the storage-schema initialization edge. */
import * as z from 'zod'

import type { NativeStorageTransport } from './native-storage-state.js'

export const NativeVirtualCredentialSchema = z.strictObject({
  id: z.string().min(1).max(1366),
  rpId: z.string().min(1).max(253),
  userHandle: z.string().min(1).max(86),
  privateKey: z.string().min(1).max(21846),
  publicKey: z.string().min(1).max(21846),
  signCount: z
    .number()
    .refine((value) => Number.isInteger(value) && value >= 0 && value <= 4294967295, {
      error: (issue) => `signCount must be between 0 and 4294967295, got ${String(issue.input)}`,
    })
    .default(0),
})
export type NativeVirtualCredential = z.infer<typeof NativeVirtualCredentialSchema>
export interface NativeCredentialOptions {
  id?: string
  rpId?: string
  userHandle?: string
  privateKey?: string
  publicKey?: string
  signCount?: number
}
const Options = NativeVirtualCredentialSchema.partial()
const Filter = z.strictObject({
  rpId: z.string().max(253).optional(),
  id: z.string().max(1366).optional(),
})

/** Commands genuine browser-owned key storage and CEF virtual authenticators. */
export class NativeCredentials {
  constructor(
    private readonly transport: NativeStorageTransport,
    private readonly contextId: number,
    private readonly assertOpen: () => void,
  ) {}
  async create(
    rpId: string,
    options: NativeCredentialOptions = {},
  ): Promise<NativeVirtualCredential> {
    this.assertOpen()
    const config = Options.parse(options)
    if (config.rpId !== undefined && config.rpId !== rpId)
      throw new Error('Credential rpId does not match create(rpId)')
    return NativeVirtualCredentialSchema.parse(
      await this.transport.runContext('credentialsCreate', {
        ...config,
        rpId: z.string().min(1).max(253).parse(rpId),
        contextId: this.contextId,
      }),
    )
  }
  async get(filter: { rpId?: string; id?: string } = {}): Promise<NativeVirtualCredential[]> {
    this.assertOpen()
    const value = z.object({ credentials: z.array(NativeVirtualCredentialSchema).max(128) }).parse(
      await this.transport.runContext('credentialsGet', {
        ...Filter.parse(filter),
        contextId: this.contextId,
      }),
    )
    return value.credentials
  }
  async delete(id: string): Promise<void> {
    this.assertOpen()
    await this.transport.runContext('credentialsDelete', {
      contextId: this.contextId,
      id: z.string().min(1).max(1366).parse(id),
    })
  }
  async install(): Promise<void> {
    this.assertOpen()
    await this.transport.runContext('credentialsInstall', { contextId: this.contextId })
  }
}
