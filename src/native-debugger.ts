// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { toError, type UntrustedInput } from './untrusted.js'

const offset = z.number().int().min(0).max(2_147_483_647)
const location = z.strictObject({
  scriptId: z.string().min(1).max(128),
  lineNumber: offset,
  columnNumber: offset.optional(),
})
const remote = z.strictObject({
  type: z.string().max(64),
  subtype: z.string().max(128).optional(),
  className: z.string().max(8192).optional(),
  description: z.string().max(8192).optional(),
  objectId: z.string().max(512).optional(),
})
const scope = z.strictObject({
  type: z.string().max(64),
  name: z.string().max(8192).optional(),
  object: remote,
  startLocation: location.optional(),
  endLocation: location.optional(),
})
const frame = z.strictObject({
  callFrameId: z.string().min(1).max(512),
  functionName: z.string().max(8192),
  url: z.string().max(8192).optional(),
  location,
  functionLocation: location.optional(),
  scopeChain: z.array(scope).max(64),
  this: remote.optional(),
})
const pauseResult = z.strictObject({
  session: z.string().min(1).max(256),
  pause: z.string().min(1).max(512),
  reason: z.string().max(1024),
  callFrames: z.array(frame).min(1).max(64),
  hitBreakpoints: z.array(z.string().max(512)).max(64),
})
const scriptMetadata = z.strictObject({
  scriptId: z.string().min(1).max(256),
  url: z.string().max(32768),
  hash: z.string().max(32768).optional(),
  scriptLanguage: z.string().max(32768).optional(),
  hasSourceURL: z.boolean().optional(),
  attestedNative: z.boolean(),
})
const scriptPage = z.strictObject({
  scripts: z.array(scriptMetadata).max(64),
  next: z.string().min(1).max(512).optional(),
})
const evaluationPrimitive = z.discriminatedUnion('type', [
  z.strictObject({ type: z.literal('undefined') }),
  z.strictObject({ type: z.literal('null') }),
  z.strictObject({ type: z.literal('string'), value: z.string().max(8192) }),
  z.strictObject({ type: z.literal('boolean'), value: z.boolean() }),
  z.strictObject({ type: z.literal('number'), value: z.number().finite() }),
  z.strictObject({
    type: z.literal('specialNumber'),
    value: z.enum(['NaN', '-0', 'Infinity', '-Infinity']),
  }),
  z.strictObject({
    type: z.literal('bigint'),
    value: z
      .string()
      .min(1)
      .max(1025)
      .regex(/^-?(?:0|[1-9][0-9]*)$/u),
  }),
])
const evaluationResult = z.discriminatedUnion('kind', [
  z.strictObject({ kind: z.literal('value'), value: evaluationPrimitive }),
  z.strictObject({
    kind: z.literal('exception'),
    exception: z.strictObject({
      text: z.string().max(4096),
      lineNumber: offset,
      columnNumber: offset,
      scriptId: z.string().max(128).optional(),
      description: z.string().max(4096).optional(),
      thrown: evaluationPrimitive.optional(),
    }),
  }),
])
const evaluationExpression = z
  .string()
  .min(1)
  .max(8192)
  .refine((text) => {
    for (let index = 0; index < text.length; ++index) {
      const unit = text.charCodeAt(index)
      if (unit >= 0xd800 && unit <= 0xdbff) {
        const next = text.charCodeAt(++index)
        if (!(next >= 0xdc00 && next <= 0xdfff)) return false
      } else if (unit >= 0xdc00 && unit <= 0xdfff) return false
    }
    return true
  }, 'Isolated expression source units require JavaScript Unicode escapes')
function projectEvaluationPrimitive(
  input: z.output<typeof evaluationPrimitive>,
): NativeDebuggerPrimitive {
  switch (input.type) {
    case 'undefined':
      return undefined
    case 'null':
      return null
    case 'string':
    case 'boolean':
    case 'number':
      return input.value
    case 'bigint':
      return BigInt(input.value)
    case 'specialNumber':
      switch (input.value) {
        case 'NaN':
          return Number.NaN
        case '-0':
          return -0
        case 'Infinity':
          return Number.POSITIVE_INFINITY
        case '-Infinity':
          return Number.NEGATIVE_INFINITY
      }
  }
}
const operationOptions = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})

export type NativeDebuggerPrimitive = undefined | null | string | boolean | number | bigint
export interface NativeDebuggerEvaluationException {
  readonly text: string
  readonly lineNumber: number
  readonly columnNumber: number
  readonly scriptId?: string
  readonly description?: string
  readonly thrown?: NativeDebuggerPrimitive
}
export type NativeDebuggerEvaluation =
  | { readonly kind: 'value'; readonly value: NativeDebuggerPrimitive }
  | { readonly kind: 'exception'; readonly exception: NativeDebuggerEvaluationException }
export type NativeDebuggerOptions = z.input<typeof operationOptions>
export type NativeDebuggerLocation = z.output<typeof location>
export type NativeDebuggerCallFrame = z.output<typeof frame>
export interface NativeDebuggerPause {
  readonly reason: string
  readonly callFrames: readonly NativeDebuggerCallFrame[]
  readonly hitBreakpoints: readonly string[]
}
export type NativeDebuggerScript = Readonly<z.output<typeof scriptMetadata>>
/** An opaque enumeration position owned by one native session/document/revision. */
export interface NativeDebuggerScriptCursor {
  readonly position: number
}
export interface NativeDebuggerScriptPage {
  readonly scripts: readonly NativeDebuggerScript[]
  readonly next?: NativeDebuggerScriptCursor
}
export interface NativeDebuggerBreakpoint {
  readonly location: Readonly<NativeDebuggerLocation>
}
export interface NativeDebuggerTransport {
  readonly signal: AbortSignal
  readonly cleanupOwner: object
  /** False for a captured frame realm: pause/step affect the entire Inspector agent. */
  readonly agentControls?: boolean
  cleanupDebugger(session: string): Promise<UntrustedInput>
  defaultTimeout(): number
  runDebugger(params: Record<string, UntrustedInput>, signal?: AbortSignal): Promise<UntrustedInput>
}

interface CleanupReservation {
  readonly owner: WeakRef<object>
  failure?: Error
}
// This is a bounded SDK cleanup ledger, separate from native physical ownership.
const cleanupReservations = new Set<CleanupReservation>()
function reserveCleanup(owner: object): CleanupReservation {
  const failures: Error[] = []
  for (const ticket of cleanupReservations) {
    const previous = ticket.owner.deref()
    if (ticket.failure && (!previous || previous === owner)) {
      if (previous === owner) failures.push(ticket.failure)
      cleanupReservations.delete(ticket)
    }
  }
  if (failures.length) throw new AggregateError(failures, 'Previous native debugger cleanup failed')
  if (cleanupReservations.size >= 16) throw new Error('Too many retained native debugger cleanups')
  const ticket = { owner: new WeakRef(owner) }
  cleanupReservations.add(ticket)
  return ticket
}

/** Explicit current-document Inspector control, separate from raw CDP and developer UI. */
export class NativeDebuggerSession {
  private readonly pauses = new WeakMap<NativeDebuggerPause, string>()
  private readonly frames = new WeakMap<NativeDebuggerCallFrame, { pause: string; frame: string }>()
  private readonly breakpoints = new WeakMap<NativeDebuggerBreakpoint, string>()
  private readonly scriptCursors = new WeakMap<NativeDebuggerScriptCursor, string>()
  private currentPause: string | undefined
  private closing = false
  private normalCloseRequested = false
  private normalClose: Promise<void> | undefined
  private canonicalClose: Promise<void> | undefined
  private readonly ownerAborted = () => {
    void this.beginCleanup()
  }

  private constructor(
    private readonly transport: NativeDebuggerTransport,
    private readonly session: string,
    private readonly reservation: CleanupReservation,
  ) {
    transport.signal.addEventListener('abort', this.ownerAborted, { once: true })
    if (transport.signal.aborted) void this.beginCleanup()
  }

  /** Supported controls of this successfully admitted native target session. */
  get capabilities(): Readonly<{
    pause: boolean
    step: boolean
    observedPause: true
    resume: true
    scriptInventory: true
    scriptSource: true
    breakpoints: true
    pausedEvaluation: true
  }> {
    return Object.freeze({
      pause: this.transport.agentControls !== false,
      step: this.transport.agentControls !== false,
      observedPause: true,
      resume: true,
      scriptInventory: true,
      scriptSource: true,
      breakpoints: true,
      pausedEvaluation: true,
    })
  }

  private released(): void {
    this.transport.signal.removeEventListener('abort', this.ownerAborted)
    cleanupReservations.delete(this.reservation)
  }

  private beginCleanup(): Promise<void> {
    if (this.canonicalClose) return this.canonicalClose
    this.closing = true
    this.currentPause = undefined
    // Defer one microtask so a synchronous owner abort inside normal close
    // admission can still join the exact operation being assigned by close().
    this.canonicalClose = Promise.resolve().then(async () => {
      if (this.normalCloseRequested && this.normalClose) {
        try {
          await this.normalClose
          return
        } catch {
          // The observer may have canceled before admission. Canonical cleanup
          // still owns the exact returned session and has no caller signal.
        }
      }
      z.strictObject({}).parse(await this.transport.cleanupDebugger(this.session))
    })
    void this.canonicalClose.then(
      () => this.released(),
      (error: UntrustedInput) => {
        this.reservation.failure = toError(error)
        this.transport.signal.removeEventListener('abort', this.ownerAborted)
      },
    )
    return this.canonicalClose
  }

  static async create(
    transport: NativeDebuggerTransport,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerSession> {
    const parsed = operationOptions.parse(options)
    transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    const reservation = reserveCleanup(transport.cleanupOwner)
    let session: NativeDebuggerSession | undefined
    try {
      const result = z
        .strictObject({ session: z.string().min(1).max(256) })
        .parse(
          await transport.runDebugger(
            { action: 'open', timeoutMs: parsed.timeout ?? transport.defaultTimeout() },
            parsed.signal ? AbortSignal.any([transport.signal, parsed.signal]) : transport.signal,
          ),
        )
      session = new NativeDebuggerSession(transport, result.session, reservation)
      if (transport.signal.aborted || parsed.signal?.aborted) {
        void session.beginCleanup()
        transport.signal.throwIfAborted()
        parsed.signal?.throwIfAborted()
      }
      return session
    } catch (error) {
      // Without a returned native token, pending-open cancellation and actual
      // receipts remain native-owned; there is no SDK cleanup command to send.
      if (!session) cleanupReservations.delete(reservation)
      throw error
    }
  }

  private run(
    action: string,
    options: NativeDebuggerOptions,
    extra: Record<string, UntrustedInput> = {},
  ): Promise<UntrustedInput> {
    const parsed = operationOptions.parse(options)
    this.transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    if (this.closing) throw new Error('Native debugger session is closing or closed')
    if (
      this.transport.agentControls === false &&
      (action === 'pause' || action === 'stepOver' || action === 'stepInto' || action === 'stepOut')
    )
      throw new Error(
        'Agent-wide pause and step controls are unsupported for native frame or worker debugger sessions',
      )
    return this.transport.runDebugger(
      {
        action,
        session: this.session,
        ...extra,
        timeoutMs: parsed.timeout ?? this.transport.defaultTimeout(),
      },
      parsed.signal
        ? AbortSignal.any([this.transport.signal, parsed.signal])
        : this.transport.signal,
    )
  }

  private projectPause(input: UntrustedInput): NativeDebuggerPause {
    const parsed = pauseResult.parse(input)
    if (parsed.session !== this.session)
      throw new Error('Native debugger pause belongs to another session')
    if (parsed.callFrames.reduce((count, item) => count + item.scopeChain.length, 0) > 64)
      throw new Error('Native debugger pause exceeds its scope metadata bound')
    const pause: NativeDebuggerPause = Object.freeze({
      reason: parsed.reason,
      callFrames: parsed.callFrames,
      hitBreakpoints: parsed.hitBreakpoints,
    })
    for (const frame of parsed.callFrames)
      this.frames.set(frame, { pause: parsed.pause, frame: frame.callFrameId })
    this.pauses.set(pause, parsed.pause)
    this.currentPause = parsed.pause
    return pause
  }

  /** A requested pause needs a real subsequent statement; timeout never implies execution stopped. */
  async pause(options: NativeDebuggerOptions = {}): Promise<NativeDebuggerPause> {
    return this.projectPause(await this.run('pause', options))
  }

  async waitForPause(options: NativeDebuggerOptions = {}): Promise<NativeDebuggerPause> {
    return this.projectPause(await this.run('waitForPause', options))
  }

  private continue(
    action: 'resume' | 'stepOver' | 'stepInto' | 'stepOut',
    pause: NativeDebuggerPause,
    options: NativeDebuggerOptions,
  ): Promise<UntrustedInput> {
    // Validate before consuming a genuine pause so a pre-aborted/invalid caller
    // cannot discard a usable token without any native admission.
    const parsed = operationOptions.parse(options)
    this.transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    if (this.transport.agentControls === false && action !== 'resume')
      throw new Error(
        'Agent-wide pause and step controls are unsupported for native frame or worker debugger sessions',
      )
    const token = this.pauses.get(pause)
    if (!token || token !== this.currentPause)
      throw new Error('Native debugger pause is stale or belongs to another session')
    this.currentPause = undefined
    return this.run(action, parsed, { pause: token })
  }

  async resume(pause: NativeDebuggerPause, options: NativeDebuggerOptions = {}): Promise<void> {
    z.strictObject({}).parse(await this.continue('resume', pause, options))
  }
  async stepOver(
    pause: NativeDebuggerPause,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerPause> {
    return this.projectPause(await this.continue('stepOver', pause, options))
  }
  async stepInto(
    pause: NativeDebuggerPause,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerPause> {
    return this.projectPause(await this.continue('stepInto', pause, options))
  }
  async stepOut(
    pause: NativeDebuggerPause,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerPause> {
    return this.projectPause(await this.continue('stepOut', pause, options))
  }

  /** Metadata from actual scriptParsed receipts. Changes require restarting with no cursor. */
  async scripts(
    cursor?: NativeDebuggerScriptCursor,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerScriptPage> {
    const parsed = operationOptions.parse(options)
    this.transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    const token = cursor === undefined ? undefined : this.scriptCursors.get(cursor)
    if (cursor !== undefined && !token)
      throw new Error('Native debugger script cursor belongs to another enumeration/session')
    const result = scriptPage.parse(
      await this.run('scripts', parsed, token ? { cursor: token } : {}),
    )
    const scripts = Object.freeze(result.scripts.map((item) => Object.freeze(item)))
    if (result.next === undefined) return Object.freeze({ scripts })
    const next = Object.freeze({ position: (cursor?.position ?? 0) + scripts.length })
    this.scriptCursors.set(next, result.next)
    return Object.freeze({ scripts, next })
  }

  async scriptSource(scriptId: string, options: NativeDebuggerOptions = {}): Promise<string> {
    const id = z.string().min(1).max(128).parse(scriptId)
    return z
      .strictObject({ source: z.string().max(262_144) })
      .parse(await this.run('scriptSource', options, { scriptId: id })).source
  }

  /** Evaluates direct source on one genuine paused frame; no remote result objects are adopted. */
  async evaluateOnCallFrame(
    pause: NativeDebuggerPause,
    frame: NativeDebuggerCallFrame,
    expression: string,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerEvaluation> {
    const parsed = operationOptions.parse(options),
      selected = evaluationExpression.parse(expression)
    this.transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    const pauseToken = this.pauses.get(pause),
      membership = this.frames.get(frame)
    if (!pauseToken || pauseToken !== this.currentPause || membership?.pause !== pauseToken)
      throw new Error(
        'Native paused evaluation requires this session’s current actual pause and frame',
      )
    const result = evaluationResult.parse(
      await this.run('evaluateOnCallFrame', parsed, {
        pause: pauseToken,
        callFrameId: membership.frame,
        expressionJSON: JSON.stringify(selected),
      }),
    )
    if (result.kind === 'value')
      return Object.freeze({ kind: 'value', value: projectEvaluationPrimitive(result.value) })
    const exception = result.exception
    return Object.freeze({
      kind: 'exception',
      exception: Object.freeze({
        text: exception.text,
        lineNumber: exception.lineNumber,
        columnNumber: exception.columnNumber,
        ...(exception.scriptId !== undefined ? { scriptId: exception.scriptId } : {}),
        ...(exception.description !== undefined ? { description: exception.description } : {}),
        ...(exception.thrown ? { thrown: projectEvaluationPrimitive(exception.thrown) } : {}),
      }),
    })
  }

  /** Sets an unconditional breakpoint in a genuinely observed current document author script. */
  async setBreakpoint(
    requested: NativeDebuggerLocation,
    options: NativeDebuggerOptions = {},
  ): Promise<NativeDebuggerBreakpoint> {
    const selected = location.parse(requested)
    const result = z
      .strictObject({ breakpoint: z.string().min(1).max(512), location })
      .parse(await this.run('setBreakpoint', options, { location: selected }))
    if (result.location.scriptId !== selected.scriptId)
      throw new Error('Native debugger breakpoint resolved into another script')
    const handle: NativeDebuggerBreakpoint = Object.freeze({
      location: Object.freeze(result.location),
    })
    this.breakpoints.set(handle, result.breakpoint)
    return handle
  }

  /** Removes only this session's genuine returned breakpoint, after its actual native ACK. */
  async removeBreakpoint(
    breakpoint: NativeDebuggerBreakpoint,
    options: NativeDebuggerOptions = {},
  ): Promise<void> {
    const parsed = operationOptions.parse(options)
    this.transport.signal.throwIfAborted()
    parsed.signal?.throwIfAborted()
    const token = this.breakpoints.get(breakpoint)
    if (!token) throw new Error('Native debugger breakpoint is stale or belongs to another session')
    const pending = this.run('removeBreakpoint', parsed, { breakpoint: token })
    this.breakpoints.delete(breakpoint)
    z.strictObject({}).parse(await pending)
  }

  /** Native canonical cleanup continues after this observer's timeout or abort. */
  async close(options: NativeDebuggerOptions = {}): Promise<void> {
    if (this.canonicalClose) return this.canonicalClose
    if (this.normalClose) return this.normalClose
    this.normalCloseRequested = true
    // run() can reject synchronously, including before any native admission.
    const admit = async () => {
      const parsed = operationOptions.parse(options)
      this.transport.signal.throwIfAborted()
      parsed.signal?.throwIfAborted()
      return this.transport.runDebugger(
        {
          action: 'close',
          session: this.session,
          timeoutMs: parsed.timeout ?? this.transport.defaultTimeout(),
        },
        parsed.signal
          ? AbortSignal.any([this.transport.signal, parsed.signal])
          : this.transport.signal,
      )
    }
    const pending = admit()
    this.closing = true
    this.currentPause = undefined
    this.normalClose = pending.then((result) => {
      z.strictObject({}).parse(result)
    })
    try {
      await this.normalClose
      this.closing = true
      this.currentPause = undefined
      this.released()
    } catch (error) {
      void this.beginCleanup()
      throw error
    }
  }
}
