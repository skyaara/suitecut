// SPDX-License-Identifier: MIT
export interface NativeTestAttachmentOptions {
  readonly body?: string | Uint8Array
  readonly path?: string
  readonly contentType?: string
}
export type NativeTestAttach = (
  name: string,
  options?: NativeTestAttachmentOptions,
) => Promise<void>
