// SPDX-License-Identifier: MIT
import { open, mkdir, writeFile } from 'node:fs/promises'
import { dirname } from 'node:path'

import * as z from 'zod'

import {
  runNativeRetryCleanup,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { NativeVirtualCredentialSchema } from './native-credentials.js'
import { type UntrustedInput } from './untrusted.js'

const Cookie = z.strictObject({
  name: z.string().max(4096),
  value: z.string().max(4096),
  domain: z.string().max(1024),
  path: z.string().max(8192),
  expires: z.number().finite().min(-1).max(253402300799),
  httpOnly: z.boolean(),
  secure: z.boolean(),
  sameSite: z.enum(['Strict', 'Lax', 'None']),
  partitionKey: z.string().max(8192).optional(),
  partitionKeyHasCrossSiteAncestor: z.boolean().optional(),
})
const KeyPath = z.union([z.string(), z.array(z.string()).max(128)])
const RecordValue = z
  .strictObject({
    key: z.unknown().optional(),
    keyEncoded: z.unknown().optional(),
    value: z.unknown().optional(),
    valueEncoded: z.unknown().optional(),
  })
  .refine((record) => Object.hasOwn(record, 'value') !== Object.hasOwn(record, 'valueEncoded'), {
    error: 'An IndexedDB record requires exactly one value or valueEncoded',
  })
  .refine((record) => !(Object.hasOwn(record, 'key') && Object.hasOwn(record, 'keyEncoded')), {
    error: 'An IndexedDB record cannot contain both key and keyEncoded',
  })
const OPFSPath = z
  .string()
  .min(1)
  .max(4096)
  .refine(
    (path) => {
      const segments = path.split('/')
      return (
        segments.length <= 64 &&
        segments.every(
          (name) =>
            name.length > 0 &&
            name.length <= 255 &&
            name !== '.' &&
            name !== '..' &&
            !name.includes('\\') &&
            !name.includes('\0'),
        )
      )
    },
    { error: 'Invalid OPFS path' },
  )
const OPFSEntry = z.discriminatedUnion('type', [
  z.strictObject({ path: OPFSPath, type: z.literal('directory') }),
  z.strictObject({
    path: OPFSPath,
    type: z.literal('file'),
    base64: z
      .string()
      .max(524288)
      .regex(/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/),
  }),
])
const Database = z.strictObject({
  name: z.string(),
  version: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
  stores: z
    .array(
      z.strictObject({
        name: z.string(),
        keyPath: KeyPath.nullable(),
        autoIncrement: z.boolean(),
        indexes: z
          .array(
            z.strictObject({
              name: z.string(),
              keyPath: KeyPath,
              unique: z.boolean(),
              multiEntry: z.boolean(),
            }),
          )
          .max(128),
        records: z.array(RecordValue).max(10000),
      }),
    )
    .max(128),
})
const NativeStorageOriginSchema = z.strictObject({
  origin: z.url({ error: (issue) => `Invalid storage origin ${String(issue.input)}` }),
  localStorage: z.array(z.strictObject({ name: z.string(), value: z.string() })).max(4096),
  indexedDB: z.array(Database).max(128).optional(),
  opfs: z.array(OPFSEntry).max(4096).optional(),
})
export const NativeStorageStateSchema = z.strictObject({
  cookies: z.array(Cookie).max(4096),
  origins: z.array(NativeStorageOriginSchema).max(1024),
  credentials: z.array(NativeVirtualCredentialSchema).max(128).optional(),
})
const NativeStorageWireSchema = NativeStorageStateSchema.extend({
  origins: z
    .array(
      z.union([
        NativeStorageOriginSchema,
        z
          .strictObject({
            origin: z.string(),
            stateJSON: z.string().max(524288),
            empty: z.boolean().optional(),
          })
          .transform((wire) => {
            const origin = NativeStorageOriginSchema.parse(JSON.parse(wire.stateJSON))
            if (origin.origin !== wire.origin)
              throw new Error('Native storage wire origin mismatch')
            return origin
          }),
      ]),
    )
    .max(1024),
})
// Inputs may omit empty collections; native captures keep their required shape.
export const NativeStorageStateInputSchema = NativeStorageStateSchema.extend({
  cookies: NativeStorageStateSchema.shape.cookies.default([]),
  origins: NativeStorageStateSchema.shape.origins.default([]),
})
export type NativeStorageStateInput = z.input<typeof NativeStorageStateInputSchema>
export type NativeStorageState = z.infer<typeof NativeStorageStateSchema>
export interface NativeStorageTransport {
  runContext(action: string, params?: Record<string, UntrustedInput>): Promise<UntrustedInput>
}
export async function readNativeStorageState(
  input: string | NativeStorageStateInput,
): Promise<NativeStorageState> {
  let value: UntrustedInput = input
  if (typeof input === 'string') {
    try {
      const file = await open(input, 'r')
      try {
        const buffer = Buffer.alloc(32 * 1024 * 1024 + 1)
        let length = 0
        while (length < buffer.length) {
          const read = await file.read(buffer, length, buffer.length - length, null)
          if (!read.bytesRead) break
          length += read.bytesRead
        }
        if (length > 32 * 1024 * 1024) throw new Error('Native storage state file exceeds 32 MiB')
        value = JSON.parse(buffer.subarray(0, length).toString('utf8')) as UntrustedInput
      } finally {
        await file.close()
      }
    } catch (error) {
      throw new Error(
        `Error reading storage state from ${input}:\n${error instanceof Error ? error.message : String(error)}`,
        { cause: error },
      )
    }
  }
  const state = NativeStorageStateInputSchema.parse(value)
  if (Buffer.byteLength(JSON.stringify(state)) > 8 * 1024 * 1024)
    throw new Error('Native storage state exceeds 8 MiB')
  return state
}
export async function captureNativeStorageState(
  transport: NativeStorageTransport,
  contextId: number,
  options: { path?: string; indexedDB?: boolean; opfs?: boolean; credentials?: boolean } = {},
): Promise<NativeStorageState> {
  return runNativeRetryOperation(async () => {
    const config = withoutNativeRetryOperation(() =>
      z
        .strictObject({
          path: z.string().optional(),
          indexedDB: z.boolean().optional(),
          opfs: z.boolean().optional(),
          credentials: z.boolean().optional(),
        })
        .parse(options),
    )
    const state = NativeStorageWireSchema.parse(
      await transport.runContext('storageState', {
        contextId,
        indexedDB: config.indexedDB ?? false,
        opfs: config.opfs ?? false,
        credentials: config.credentials ?? false,
      }),
    )
    if (config.path !== undefined) {
      await mkdir(dirname(config.path), { recursive: true })
      await writeFile(config.path, JSON.stringify(state, null, 2), { mode: 0o600 })
    }
    return state
  })
}
export async function restoreNativeStorageState(
  transport: NativeStorageTransport,
  contextId: number,
  input: string | NativeStorageStateInput,
): Promise<void> {
  const state = await withoutNativeRetryOperation(() => readNativeStorageState(input))
  // Keep isolated UTF-16 units in a native-owned JSON string, because CEF's
  // UTF-8 dictionaries reject them. This leaves the public storage schema intact.
  const json = withoutNativeRetryOperation(() =>
    JSON.stringify({
      ...state,
      origins: state.origins.map((origin) => {
        const stateJSON = JSON.stringify(origin)
        return /\\ud[89a-f][0-9a-f]{2}/i.test(stateJSON)
          ? { origin: origin.origin, stateJSON }
          : origin
      }),
    }),
  )
  const result = z
    .object({ uploadId: z.string().min(1).max(32) })
    .parse(await transport.runContext('storageUploadBegin', { contextId }))
  try {
    for (let offset = 0; offset < json.length;) {
      let end = Math.min(offset + 8000, json.length)
      // Preserve Unicode pairs through each independently decoded CEF command.
      if (
        end < json.length &&
        json.charCodeAt(end - 1) >= 0xd800 &&
        json.charCodeAt(end - 1) <= 0xdbff
      )
        end--
      await transport.runContext('storageUploadAppend', {
        contextId,
        uploadId: result.uploadId,
        data: json.slice(offset, end),
      })
      offset = end
    }
    await transport.runContext('setStorageState', { contextId, uploadId: result.uploadId })
  } finally {
    await runNativeRetryCleanup(() =>
      transport.runContext('storageUploadDiscard', { contextId, uploadId: result.uploadId }),
    ).catch(() => undefined)
  }
}
