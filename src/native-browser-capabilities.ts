// SPDX-License-Identifier: MIT
import * as z from 'zod'

/** Actual native adapter capabilities; isolated vendor source patches do not enable these. */
export const NativeBrowserCapabilitiesSchema = z.strictObject({
  schemaVersion: z.literal(1),
  backend: z.literal('cef'),
  directoryUpload: z.boolean(),
  nativeFileSelectionReset: z.boolean(),
  webSocketRouting: z.boolean(),
  fullClockControl: z.boolean(),
  coverageAcrossNavigation: z.boolean(),
  offscreenOopifFullPageCapture: z.boolean(),
  // Older schema-v1 helpers omitted these capabilities; absence cannot grant
  // backend authority and therefore resolves conservatively to false.
  clientCertificateImport: z.boolean().default(false),
  concurrentCredentialCeremonies: z.boolean().default(false),
  originalWidgetInputReceipts: z.boolean().default(false),
  frameSubscription: z.boolean().default(false),
})

export type NativeBrowserCapabilities = Readonly<z.infer<typeof NativeBrowserCapabilitiesSchema>>
