// SPDX-License-Identifier: MIT
import { type NativeArtifactRunner } from '../src/native-artifacts.js'
import { toError, type UntrustedInput } from '../src/untrusted.js'

export const png = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jk1sAAAAASUVORK5CYII=',
  'base64',
)
export function result(passed = true) {
  return {
    passed,
    stable: true,
    timedOut: !passed,
    attempts: 2,
    threshold: 0.2,
    dimensionsMatch: true,
    differentPixels: passed ? 0 : 1,
    totalPixels: 1,
    expectedWidth: 1,
    expectedHeight: 1,
    actualWidth: 1,
    actualHeight: 1,
    actual: png.toString('base64'),
    ...(passed ? {} : { diff: png.toString('base64') }),
  }
}
export class InputFixture {
  actions: Record<string, UntrustedInput>[] = []
  chunks: { section: string; offset: number; bytes: Buffer }[] = []
  finishWatch: ((value: UntrustedInput) => void) | undefined
  failWatch: ((error: Error) => void) | undefined
  constructor(
    readonly completion: () => Promise<UntrustedInput> = () => Promise.resolve(result()),
  ) {}
  readonly run: NativeArtifactRunner = async (params, signal) => {
    this.actions.push(params)
    signal?.throwIfAborted()
    if (params.action === 'comparePrepare')
      return { token: 'nativeScreenshotInput:1', contextId: 3, pageId: 7 }
    if (params.action === 'compareCommit') {
      this.finishWatch?.({ committed: true })
      return this.completion()
    }
    throw new Error('Unexpected native artifact command')
  }
  readonly source = {
    pageId: 7,
    runContext: (
      action: string,
      params: Record<string, UntrustedInput> = {},
      signal?: AbortSignal,
    ): Promise<UntrustedInput> => {
      if (action !== 'screenshotInput') throw new Error('Unexpected native context command')
      this.actions.push(params)
      signal?.throwIfAborted()
      if (params.action === 'watch')
        return new Promise((resolveWatch, rejectWatch) => {
          this.finishWatch = (value) => {
            signal?.removeEventListener('abort', abort)
            resolveWatch(value)
          }
          this.failWatch = (error) => {
            signal?.removeEventListener('abort', abort)
            rejectWatch(error)
          }
          const abort = () => {
            rejectWatch(toError(signal?.reason as UntrustedInput))
          }
          signal?.addEventListener('abort', abort, { once: true })
        })
      if (params.action === 'append') {
        if (
          typeof params.section !== 'string' ||
          typeof params.offset !== 'number' ||
          typeof params.data !== 'string'
        )
          throw new Error('Malformed chunk')
        this.chunks.push({
          section: params.section,
          offset: params.offset,
          bytes: Buffer.from(params.data, 'base64'),
        })
        return Promise.resolve({})
      }
      if (params.action === 'discard') return Promise.resolve({})
      throw new Error('Unexpected native screenshot input action')
    },
  }
  config() {
    return JSON.parse(
      Buffer.concat(
        this.chunks.filter((c) => c.section === 'config').map((c) => c.bytes),
      ).toString(),
    ) as UntrustedInput
  }
}
