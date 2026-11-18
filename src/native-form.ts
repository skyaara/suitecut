// SPDX-License-Identifier: MIT
import { stat } from 'node:fs/promises'
import { resolve } from 'node:path'

import * as z from 'zod'

import {
  runNativeRetryCleanup,
  nativeRetryInvocation,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { type NativeElementHandle } from './native-page.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeFormOptions {
  timeout?: number
  force?: boolean
  trial?: boolean
  position?: { x: number; y: number }
}

export interface NativeSelectOption {
  value?: string
  label?: string
  index?: number
}

export interface NativeFilePayload {
  name: string
  mimeType: string
  buffer: Buffer
}

export type NativeSelectValues =
  | string
  | NativeSelectOption
  | NativeElementHandle
  | readonly (string | NativeSelectOption | NativeElementHandle)[]
  | null
export type NativeInputFiles =
  string | readonly string[] | NativeFilePayload | readonly NativeFilePayload[]

type Dispatch = (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>
const formOptions = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  force: z.boolean().optional(),
  trial: z.boolean().optional(),
  position: z.strictObject({ x: z.number().finite(), y: z.number().finite() }).optional(),
})
const pageFormOptions = formOptions.extend({ strict: z.boolean().optional() })
export type NativePageFormOptions = z.input<typeof pageFormOptions>

/** Page selectors choose the first native match unless strict is requested. */
export function parseNativePageFormOptions(options: NativePageFormOptions) {
  const parsed = withoutNativeRetryOperation(() => pageFormOptions.parse(options))
  const native: NativeFormOptions = {}
  if (parsed.timeout !== undefined) native.timeout = parsed.timeout
  if (parsed.force !== undefined) native.force = parsed.force
  if (parsed.trial !== undefined) native.trial = parsed.trial
  if (parsed.position !== undefined) native.position = parsed.position
  return { ...native, strict: parsed.strict ?? false }
}
const option = z
  .strictObject({
    value: z.string().optional(),
    label: z.string().optional(),
    index: z.number().int().min(0).optional(),
  })
  .refine(
    (value) => Object.keys(value).length > 0,
    'A select option requires value, label or index',
  )

/** Thin argument validation; the C++ engine owns form retries and completion. */
export class NativeFormActions {
  constructor(
    private readonly dispatch: Dispatch,
    private readonly optionHandle?: (value: UntrustedInput) => number | undefined,
  ) {}

  async setChecked(checked: boolean, options: NativeFormOptions = {}): Promise<void> {
    const config = withoutNativeRetryOperation(() => formOptions.parse(options))
    await this.dispatch({
      action: 'setChecked',
      checked: withoutNativeRetryOperation(() => z.boolean().parse(checked)),
      force: config.force ?? false,
      trial: config.trial ?? false,
      ...(config.position ? { position: config.position } : {}),
      ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
    })
  }

  async selectOption(
    values: NativeSelectValues,
    options: NativeFormOptions = {},
  ): Promise<string[]> {
    const config = withoutNativeRetryOperation(() =>
      formOptions.omit({ trial: true, position: true }).parse(options),
    )
    const list = values === null ? [] : Array.isArray(values) ? values : [values]
    const choices = list.map(
      (value: string | NativeSelectOption | NativeElementHandle, index: number) => {
        const handle = this.optionHandle?.(value)
        if (handle !== undefined) return { handle }
        if (typeof value === 'string') return { valueOrLabel: value }
        const result = withoutNativeRetryOperation(() => option.safeParse(value))
        if (result.success) return result.data
        const issue = result.error.issues[0]
        if (issue?.code === 'invalid_type') {
          const field = issue.path[0]
          const raw: UntrustedInput =
            field !== undefined && value !== null && typeof value === 'object'
              ? (Reflect.get(value, field) as UntrustedInput)
              : value
          const expected = field === 'index' ? 'integer' : issue.expected
          const actual = raw === null ? 'null' : typeof raw
          throw new Error(
            `options[${index}]${field !== undefined ? `.${String(field)}` : ''}: expected ${expected}, got ${actual}`,
            { cause: result.error },
          )
        }
        throw result.error
      },
    )
    return z.array(z.string()).parse(
      await this.dispatch({
        action: 'selectOption',
        options: choices,
        force: config.force ?? false,
        ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
      }),
    )
  }

  async setInputFiles(
    files: NativeInputFiles,
    options: Omit<NativeFormOptions, 'force'> = {},
  ): Promise<void> {
    return runNativeRetryOperation(async () => {
      const config = withoutNativeRetryOperation(() =>
        formOptions.omit({ force: true, trial: true, position: true }).parse(options),
      )
      const list = withoutNativeRetryOperation(
        () =>
          (Array.isArray(files) ? Array.from(files) : [files]) as (string | NativeFilePayload)[],
      )
      const paths: { path: string; directory: boolean }[] = []
      const payloads: NativeFilePayload[] = []
      for (const entry of list) {
        if (typeof entry === 'string') {
          const path = resolve(entry)
          const info = await stat(path)
          if (!info.isFile() && !info.isDirectory())
            throw new Error(`Upload path is not a regular file or directory: ${path}`)
          paths.push({ path, directory: info.isDirectory() })
        } else {
          const parsed = withoutNativeRetryOperation(() =>
            z
              .strictObject({
                name: z
                  .string()
                  .min(1)
                  .refine(
                    (name) => !/[/\\\0]/u.test(name) && name !== '.' && name !== '..',
                    'File name must be a basename',
                  ),
                mimeType: z.string(),
                buffer: z.instanceof(Buffer),
              })
              .parse(entry),
          )
          payloads.push(
            nativeRetryInvocation()
              ? { ...parsed, buffer: withoutNativeRetryOperation(() => Buffer.from(parsed.buffer)) }
              : parsed,
          )
        }
      }
      if (paths.length && payloads.length)
        throw new Error('Upload paths and in-memory payloads cannot be mixed in one command')
      const directories = paths.filter((entry) => entry.directory)
      if (directories.length > 1) throw new Error('Multiple directories are not supported')
      if (directories.length && paths.length > 1)
        throw new Error('File paths must be all files or a single directory')
      if (!payloads.length) {
        await this.dispatch({
          action: 'setInputFiles',
          files: paths,
          ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
        })
        return
      }
      const metadata = payloads.map(({ name, mimeType, buffer }) => ({
        name,
        mimeType,
        size: buffer.length,
      }))
      const token = z
        .string()
        .min(1)
        .parse(
          await this.dispatch({
            action: 'uploadBegin',
            files: metadata,
            ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
          }),
        )
      try {
        for (let fileIndex = 0; fileIndex < payloads.length; ++fileIndex) {
          const payload = payloads[fileIndex]
          if (!payload) throw new Error('Native upload payload disappeared during staging')
          for (let offset = 0; offset < payload.buffer.length; offset += 24 * 1024) {
            await this.dispatch({
              action: 'uploadAppend',
              uploadToken: token,
              fileIndex,
              base64: payload.buffer.subarray(offset, offset + 24 * 1024).toString('base64'),
              ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
            })
          }
        }
        await this.dispatch({
          action: 'setInputFiles',
          uploadToken: token,
          ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
        })
      } catch (error) {
        try {
          await runNativeRetryCleanup(() =>
            this.dispatch({ action: 'uploadAbort', uploadToken: token }),
          )
        } catch (cleanupError) {
          throw new AggregateError(
            [error, cleanupError],
            'Native upload and staging cleanup failed',
            { cause: cleanupError },
          )
        }
        throw error
      }
    })
  }
}
