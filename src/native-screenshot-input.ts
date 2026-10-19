import { randomUUID } from 'node:crypto'
import { resolve } from 'node:path'

import * as z from 'zod'

import { type NativeArtifactRunner } from './native-artifacts.js'
import { type NativeBrowser } from './native-browser.js'
import {
  retainNativeRetryOperation,
  runNativeRetryCleanup,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { toError, type UntrustedInput } from './untrusted.js'

type InputTransport = Pick<NativeBrowser, 'pageId' | 'runContext'>
const chunkBytes = 24 * 1024
const owners = new Set<object>()
let retainedPayloadBytes = 0
const maxPayloadBytes = 64 * 1024 * 1024
interface CleanupFailure {
  source: WeakRef<InputTransport>
  error: Error
}
const retainedFailures = new Set<CleanupFailure>()
const cleanupFailures = new WeakMap<InputTransport, CleanupFailure[]>()
const TicketSchema = z.strictObject({
  token: z.string().min(1).max(128),
  contextId: z.number().int().positive(),
  pageId: z.number().int().positive(),
})
const WatchSchema = z.strictObject({ committed: z.literal(true) })

// Encode only native-produced screenshot configuration. Bound output before
// allocation; author argument objects and their toJSON hooks are not accepted.
function configurationEncoding(value: Record<string, UntrustedInput>, limit: number) {
  const output: { storage?: Buffer } = {}
  const ancestors = new Set<object>()
  let used = 0,
    work = 0
  const append = (text: string) => {
    const bytes = Buffer.byteLength(text)
    if (bytes > (output.storage?.length ?? limit) - used)
      throw new Error(`Native artifact configuration exceeds${limit / 1048576}MiB`)
    if (output.storage) used += output.storage.write(text, used, bytes, 'utf8')
    else used += bytes
  }
  const string = (text: string) => {
    if (text.length > limit - used - 2)
      throw new Error(`Native artifact configuration exceeds${limit / 1048576}MiB`)
    let bytes = 2
    for (let i = 0; i < text.length; ++i) {
      const c = text.charCodeAt(i)
      if (c === 34 || c === 92 || c === 8 || c === 9 || c === 10 || c === 12 || c === 13) bytes += 2
      else if (c < 32) bytes += 6
      else if (c < 128) ++bytes
      else if (c < 2048) bytes += 2
      else if (c >= 0xd800 && c <= 0xdbff) {
        const next = text.charCodeAt(i + 1)
        if (next >= 0xdc00 && next <= 0xdfff) {
          bytes += 4
          ++i
        } else bytes += 6
      } else bytes += c >= 0xdc00 && c <= 0xdfff ? 6 : 3
      if (bytes > limit - used)
        throw new Error(`Native artifact configuration exceeds${limit / 1048576}MiB`)
    }
    if (output.storage) append(JSON.stringify(text))
    else used += bytes
  }
  const encode = (item: UntrustedInput, depth: number) => {
    if (++work > 100000 || depth > 256)
      throw new Error('Native screenshot configuration nesting/work limit exceeded')
    if (item === null) {
      append('null')
      return
    }
    if (typeof item === 'string') {
      string(item)
      return
    }
    if (typeof item === 'boolean' || (typeof item === 'number' && Number.isFinite(item))) {
      append(JSON.stringify(item))
      return
    }
    if (typeof item !== 'object' || ancestors.has(item))
      throw new Error('Invalid or cyclic native screenshot configuration')
    ancestors.add(item)
    if (Array.isArray(item)) {
      const values = item as UntrustedInput[]
      append('[')
      for (let i = 0; i < values.length; ++i) {
        if (i) append(',')
        encode(values[i] === undefined ? null : values[i], depth + 1)
      }
      append(']')
    } else {
      const prototype: UntrustedInput = Object.getPrototypeOf(item) as UntrustedInput
      if (prototype !== null && prototype !== Object.prototype)
        throw new Error('Native screenshot configuration must contain plain protocol objects')
      append('{')
      let first = true
      for (const key of Object.keys(item)) {
        const field = (item as Record<string, UntrustedInput>)[key]
        if (field === undefined) continue
        if (!first) append(',')
        first = false
        string(key)
        append(':')
        encode(field, depth + 1)
      }
      append('}')
    }
    ancestors.delete(item)
  }
  encode(value, 0)
  const length = used
  return {
    length,
    write(reserve: (bytes: number) => void) {
      reserve(length)
      output.storage = Buffer.allocUnsafe(length)
      used = 0
      work = 0
      encode(value, 0)
      if (used !== length) throw new Error('Native artifact configuration changed during encoding')
      return output.storage
    },
  }
}

/**
 * Native prepare owns the deadline and all file I/O. Chunk commands only
 * transport bounded bytes; their zero timeout cannot alter that native lease.
 */
export function runNativeScreenshotInput(
  run: NativeArtifactRunner,
  source: InputTransport,
  baseline: string | Buffer,
  config: Record<string, UntrustedInput>,
  timeout: number,
  signal?: AbortSignal,
): Promise<UntrustedInput> {
  return runNativeRetryOperation(() => {
    return runNativePreparedInput(run, source, baseline, config, timeout, signal)
  })
}

/** Upload typed screenshot/PDF configuration under its native purpose and clock. */
export async function runNativeArtifactConfiguration(
  run: NativeArtifactRunner,
  source: InputTransport | undefined,
  config: Record<string, UntrustedInput>,
  timeout?: number,
  signal?: AbortSignal,
): Promise<UntrustedInput> {
  return runNativeRetryOperation(async () => {
    signal?.throwIfAborted()
    const kind = withoutNativeRetryOperation(() =>
      z.enum(['screenshot', 'pdf']).parse(config.action),
    )
    const encoding = configurationEncoding(config, 16 * 1024 * 1024)
    if (encoding.length <= 16 * 1024) {
      const params = { ...config, timeoutMs: timeout }
      return kind === 'pdf' && signal === undefined ? run(params) : run(params, signal)
    }
    if (!source)
      throw new Error('Large native artifact configuration requires its actual page transport')
    return runNativePreparedInput(run, source, undefined, config, timeout, signal, kind, encoding)
  })
}

async function runNativePreparedInput(
  run: NativeArtifactRunner,
  source: InputTransport,
  baseline: string | Buffer | undefined,
  config: Record<string, UntrustedInput>,
  timeout: number | undefined,
  signal?: AbortSignal,
  kind?: 'screenshot' | 'pdf',
  preparedEncoding?: ReturnType<typeof configurationEncoding>,
): Promise<UntrustedInput> {
  signal?.throwIfAborted()
  if (!source.runContext || !source.pageId || source.pageId < 1 || !Number.isInteger(source.pageId))
    throw new Error('Native screenshot input requires its actual page transport')
  if (Buffer.isBuffer(baseline) && (baseline.length < 33 || baseline.length > 2 * 1024 * 1024))
    throw new Error('Native screenshot PNG input must be between33bytes and2MiB')
  const control = source.runContext.bind(source)
  for (const failure of retainedFailures)
    if (!failure.source.deref()) retainedFailures.delete(failure)
  const previous = cleanupFailures.get(source)
  if (previous?.length) {
    cleanupFailures.delete(source)
    for (const failure of previous) retainedFailures.delete(failure)
    throw new AggregateError(
      previous.map((failure) => failure.error),
      'Earlier native screenshot input cleanup failed',
    )
  }
  if (owners.size + retainedFailures.size >= 16)
    throw new Error('Native screenshot input SDK staging capacity exceeded')
  const admission = {}
  owners.add(admission)
  let payloadBytes = 0
  const reservePayload = (configBytes: number) => {
    const bytes = 3 * configBytes + (Buffer.isBuffer(baseline) ? baseline.length : 0)
    if (bytes > maxPayloadBytes - retainedPayloadBytes)
      throw new Error('Native artifact SDK64MiB staging byte budget exceeded')
    retainedPayloadBytes += bytes
    payloadBytes = bytes
  }
  const inputAbort = new AbortController()
  const operationSignal = signal ? AbortSignal.any([signal, inputAbort.signal]) : inputAbort.signal
  let settled = false,
    comparisonCommit = false
  let resolvePublic!: (value: UntrustedInput) => void
  let rejectPublic!: (error: UntrustedInput) => void
  const publicResult = new Promise<UntrustedInput>((resolve, reject) => {
    resolvePublic = resolve
    rejectPublic = reject
  })
  const failPublic = (error: UntrustedInput): void => {
    if (settled) return
    settled = true
    operationSignal.removeEventListener('abort', aborted)
    rejectPublic(error)
  }
  const aborted = (): void => {
    // Once the native comparison owns the producer, its actual retained reply
    // carries physical retirement. Its Source cancellation still happens
    // immediately through the real transport signal; publication waits for ACK.
    if (!comparisonCommit) failPublic(operationSignal.reason as UntrustedInput)
  }
  operationSignal.addEventListener('abort', aborted, { once: true })
  // The task, not the public promise, owns its slot through the real discard
  // receipt. Caller failure never detaches an unbounded sequence of cleanups.
  const rememberCleanup = (error: Error): void => {
    const failures = cleanupFailures.get(source) ?? []
    const failure = { source: new WeakRef(source), error }
    failures.push(failure)
    retainedFailures.add(failure)
    cleanupFailures.set(source, failures)
  }
  const task = (async (): Promise<void> => {
    let baselineInput: string | Buffer | undefined
    const clientToken = randomUUID()
    const pageId = source.pageId
    const watchAbort = new AbortController()
    let watch: Promise<void> | undefined
    let failure: Error | undefined
    let result: UntrustedInput
    let prepared = false
    try {
      const encoding =
        preparedEncoding ??
        configurationEncoding(
          {
            ...config,
            ...(typeof baseline === 'string' ? { baselinePath: resolve(baseline) } : {}),
          },
          kind ? 16 * 1024 * 1024 : 1024 * 1024,
        )
      const body = encoding.write(reservePayload)
      baselineInput = Buffer.isBuffer(baseline) ? Buffer.from(baseline) : baseline
      // Mark before admission: cleanup also covers a real prepared lease whose
      // reply was lost to caller cancellation before the SDK learned its token.
      prepared = true
      const ticket = TicketSchema.parse(
        await run(
          {
            action: kind ? 'capturePrepare' : 'comparePrepare',
            clientToken,
            ...(kind
              ? { kind }
              : {
                  file: typeof baselineInput === 'string',
                  pngBytes: Buffer.isBuffer(baselineInput) ? baselineInput.length : 0,
                }),
            configBytes: body.length,
            timeoutMs: timeout,
          },
          operationSignal,
        ),
      )
      if (ticket.pageId !== pageId)
        throw new Error('Native screenshot input returned a different page owner')
      const identity = { pageId, contextId: ticket.contextId, token: ticket.token, timeoutMs: 0 }
      watch = control('screenshotInput', { ...identity, action: 'watch' }, watchAbort.signal)
        .then((value) => {
          withoutNativeRetryOperation(() => WatchSchema.parse(value))
        })
        .catch((error: UntrustedInput) => {
          if (!watchAbort.signal.aborted) inputAbort.abort(error)
        })
      const upload = async (section: 'config' | 'png', bytes: Buffer) => {
        for (let offset = 0; offset < bytes.length; offset += chunkBytes) {
          operationSignal.throwIfAborted()
          await control(
            'screenshotInput',
            {
              ...identity,
              action: 'append',
              section,
              offset,
              data: bytes.subarray(offset, offset + chunkBytes).toString('base64'),
            },
            operationSignal,
          )
        }
      }
      await upload('config', body)
      if (Buffer.isBuffer(baselineInput)) await upload('png', baselineInput)
      operationSignal.throwIfAborted()
      comparisonCommit = kind === undefined
      result = await run(
        {
          action: kind ? 'captureCommit' : 'compareCommit',
          ...(kind ? { kind } : {}),
          inputToken: ticket.token,
          timeoutMs: 0,
        },
        operationSignal,
      )
      operationSignal.throwIfAborted()
    } catch (error) {
      failure = toError(error as UntrustedInput)
      if (!comparisonCommit)
        failPublic(operationSignal.aborted ? (operationSignal.reason as UntrustedInput) : failure)
    }
    watchAbort.abort()
    await watch
    try {
      if (prepared)
        await runNativeRetryCleanup(() =>
          control('screenshotInput', { action: 'discard', pageId, clientToken, timeoutMs: 0 }),
        )
    } catch (cleanup) {
      const error = failure
        ? new AggregateError(
            [failure, cleanup],
            'Native screenshot input failed and native cleanup was rejected',
            { cause: cleanup },
          )
        : toError(cleanup as UntrustedInput)
      if (settled) rememberCleanup(error)
      else if (comparisonCommit && operationSignal.aborted) {
        rememberCleanup(error)
        failPublic(operationSignal.reason as UntrustedInput)
      } else failPublic(error)
      return
    }
    if (!settled && failure) {
      failPublic(operationSignal.aborted ? (operationSignal.reason as UntrustedInput) : failure)
      return
    }
    if (!settled) {
      settled = true
      operationSignal.removeEventListener('abort', aborted)
      resolvePublic(result)
    }
  })()
  retainNativeRetryOperation(task)
  void task.then(
    () => {
      owners.delete(admission)
      retainedPayloadBytes -= payloadBytes
    },
    (error: UntrustedInput) => {
      owners.delete(admission)
      retainedPayloadBytes -= payloadBytes
      if (settled) rememberCleanup(toError(error))
      else failPublic(error)
    },
  )
  if (operationSignal.aborted) aborted()
  return publicResult
}
