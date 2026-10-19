// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'
import { resolve } from 'node:path'

import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'

const HARPathSchema = z
  .string()
  .min(1)
  .max(4096)
  .refine((path) => !path.includes('\0'))
  .transform((path) => resolve(path))
export const NativeRecordHARSchema = z
  .object({
    path: HARPathSchema,
    content: z.enum(['embed', 'attach', 'omit']).optional(),
    omitContent: z.boolean().optional(),
    mode: z.enum(['full', 'minimal']).optional(),
    urlFilter: z.string().min(1).max(8192).optional(),
  })
  .transform(({ omitContent, content, ...config }) => ({
    ...config,
    content: omitContent
      ? ('omit' as const)
      : (content ?? (config.path.endsWith('.zip') ? ('attach' as const) : ('embed' as const))),
  }))
export type NativeRecordHAROptions = z.input<typeof NativeRecordHARSchema>
const ReplayOptions = z.strictObject({
  notFound: z.enum(['abort', 'fallback']).optional(),
  update: z.boolean().optional(),
  updateContent: z.enum(['embed', 'attach']).optional(),
  updateMode: z.enum(['full', 'minimal']).optional(),
  url: z.string().min(1).max(8192).optional(),
})
export type NativeRouteFromHAROptions = z.input<typeof ReplayOptions>
export async function routeNativeFromHAR(
  source: {
    runAutomation(
      command: 'har',
      params: Parameters<NativeBrowser['runAutomation']>[1],
    ): ReturnType<NativeBrowser['runAutomation']>
  },
  path: string,
  options: NativeRouteFromHAROptions,
  timeout: number,
): Promise<void> {
  const absolute = HARPathSchema.parse(path)
  const config = ReplayOptions.parse(options)
  if (config.update) {
    await source.runAutomation('har', {
      action: 'record',
      timeoutMs: timeout,
      options: {
        path: absolute,
        content: config.updateContent ?? 'attach',
        mode: config.updateMode ?? 'minimal',
        ...(config.url ? { urlFilter: config.url } : {}),
      },
    })
    return
  }
  if (config.updateContent !== undefined || config.updateMode !== undefined)
    throw new Error('HAR update options require update: true')
  await source.runAutomation('har', {
    action: 'replay',
    path: absolute,
    registrationId: randomUUID(),
    timeoutMs: timeout,
    notFound: config.notFound ?? 'abort',
    ...(config.url ? { url: config.url } : {}),
  })
}
