// SPDX-License-Identifier: MIT
// Callback execution and IPC only. The native executable owns test scheduling.
import { Buffer } from 'node:buffer'
import { once } from 'node:events'
import { fstatSync } from 'node:fs'
import { Socket } from 'node:net'
import process from 'node:process'
import { createInterface } from 'node:readline'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

import { installNativeCallbackAssertionBridge } from './native-callback-assertions.js'
import { NativeFixtureDeclarationSchema } from './native-fixture-declarations.js'
import { isNativeFixtureLayers, type NativeFixtureLayers } from './native-fixture-layers.js'
import { nativeFixtureLocation } from './native-fixture-location.js'
import { nativeFixtureLiteralWire, type NativeFixtureLiteral } from './native-fixture-values.js'
import { NativeAttachmentCallbacks } from './native-runner-attachments.js'
import { NativeCallbackRetryCallbacks } from './native-runner-callback-retries.js'
import { NativeFixtureBindings } from './native-runner-fixture-bindings.js'
import { NativeFixtureTimeoutCallbacks } from './native-runner-fixture-timeouts.js'
import { NativeWorkerIdleCallbacks } from './native-runner-idle.js'
import { nativeModifierLocation } from './native-runner-location.js'
import { NativeOutputCallbacks } from './native-runner-output.js'
import { NativeRepeatCallbacks, nativeRepeatCount } from './native-runner-repeat.js'
import {
  NativeSnapshotCallbacks,
  NativeSnapshotConfigurationSchema,
} from './native-runner-snapshot-paths.js'
import { NativeSoftAssertionCallbacks } from './native-runner-soft-assertions.js'
import { NativeSourceSetupProtocol } from './native-runner-source-protocol.js'
import { type NativeStepOwner } from './native-runner-steps.js'
import { NativeStepCallbacks } from './native-runner-steps.js'
import { installNativeSoftAssertionBridge } from './native-soft-assertions.js'
import {
  type NativeFixtureDefinition,
  type NativeFixtureInfo,
  type NativeHookDefinition,
  type NativeHookInfo,
  type NativeTestDefinition,
  type NativeTestInfo,
  type NativeTestAnnotation,
  type NativeProjectInfo,
} from './native-suite.js'
import { toError, type UntrustedInput } from './untrusted.js'

const Name = z.string().regex(/^[A-Za-z_][A-Za-z0-9_]{0,255}$/u)
const Fixture = NativeFixtureDeclarationSchema
const FixtureOption = z.custom<NativeFixtureLiteral>((value) => {
  nativeFixtureLiteralWire(value as UntrustedInput)
  return true
})
const Lock = z.union([z.string().max(1024), z.array(z.string().max(1024)).max(64)])
const Tag = z.union([
  z.string().startsWith('@').max(256),
  z.array(z.string().startsWith('@').max(256)).max(64),
])
const Test = z.strictObject({
  lock: Lock.optional(),
  title: z.string().max(4096),
  tag: Tag.optional(),
  fixtures: z.array(Name).max(256).optional(),
  retries: z.number().int().min(0).max(100).optional(),
  timeout: z.number().int().min(0).max(120000).optional(),
  expectedFailure: z.boolean().optional(),
  skip: z.union([z.boolean(), z.string().max(8192)]).optional(),
  only: z.boolean().optional(),
  run: z.custom<NativeTestDefinition['run']>((value) => typeof value === 'function'),
})
const Hook = z.strictObject({
  fixtures: z.array(Name).max(256).optional(),
  timeout: z.number().int().min(0).max(120000).optional(),
  run: z.custom<NativeHookDefinition['run']>((value) => typeof value === 'function'),
})
const HookKind = z.enum(['beforeAll', 'beforeEach', 'afterEach', 'afterAll'])
const Hooks = z.strictObject({
  beforeAll: z.array(Hook).max(256).optional(),
  beforeEach: z.array(Hook).max(256).optional(),
  afterEach: z.array(Hook).max(256).optional(),
  afterAll: z.array(Hook).max(256).optional(),
})
type ParsedTest = z.infer<typeof Test>
type ParsedHooks = z.infer<typeof Hooks>
interface ParsedSuite {
  title: string
  tag?: string | string[] | undefined
  lock?: string | string[] | undefined
  mode?: 'default' | 'serial' | 'parallel' | undefined
  skip?: boolean | string | undefined
  only?: boolean | undefined
  hooks?: ParsedHooks | undefined
  entries: (ParsedTest | ParsedSuite)[]
}
const NestedSuite: z.ZodType<ParsedSuite> = z.lazy(() =>
  z.strictObject({
    lock: Lock.optional(),
    title: z.string().max(4096),
    tag: Tag.optional(),
    mode: z.enum(['default', 'serial', 'parallel']).optional(),
    skip: z.union([z.boolean(), z.string().max(8192)]).optional(),
    only: z.boolean().optional(),
    hooks: Hooks.optional(),
    entries: z.array(z.union([Test, NestedSuite])).max(4096),
  }),
)
const ProjectName = z
  .string()
  .min(1)
  .max(256)
  .refine(
    (value) =>
      !value.includes('\0') &&
      Buffer.byteLength(value) <= 256 &&
      Buffer.from(value).toString('utf8') === value,
  )
const ProjectMetadata = z
  .record(z.string(), z.json())
  .refine((value) => Buffer.byteLength(JSON.stringify(value)) <= 16384)
const Grep = z.union([z.instanceof(RegExp), z.array(z.instanceof(RegExp)).max(128)])
const ProjectOutputDir = z
  .string()
  .max(7900)
  .refine(
    (value) =>
      !value.includes('\0') &&
      Buffer.byteLength(value) <= 7900 &&
      Buffer.from(value).toString('utf8') === value,
  )
const Project = z
  .strictObject({
    repeatEach: nativeRepeatCount.optional(),
    outputDir: ProjectOutputDir.optional(),
    ...NativeSnapshotConfigurationSchema.shape,
    timeout: z.number().int().min(0).max(120000).optional(),
    grep: Grep.optional(),
    grepInvert: Grep.optional(),
    fullyParallel: z.boolean().optional(),
    name: ProjectName,
    dependencies: z.array(ProjectName).max(128).optional(),
    teardown: ProjectName.optional(),
    default: z.boolean().optional(),
    metadata: ProjectMetadata.optional(),
    hooks: Hooks.optional(),
    tests: z.array(Test).max(4096).optional(),
    entries: z
      .array(z.union([Test, NestedSuite]))
      .max(4096)
      .optional(),
  })
  .refine(
    (value) => (value.tests === undefined) !== (value.entries === undefined),
    'Declare project tests or entries',
  )
const Definition = z
  .strictObject({
    repeatEach: nativeRepeatCount.optional(),
    ...NativeSnapshotConfigurationSchema.shape,
    timeout: z.number().int().min(0).max(120000).optional(),
    tag: Tag.optional(),
    grep: Grep.optional(),
    grepInvert: Grep.optional(),
    fullyParallel: z.boolean().optional(),
    lock: Lock.optional(),
    fixtures: z
      .union([
        z.record(Name, Fixture),
        z.custom<NativeFixtureLayers>((value) => isNativeFixtureLayers(value as UntrustedInput)),
      ])
      .optional(),
    fixtureOptions: z.record(Name, FixtureOption).optional(),
    projects: z.array(Project).min(1).max(128).optional(),
    hooks: Hooks.optional(),
    tests: z.array(Test).max(4096).optional(),
    entries: z
      .array(z.union([Test, NestedSuite]))
      .max(4096)
      .optional(),
  })
  .refine(
    (value) =>
      [value.tests, value.entries, value.projects].filter((value) => value !== undefined).length ===
      1,
    'Declare exactly one of tests, ordered entries or projects',
  )

const modulePath = z.string().min(1).parse(process.argv[2]),
  workerIndex = z.coerce.number().int().nonnegative().parse(process.argv[3])
const channel = new Socket({ fd: 3, readable: true, writable: true }),
  incoming = createInterface({ input: channel })
const hex = (text: string) => Buffer.from(text, 'utf8').toString('hex') || '-'
const jsonHex = (value: string) => hex(JSON.stringify(value))
const unhex = (text: string) => (text === '-' ? '' : Buffer.from(text, 'hex').toString('utf8'))
const send = async (...fields: (string | number)[]): Promise<void> => {
  const line = fields.join(' ')
  if (Buffer.byteLength(line) > 65536) throw new Error('Native runner control packet exceeds64KiB')
  if (!channel.write(line + '\n')) await once(channel, 'drain')
}
const describe = (error: UntrustedInput): string => {
  try {
    return (toError(error).stack ?? toError(error).message).slice(0, 8192)
  } catch {
    return 'Thrown value could not be represented'
  }
}
interface Lease {
  token: string
  owner: string
  generation: string
  name: string
  used: boolean
  canceled: boolean
  valueKey: string
  release?: () => void
}
interface Body {
  owner: string
  generation: string
  abort: AbortController
  raw: Promise<void>
  timeout: number
  amendments: Set<Promise<void>>
  amendmentError?: Error
  acceptsAmendments: boolean
  skipControl?: Error
  expectedStatus: NativeTestInfo['expectedStatus']
  annotations: readonly NativeTestAnnotation[]
  skipAccepted?: boolean
  skipRequest?: string
  skipAck?: Promise<void>
}
const leases = new Map<string, Lease>(),
  values = new Map<string, UntrustedInput>(),
  valueOwners = new Map<string, string>(),
  bodies = new Map<string, Body>(),
  callbacks = new Map<string, Body>()
interface MutationRequest {
  kind: 'timeout' | 'skip' | 'failure'
  token: string
  owner: string
  generation: string
  callback: Body
  resolve: () => void
  reject: (error: Error) => void
}
const ExpectedStatus = z.enum(['passed', 'failed', 'skipped'])
const annotationWire = z
  .string()
  .max(32768)
  .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
const parseAnnotation = (fields: readonly string[]): NativeTestAnnotation => {
  const [type, present, reason, file, line, column] = z
    .tuple([
      z.enum(['skip', 'fail']),
      z.enum(['0', '1']),
      annotationWire,
      annotationWire,
      z.coerce.number().int().min(0).max(2147483647),
      z.coerce.number().int().min(0).max(2147483647),
    ])
    .parse(fields)
  const decode = (value: string): string =>
    value === '-' ? '' : Buffer.from(value, 'hex').toString('utf16le')
  const description = z.string().max(8192).parse(decode(reason))
  if (present === '0' && reason !== '-')
    throw new Error('Invalid native annotation reason presence')
  if ((file === '-') !== (line === 0 && column === 0) || (file !== '-' && (!line || !column)))
    throw new Error('Invalid native annotation location')
  return Object.freeze({
    type,
    ...(present === '1' ? { description } : {}),
    ...(file === '-'
      ? {}
      : {
          location: Object.freeze({ file: z.string().max(4096).parse(decode(file)), line, column }),
        }),
  })
}
const parseAdmissionMetadata = (
  fields: readonly string[],
  offset: number,
): {
  expectedStatus: NativeTestInfo['expectedStatus']
  annotations: readonly NativeTestAnnotation[]
} => {
  const expectedStatus = ExpectedStatus.parse(fields[offset])
  const count = z.coerce
    .number()
    .int()
    .min(0)
    .max(256)
    .parse(fields[offset + 1])
  if (fields.length !== offset + 2 + count * 6)
    throw new Error('Invalid native callback annotation snapshot')
  return {
    expectedStatus,
    annotations: Object.freeze(
      Array.from({ length: count }, (_, index) =>
        parseAnnotation(fields.slice(offset + 2 + index * 6, offset + 8 + index * 6)),
      ),
    ),
  }
}
const modifierLocationWire = (): readonly [string, number, number] => {
  const location = nativeModifierLocation()
  return location
    ? [Buffer.from(location.file, 'utf16le').toString('hex'), location.line, location.column]
    : ['-', 0, 0]
}
const mutationRequests = new Map<string, MutationRequest>()
let mutationNonce = 0n
const amendTimeout = (callback: Body, token: string, timeout: number): Promise<void> => {
  const actual = z.number().int().min(0).max(120000).parse(timeout)
  if (!callback.acceptsAmendments || callback.abort.signal.aborted || closing)
    throw new Error('Native live timeout callback is no longer active')
  if (mutationRequests.size >= 128 || mutationNonce === 18446744073709551615n)
    throw new Error('Native live timeout amendment capacity exceeded')
  const request = String(++mutationNonce)
  const promise = new Promise<void>((resolve, reject) => {
    mutationRequests.set(request, {
      kind: 'timeout',
      token,
      owner: callback.owner,
      generation: callback.generation,
      callback,
      resolve,
      reject,
    })
  })
  callback.amendments.add(promise)
  // Retain genuine native rejection for callback completion even if the author ignores this return value.
  void promise.then(
    () => callback.amendments.delete(promise),
    (error: UntrustedInput) => {
      callback.amendmentError ??= toError(error)
      callback.amendments.delete(promise)
    },
  )
  void send('SET_TIMEOUT', request, token, callback.owner, callback.generation, actual).catch(
    (error: UntrustedInput) => {
      mutationRequests.get(request)?.reject(toError(error))
    },
  )
  return promise
}
const callbackSkip = (
  callback: Body,
  token: string,
  kind: 'body' | z.infer<typeof HookKind>,
): NativeTestInfo['skip'] => {
  function skip(): never
  function skip(reason: string): never
  function skip(condition: boolean, reason?: string): void
  function skip(conditionOrReason: boolean | string = true, reason?: string): void {
    const condition = z.union([z.boolean(), z.string().max(8192)]).parse(conditionOrReason)
    const actualReason = z
      .string()
      .max(8192)
      .optional()
      .parse(typeof condition === 'string' ? condition : reason)
    if (condition === false) return
    if (kind !== 'body' && kind !== 'beforeEach' && kind !== 'beforeAll')
      throw new Error('Native runtime skip supports test bodies, beforeEach and beforeAll hooks')
    if (!callback.acceptsAmendments || callback.abort.signal.aborted || closing)
      throw new Error('Native runtime skip callback is no longer active')
    if (mutationRequests.size >= 128 || mutationNonce === 18446744073709551615n)
      throw new Error('Native callback mutation capacity exceeded')
    const request = String(++mutationNonce)
    const promise = new Promise<void>((resolve, reject) => {
      mutationRequests.set(request, {
        kind: 'skip',
        token,
        owner: callback.owner,
        generation: callback.generation,
        callback,
        resolve,
        reject,
      })
    })
    callback.amendments.add(promise)
    void promise.then(
      () => callback.amendments.delete(promise),
      (error: UntrustedInput) => {
        callback.amendmentError ??= toError(error)
        callback.amendments.delete(promise)
      },
    )
    callback.skipControl = new Error('Native runtime skip requested')
    callback.skipRequest = request
    callback.skipAck = promise
    callback.acceptsAmendments = false
    void send(
      'SKIP_REQUEST',
      request,
      token,
      callback.owner,
      callback.generation,
      Buffer.from(actualReason ?? '', 'utf16le').toString('hex') || '-',
      actualReason === undefined ? 0 : 1,
      ...modifierLocationWire(),
    ).catch((error: UntrustedInput) => {
      mutationRequests.get(request)?.reject(toError(error))
    })
    throw callback.skipControl
  }
  return skip
}
const callbackFail =
  (
    callback: Body,
    token: string,
    kind: 'body' | z.infer<typeof HookKind>,
  ): NativeTestInfo['fail'] =>
  (conditionOrReason = true, reason) => {
    const condition = z.union([z.boolean(), z.string().max(8192)]).parse(conditionOrReason)
    const actualReason = z
      .string()
      .max(8192)
      .optional()
      .parse(typeof condition === 'string' ? condition : reason)
    if (condition === false) return Promise.resolve()
    if (kind !== 'body' && kind !== 'beforeEach')
      throw new Error('Native dynamic expected failure supports test bodies and beforeEach hooks')
    if (!callback.acceptsAmendments || callback.abort.signal.aborted || closing)
      throw new Error('Native expected failure callback is no longer active')
    if (mutationRequests.size >= 128 || mutationNonce === 18446744073709551615n)
      throw new Error('Native callback mutation capacity exceeded')
    const request = String(++mutationNonce)
    const promise = new Promise<void>((resolve, reject) => {
      mutationRequests.set(request, {
        kind: 'failure',
        token,
        owner: callback.owner,
        generation: callback.generation,
        callback,
        resolve,
        reject,
      })
    })
    callback.amendments.add(promise)
    void promise.then(
      () => callback.amendments.delete(promise),
      (error: UntrustedInput) => {
        callback.amendmentError ??= toError(error)
        callback.amendments.delete(promise)
      },
    )
    void send(
      'FAIL_REQUEST',
      request,
      token,
      callback.owner,
      callback.generation,
      Buffer.from(actualReason ?? '', 'utf16le').toString('hex') || '-',
      actualReason === undefined ? 0 : 1,
      ...modifierLocationWire(),
    ).catch((error: UntrustedInput) => {
      mutationRequests.get(request)?.reject(toError(error))
    })
    return promise
  }
let idleCallbacks: NativeWorkerIdleCallbacks | undefined
let closing = false
const maybeClose = (): void => {
  idleCallbacks?.settled()
  if (
    closing &&
    (sourceProtocol?.idleReady ?? true) &&
    fixtureTimeoutCallbacks.idleReady &&
    fixtureTimeoutCallbacks.physical === 0 &&
    retryCallbacks.idleReady &&
    softCallbacks.idleReady &&
    attachmentCallbacks.physical === 0 &&
    attachmentCallbacks.pendingMutationRequests === 0 &&
    leases.size === 0 &&
    bodies.size === 0 &&
    callbacks.size === 0 &&
    mutationRequests.size === 0 &&
    stepCallbacks.pending === 0
  ) {
    incoming.close()
    channel.end(() => channel.destroy())
  }
}
const sourceProtocol =
  process.argv[4] === '--suitecut-source-endpoint=4'
    ? new NativeSourceSetupProtocol(
        send,
        maybeClose,
        process.argv[5] === '--suitecut-source-work=2',
      )
    : undefined
if (sourceProtocol && !fstatSync(4).isSocket())
  throw new Error('Native Source host endpoint is not inherited')
const outputCallbacks = new NativeOutputCallbacks(send)
const attachmentCallbacks: NativeAttachmentCallbacks = new NativeAttachmentCallbacks(
  send,
  (error) => {
    process.exitCode = 1
    void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
  },
  maybeClose,
  { workerIndex, pid: process.pid },
  (owner) => stepCallbacks.current(owner),
)
const stepCallbacks: NativeStepCallbacks = new NativeStepCallbacks(
  send,
  (error) => {
    process.exitCode = 1
    void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
  },
  maybeClose,
  {
    acceptedControl: async (owner, request) => {
      const callback = owner.token === '0' ? bodies.get(owner.scope) : callbacks.get(owner.token)
      if (
        callback?.owner !== owner.scope ||
        callback.generation !== owner.generation ||
        callback.skipRequest !== request ||
        !callback.skipControl ||
        !callback.skipAck
      )
        throw new Error('Native test-skip step cause has no actual callback mutation')
      await callback.skipAck
      if (!callback.skipAccepted) throw new Error('Native test-skip mutation was not accepted')
      return callback.skipControl
    },
    caughtControl: async (owner, error) => {
      const callback = owner.token === '0' ? bodies.get(owner.scope) : callbacks.get(owner.token)
      if (
        callback?.owner !== owner.scope ||
        callback.generation !== owner.generation ||
        error !== callback.skipControl
      )
        return undefined
      try {
        await callback.skipAck
      } catch {
        return undefined
      }
      if (!callback.skipAccepted || !callback.skipRequest) return undefined
      return callback.skipRequest
    },
  },
  (owner, id, active) => attachmentCallbacks.bindStep(owner, id, active),
  (owner, request, source, ids, control) =>
    attachmentCallbacks.acceptLocalSkip(owner, request, source, ids, control),
  (owner, active, callback) => runAuthorCallback(owner, active, callback),
)
const fixtureTimeoutCallbacks = new NativeFixtureTimeoutCallbacks(
  send,
  (error) => {
    process.exitCode = 1
    void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
  },
  maybeClose,
)
const softCallbacks = new NativeSoftAssertionCallbacks(
  send,
  (error) => {
    process.exitCode = 1
    void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
  },
  maybeClose,
  describe,
  (owner) => stepCallbacks.current(owner),
  { workerIndex, pid: process.pid },
)
const retryCallbacks = new NativeCallbackRetryCallbacks(
  send,
  (error) => {
    process.exitCode = 1
    void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
  },
  maybeClose,
  describe,
  stepCallbacks,
  { workerIndex, pid: process.pid },
  async (owner) => {
    const callback = owner.token === '0' ? bodies.get(owner.scope) : callbacks.get(owner.token)
    if (
      callback?.owner !== owner.scope ||
      callback.generation !== owner.generation ||
      !callback.skipControl ||
      !callback.skipAck
    )
      return undefined
    await callback.skipAck
    return callback.skipAccepted ? callback.skipControl : undefined
  },
)
installNativeCallbackAssertionBridge((callback, options, location) =>
  retryCallbacks.begin(callback, options, location),
)
installNativeSoftAssertionBridge((location) => softCallbacks.begin(location))
function runAuthorCallback<T>(owner: NativeStepOwner, active: () => boolean, callback: () => T): T {
  return retryCallbacks.run(owner, active, () =>
    softCallbacks.run(owner, active, () => attachmentCallbacks.run(owner, active, callback)),
  )
}
let fixtureDefinitions: Readonly<Record<string, NativeFixtureDefinition>> = {}
let fixtureBindings: NativeFixtureBindings | undefined
const fixtureScope = (name: string): 'test' | 'worker' | undefined =>
  fixtureBindings ? fixtureBindings.version(name).scope : fixtureDefinitions[name]?.scope
const valueKey = (name: string, scope: string): string =>
  (fixtureScope(name) === 'worker' ? '0' : scope) + ':' + name
const dependentValues = (
  names: readonly string[],
  scope: string,
  bindings?: readonly (readonly [string, string])[],
): Readonly<Record<string, UntrustedInput>> =>
  Object.fromEntries(
    (bindings ?? names.map((name) => [name, fixtureBindings?.root(name) ?? name] as const)).map(
      ([name, target]) => {
        const key = valueKey(target, scope)
        if (!values.has(key)) throw new Error('Native fixture dependency not ready: ' + name)
        return [name, values.get(key)]
      },
    ),
  )
try {
  const imported = (await import(pathToFileURL(modulePath).href)) as UntrustedInput
  const suite = Definition.parse(z.object({ default: z.unknown() }).parse(imported).default)
  if (
    isNativeFixtureLayers(suite.fixtures) ||
    suite.fixtureOptions !== undefined ||
    Object.values(suite.fixtures ?? {}).some(
      (definition) => 'value' in definition || definition.option !== undefined,
    )
  ) {
    fixtureBindings = new NativeFixtureBindings(suite.fixtures ?? {}, suite.fixtureOptions ?? {})
    for (const row of fixtureBindings.rows()) await send(...row)
  } else {
    const callbacks: [string, NativeFixtureDefinition][] = []
    for (const [name, definition] of Object.entries(suite.fixtures ?? {})) {
      if (!('setup' in definition)) throw new Error('Legacy fixture requires an authored callback')
      callbacks.push([name, definition])
    }
    fixtureDefinitions = Object.fromEntries(callbacks)
  }
  let fixtureDeclaration = 0
  for (const [name, fixture] of Object.entries(fixtureDefinitions)) {
    fixtureDeclaration++
    await send(
      'FIXTURE',
      name,
      fixture.scope ?? 'test',
      fixture.automatic ? 1 : 0,
      fixture.dependencies?.length ? fixture.dependencies.join(',') : '-',
    )
    if (fixture.timeout !== undefined) await send('FTIMEOUT', fixtureDeclaration, fixture.timeout)
    if (fixture.title !== undefined)
      await send(
        'FTITLE',
        fixtureDeclaration,
        Buffer.from(fixture.title, 'utf16le').toString('hex') || '-',
      )
    const location = nativeFixtureLocation(fixture)
    if (location)
      await send(
        'FLOCATION',
        fixtureDeclaration,
        Buffer.from(location.file).toString('hex'),
        location.line,
        location.column,
      )
  }
  const tests: ParsedTest[] = []
  const admittedTimeouts = new Map<string, number>()
  const snapshotCallbacks = new NativeSnapshotCallbacks()
  const repeatCallbacks = new NativeRepeatCallbacks()
  const projectMembership = new Map<string, number>()
  let activeProject: NativeProjectInfo | undefined
  let activeProjectId: number | undefined
  let activeFile: string | undefined
  let activeFileId: string | undefined
  let directoryFileContext = false
  idleCallbacks = new NativeWorkerIdleCallbacks(
    send,
    { workerIndex, pid: process.pid, fileId: () => activeFileId },
    () => {
      const retained = [...leases.values()]
      const workerFixtures = retained.filter(
        (lease) =>
          fixtureScope(lease.name) === 'worker' &&
          lease.used &&
          !lease.canceled &&
          lease.release !== undefined,
      ).length
      return {
        workerFixtures,
        ready:
          !closing &&
          (sourceProtocol?.idleReady ?? true) &&
          fixtureTimeoutCallbacks.idleReady &&
          !fixtureTimeoutCallbacks.physical &&
          retryCallbacks.idleReady &&
          softCallbacks.idleReady &&
          softCallbacks.physical === 0 &&
          softCallbacks.contextCount === 0 &&
          softCallbacks.retirementPending === 0 &&
          !bodies.size &&
          !callbacks.size &&
          !mutationRequests.size &&
          !stepCallbacks.pending &&
          attachmentCallbacks.idleReady &&
          retained.length === workerFixtures &&
          values.size === workerFixtures &&
          valueOwners.size === workerFixtures,
      }
    },
    (error) => {
      process.exitCode = 1
      void send('HOST_ERROR', jsonHex(describe(error))).catch(() => channel.destroy())
    },
  )
  const projectInfo = (): { project?: NativeProjectInfo } =>
    activeProject === undefined ? {} : { project: activeProject }
  const callbackInfo = (): { project?: NativeProjectInfo; file?: string } => ({
    ...projectInfo(),
    ...(activeFile === undefined ? {} : { file: activeFile }),
  })
  const testTitlePaths = new Map<string, readonly string[]>()
  const hookOwners = new Map<string, { id?: string; titlePath: readonly string[] }>()
  const testTitlePath = (id: string): readonly string[] => {
    const path = testTitlePaths.get(id)
    if (!path) throw new Error('Missing actual declared test ancestry')
    return [...path]
  }
  const hookCallbacks = new Map<string, z.infer<typeof Hook>>()
  type MetadataRow = readonly (string | number)[]
  const rows: MetadataRow[] = []
  let rowBytes = 0
  const queueRow = (row: MetadataRow): void => {
    const bytes = Buffer.byteLength(row.join(' ')) + 1
    if (bytes > 16777216 - rowBytes || rows.length >= 32768)
      throw new Error('Native declaration metadata capacity16MiB exceeded')
    rowBytes += bytes
    rows.push(row)
  }
  const addLocks = (
    lock: string | readonly string[] | undefined,
    kind: 'FLOCK' | 'TLOCK' | 'SLOCK',
    owner?: number,
  ): void => {
    const names = lock === undefined ? [] : typeof lock === 'string' ? [lock] : lock
    for (const name of names)
      queueRow(
        owner === undefined
          ? [kind, Buffer.from(name, 'utf16le').toString('hex') || '-']
          : [kind, owner, Buffer.from(name, 'utf16le').toString('hex') || '-'],
      )
  }
  const addSnapshotConfig = (
    declaration: z.infer<typeof NativeSnapshotConfigurationSchema>,
    owner: number,
  ): void => {
    for (const [field, value] of [
      ['testDir', declaration.testDir],
      ['snapshotDir', declaration.snapshotDir],
      ['snapshotPathTemplate', declaration.snapshotPathTemplate],
      ['screenshotPathTemplate', declaration.expect?.toHaveScreenshot?.pathTemplate],
      ['ariaPathTemplate', declaration.expect?.toMatchAriaSnapshot?.pathTemplate],
    ] as const)
      if (value !== undefined) queueRow(['SNAPSHOT_CONFIG', owner, field, hex(value)])
  }
  addSnapshotConfig(suite, 0)
  if (suite.repeatEach !== undefined) queueRow(['REPEAT_EACH', 0, suite.repeatEach])
  if (suite.timeout !== undefined) queueRow(['ROOT_TIMEOUT', suite.timeout])
  const addGrep = (
    declaration: {
      grep?: RegExp | RegExp[] | undefined
      grepInvert?: RegExp | RegExp[] | undefined
    },
    owner: number,
  ): void => {
    for (const [kind, value] of [
      ['include', declaration.grep],
      ['exclude', declaration.grepInvert],
    ] as const) {
      if (value === undefined) continue
      const patterns = value instanceof RegExp ? [value] : value
      queueRow(['GREP_CONFIG', owner, kind, patterns.length])
      for (const pattern of patterns) {
        const source = pattern.source,
          flags = pattern.flags
        if (source.length > 4096) throw new Error('Native config grep source capacity4096 exceeded')
        queueRow(['GREP_REGEXP', owner, kind, jsonHex(source), flags || '-'])
      }
    }
  }
  addGrep(suite, 0)
  if (suite.tag !== undefined) {
    const tags = typeof suite.tag === 'string' ? [suite.tag] : suite.tag
    queueRow(['GREP_GLOBAL_TAGS', tags.length])
    for (const [index, tag] of tags.entries()) queueRow(['GREP_GLOBAL_TAG', index, jsonHex(tag)])
  }
  addLocks(suite.lock, 'FLOCK')
  if (suite.fullyParallel !== undefined) queueRow(['FULLY_PARALLEL', suite.fullyParallel ? 1 : 0])
  let suiteCount = 0,
    callbackCount = 0
  const addHooks = (
    hooks: ParsedHooks | undefined,
    suiteId?: number,
    titlePath: readonly string[] = [],
  ): void => {
    for (const kind of HookKind.options)
      for (const [index, hook] of (hooks?.[kind] ?? []).entries()) {
        if ((kind === 'beforeEach' || kind === 'afterEach') && hook.timeout !== undefined)
          throw new Error(
            'Per-test hook timeout overrides are unsupported; set the native test timeout',
          )
        if (++callbackCount > 8192) throw new Error('Native hook declaration capacity8192 exceeded')
        const id = suiteId === undefined ? index + 1 : callbackCount
        hookCallbacks.set(kind + ':' + id, hook)
        hookOwners.set(kind + ':' + id, {
          ...(suiteId === undefined ? {} : { id: String(suiteId) }),
          titlePath,
        })
        queueRow(
          suiteId === undefined
            ? [
                'HOOK',
                kind,
                id,
                hook.timeout ?? '-',
                hook.fixtures?.length ? hook.fixtures.join(',') : '-',
              ]
            : [
                'SHOOK',
                suiteId,
                kind,
                id,
                hook.timeout ?? '-',
                hook.fixtures?.length ? hook.fixtures.join(',') : '-',
              ],
        )
      }
  }
  interface TitleComponent {
    title: string
    tag?: string | readonly string[] | undefined
  }
  const addTest = (
    test: ParsedTest,
    suiteId?: number,
    position?: number,
    titles: readonly string[] = [],
    components: readonly TitleComponent[] = [],
  ): void => {
    if (tests.length >= 4096) throw new Error('Native test declaration capacity4096 exceeded')
    tests.push(test)
    const id = tests.length
    testTitlePaths.set(String(id), [...titles, test.title])
    queueRow([
      'TEST',
      id,
      test.retries ?? 0,
      test.timeout ?? '-',
      test.expectedFailure ? 1 : 0,
      jsonHex([...titles, test.title].join(' › ')),
      test.fixtures?.length ? test.fixtures.join(',') : '-',
    ])
    for (const [position, component] of [...components, test].entries()) {
      const tags =
        component.tag === undefined
          ? []
          : typeof component.tag === 'string'
            ? [component.tag]
            : component.tag
      queueRow(['GREP_COMPONENT', id, position, jsonHex(component.title), tags.length])
      for (const tag of tags) queueRow(['GREP_TAG', id, position, jsonHex(tag)])
    }
    addLocks(test.lock, 'TLOCK', id)
    if (suiteId !== undefined) queueRow(['MEMBER', id, suiteId, position ?? 0])
    if (test.only) queueRow(['ONLY', id])
    if (test.skip === true || typeof test.skip === 'string')
      queueRow([
        'SKIP',
        id,
        jsonHex(typeof test.skip === 'string' ? test.skip : 'Skipped by declaration'),
      ])
  }
  const addSuite = (
    entries: readonly (ParsedTest | ParsedSuite)[],
    hooks: ParsedHooks | undefined,
    parent: number,
    position: number,
    titles: readonly string[],
    depth: number,
    modifiers?: Pick<ParsedSuite, 'skip' | 'only' | 'mode' | 'lock'>,
    components: readonly TitleComponent[] = [],
  ): void => {
    if (++suiteCount > 256 || depth > 64)
      throw new Error('Native suite declaration capacity exceeded')
    const id = suiteCount
    queueRow(['SUITE', id, parent, position, jsonHex(titles.at(-1) ?? '')])
    addLocks(modifiers?.lock, 'SLOCK', id)
    if (modifiers?.mode !== undefined) queueRow(['SMODE', id, modifiers.mode])
    if (modifiers?.only) queueRow(['SONLY', id])
    if (modifiers?.skip === true || typeof modifiers?.skip === 'string')
      queueRow([
        'SSKIP',
        id,
        jsonHex(
          typeof modifiers.skip === 'string' ? modifiers.skip : 'Skipped by suite declaration',
        ),
      ])
    addHooks(hooks, id, titles)
    for (const [index, entry] of entries.entries()) {
      if ('entries' in entry)
        addSuite(
          entry.entries,
          entry.hooks,
          id,
          index,
          [...titles, entry.title],
          depth + 1,
          entry,
          [...components, { title: entry.title, tag: entry.tag }],
        )
      else addTest(entry, id, index, titles, components)
    }
  }
  if (suite.projects !== undefined) {
    for (const [index, project] of suite.projects.entries()) {
      const id = index + 1
      queueRow([
        'PROJECT',
        id,
        hex(project.name),
        project.default === false ? 0 : 1,
        hex(JSON.stringify(project.metadata ?? {})),
      ])
      if (project.repeatEach !== undefined) queueRow(['REPEAT_EACH', id, project.repeatEach])
      if (project.outputDir !== undefined)
        queueRow(['PROJECT_OUTPUT_DIR', id, hex(project.outputDir)])
      if (project.timeout !== undefined) queueRow(['PROJECT_TIMEOUT', id, project.timeout])
      addSnapshotConfig(project, id)
      addGrep(project, id)
      if (project.fullyParallel !== undefined)
        queueRow(['PROJECT_PARALLEL', id, project.fullyParallel ? 1 : 0])
      for (const dependency of project.dependencies ?? [])
        queueRow(['PROJECT_DEP', id, hex(dependency)])
      if (project.teardown !== undefined) queueRow(['PROJECT_TEARDOWN', id, hex(project.teardown)])
    }
    const root = ++suiteCount
    queueRow(['SUITE', root, 0, 0, jsonHex('')])
    addHooks(suite.hooks, root, [])
    for (const [index, project] of suite.projects.entries()) {
      const before = tests.length
      addSuite(project.entries ?? project.tests ?? [], project.hooks, root, index, [], 2)
      for (let test = before + 1; test <= tests.length; ++test) {
        projectMembership.set(String(test), index + 1)
        queueRow(['PROJECT_TEST', index + 1, test])
      }
    }
  } else if (suite.entries !== undefined) addSuite(suite.entries, suite.hooks, 0, 0, [], 1)
  else {
    for (const test of suite.tests ?? []) addTest(test)
    addHooks(suite.hooks)
  }
  for (const row of rows) await send(...row)
  if (sourceProtocol)
    await send('SOURCE_CAP', process.argv[5] === '--suitecut-source-work=2' ? 2 : 1)
  await send('LOADED')
  for await (const line of incoming) {
    const f = line.split(' '),
      command = f[0]
    if (idleCallbacks.packet(f)) continue
    if (command === 'SETUP' || command === 'BODY' || command === 'CALL')
      idleCallbacks.beforeAuthorAdmission()
    if (sourceProtocol?.packet(f)) continue
    if (fixtureBindings?.packet(f)) continue
    if (await outputCallbacks.packet(f, workerIndex, process.pid)) continue
    if (fixtureTimeoutCallbacks.packet(f)) continue
    if (retryCallbacks.packet(f)) continue
    if (softCallbacks.packet(f)) continue
    if (attachmentCallbacks.packet(f)) continue
    if (stepCallbacks.handle(f)) continue
    if (command === 'PROJECT_REPEAT_CONTEXT') {
      if (!activeProject || leases.size || bodies.size || callbacks.size)
        throw new Error('Native project repeat context arrived outside original author admission')
      const declared = repeatCallbacks.project(f, activeProjectId ?? 0)
      activeProject = Object.freeze({ ...activeProject, repeatEach: declared })
      continue
    } else if (command === 'REPEAT_ACTOR_CONTEXT') {
      const declared = repeatCallbacks.begin(f, activeProjectId ?? 0)
      // Older native actor ABI may supply only its existing repeat marker.
      if (activeProject && activeProject.repeatEach === undefined)
        activeProject = Object.freeze({ ...activeProject, repeatEach: declared })
      continue
    } else if (command === 'TEST_REPEAT_CONTEXT') {
      repeatCallbacks.packet(f, tests.length)
      continue
    } else if (command === 'TEST_SNAPSHOT_CONTEXT') {
      snapshotCallbacks.packet(f, tests.length)
      continue
    } else if (command === 'TEST_TIMEOUT_CONTEXT') {
      if (f.length !== 3 || bodies.size || callbacks.size)
        throw new Error('Invalid native test timeout context')
      const id = z
        .string()
        .regex(/^[1-9][0-9]*$/u)
        .parse(f[1])
      const timeout = z.coerce.number().int().min(0).max(120000).parse(f[2])
      const definition = tests[Number(id) - 1]
      if (
        !definition ||
        admittedTimeouts.has(id) ||
        (definition.timeout !== undefined && definition.timeout !== timeout)
      )
        throw new Error('Native test timeout context differs from declared test identity')
      admittedTimeouts.set(id, timeout)
      continue
    }
    if (command === 'DIRECTORY_FILE_CONTEXT') {
      if (
        activeFile !== undefined ||
        activeProject !== undefined ||
        directoryFileContext ||
        leases.size ||
        bodies.size ||
        callbacks.size ||
        f.length !== 3
      )
        throw new Error('Invalid native directory file context admission')
      z.string()
        .regex(/^[1-9][0-9]{0,19}$/u)
        .parse(f[1])
      const file = z
        .string()
        .min(1)
        .max(16384)
        .parse(JSON.parse(unhex(z.string().parse(f[2]))) as UntrustedInput)
      if (file !== modulePath || !file.startsWith('/') || file.includes('\0'))
        throw new Error('Native directory context differs from actual loaded suite path')
      activeFile = file
      activeFileId = f[1]
      directoryFileContext = true
    } else if (command === 'FILE_CONTEXT') {
      if (
        activeFile !== undefined ||
        activeProject !== undefined ||
        suite.projects !== undefined ||
        leases.size ||
        bodies.size ||
        callbacks.size ||
        f.length !== 3
      )
        throw new Error('Invalid native file context admission')
      z.string()
        .regex(/^[1-9][0-9]{0,19}$/u)
        .parse(f[1])
      const file = z
        .string()
        .min(1)
        .max(8192)
        .parse(JSON.parse(unhex(z.string().parse(f[2]))) as UntrustedInput)
      if (file !== modulePath || !file.startsWith('/') || file.includes('\0'))
        throw new Error('Native file context differs from actual loaded suite path')
      activeFile = file
      activeFileId = f[1]
    } else if (command === 'PROJECT_CONTEXT') {
      if (
        activeProject !== undefined ||
        (activeFile !== undefined && !directoryFileContext) ||
        leases.size ||
        bodies.size ||
        callbacks.size ||
        (f.length !== 4 && f.length !== 5)
      )
        throw new Error('Invalid native project context admission')
      activeProjectId = z.coerce
        .number()
        .int()
        .min(1)
        .max(suite.projects?.length ?? 0)
        .parse(f[1])
      const name = ProjectName.parse(unhex(z.string().parse(f[2])))
      const metadata = ProjectMetadata.parse(
        JSON.parse(unhex(z.string().parse(f[3]))) as UntrustedInput,
      )
      const declared = suite.projects?.[activeProjectId - 1]
      if (
        declared?.name !== name ||
        JSON.stringify(declared.metadata ?? {}) !== JSON.stringify(metadata)
      )
        throw new Error('Native project context differs from actual declaration')
      const outputDir =
        f.length === 5 ? ProjectOutputDir.parse(unhex(z.string().parse(f[4]))) : undefined
      if (
        (declared.outputDir === undefined) !== (outputDir === undefined) ||
        (outputDir !== undefined && !outputDir.startsWith('/'))
      )
        throw new Error('Native project output context lacks the configured absolute root')
      activeProject = Object.freeze({
        name,
        metadata: Object.freeze(metadata),
        ...(outputDir === undefined ? {} : { outputDir }),
      })
    } else if (command === 'SETUP') {
      const [token, owner, generation, name, scopeKind, actualTestId] = z
        .union([
          z.tuple([z.string(), z.string(), z.string(), Name]),
          z.tuple([
            z.string(),
            z.string(),
            z.string(),
            Name,
            z.enum(['test', 'beforeAll', 'afterAll']),
            z.string(),
          ]),
        ])
        .parse(f.slice(1))
      const version = fixtureBindings?.version(name)
      const definition = version?.definition ?? fixtureDefinitions[name]
      if (!definition || leases.has(token) || leases.size >= 64)
        throw new Error('Invalid native fixture setup admission')
      const constant = version?.constant
      const setup: NativeFixtureDefinition['setup'] = constant
        ? async (_dependencies, use) => {
            await use(constant.value)
          }
        : 'setup' in definition
          ? definition.setup
          : () => {
              throw new Error('Literal fixture lacks actual native selected constant')
            }
      const sourceSelection = sourceProtocol?.take(
        token,
        owner,
        generation,
        name,
        version?.scope ?? definition.scope,
        constant !== undefined || !('setup' in definition),
        scopeKind ?? 'test',
        actualTestId ?? owner,
      )
      const key = valueKey(name, owner)
      if (valueOwners.has(key)) throw new Error('Native fixture value scope already owned')
      valueOwners.set(key, token)
      const lease: Lease = {
        token,
        owner,
        generation,
        name,
        valueKey: key,
        used: false,
        canceled: false,
      }
      leases.set(token, lease)
      const clockOwner = { token, scope: owner, generation }
      const clockInfo = fixtureTimeoutCallbacks.bind(clockOwner)
      void (async () => {
        try {
          let beforeSourceUse: (() => void) | undefined
          const invokeSetup = () =>
            setup(
              dependentValues(
                'setup' in definition ? (definition.dependencies ?? []) : [],
                owner,
                version?.bindings,
              ),
              async (value) => {
                beforeSourceUse?.()
                if (lease.used) throw new Error('Fixture use() called more than once: ' + name)
                lease.used = true
                values.set(key, value)
                const resume = new Promise<void>((resolve) => {
                  lease.release = resolve
                })
                if (lease.canceled) lease.release?.()
                if (await fixtureTimeoutCallbacks.beforeReady(clockOwner))
                  await send('READY', token, owner, generation)
                await resume
              },
              {
                ...((version?.scope ?? definition.scope) === 'worker'
                  ? { workerIndex, scope: 'worker' }
                  : scopeKind === 'beforeAll' || scopeKind === 'afterAll'
                    ? { workerIndex, scope: 'hook', scopeId: owner, hook: scopeKind }
                    : {
                        testId: actualTestId ?? owner,
                        workerIndex,
                        scope: 'test',
                      }),
                ...projectInfo(),
                get timeout() {
                  return clockInfo.timeout
                },
                get signal() {
                  return clockInfo.signal
                },
                setTimeout: clockInfo.setTimeout,
              } satisfies NativeFixtureInfo,
            )
          const raw = fixtureTimeoutCallbacks.run(clockOwner, () =>
            sourceSelection && sourceProtocol
              ? sourceProtocol.callbacks.invoke(sourceSelection, (beforeUse) => {
                  beforeSourceUse = beforeUse
                  return invokeSetup()
                })
              : invokeSetup(),
          )
          await send('STARTED', token, owner, generation)
          await raw
          const mutationError = await fixtureTimeoutCallbacks.beforeRawSettlement(clockOwner)
          if (mutationError) throw mutationError
          await send('SETTLED', token, owner, generation, 1, '-')
        } catch (error) {
          await fixtureTimeoutCallbacks.beforeRawSettlement(clockOwner)
          await send(
            'SETTLED',
            token,
            owner,
            generation,
            0,
            jsonHex(describe(error as UntrustedInput)),
          )
        } finally {
          sourceProtocol?.close(token, generation)
          fixtureTimeoutCallbacks.closed(clockOwner)
          leases.delete(token)
          if (valueOwners.get(key) === token) {
            valueOwners.delete(key)
            values.delete(key)
          }
          maybeClose()
        }
      })()
    } else if (command === 'TEARDOWN' || command === 'CANCEL') {
      const [token, owner, generation] = z
        .tuple([z.string(), z.string(), z.string(), z.string()])
        .parse(f.slice(1))
      const lease = leases.get(token)
      if (lease?.owner !== owner || lease.generation !== generation)
        throw new Error('Stale native fixture continuation')
      sourceProtocol?.close(token, generation)
      if (command === 'CANCEL') lease.canceled = true
      lease.release?.()
    } else if (
      command === 'TIMEOUT_RESULT' ||
      command === 'SKIP_RESULT' ||
      command === 'FAIL_RESULT'
    ) {
      const [request, token, owner, generation, ok, value] = z
        .tuple([z.string(), z.string(), z.string(), z.string(), z.enum(['0', '1']), z.string()])
        .parse(f.slice(1, 7))
      if ((command === 'TIMEOUT_RESULT' || ok === '0') && f.length !== 7)
        throw new Error('Invalid native mutation acknowledgment length')
      const pending = mutationRequests.get(request)
      if (
        pending?.token !== token ||
        pending.owner !== owner ||
        pending.generation !== generation ||
        pending.kind !==
          (command === 'TIMEOUT_RESULT'
            ? 'timeout'
            : command === 'SKIP_RESULT'
              ? 'skip'
              : 'failure')
      )
        throw new Error('Stale native live timeout acknowledgment')
      mutationRequests.delete(request)
      if (ok === '1') {
        if (command === 'TIMEOUT_RESULT')
          pending.callback.timeout = z.coerce.number().int().min(0).max(120000).parse(value)
        else {
          const expected = ExpectedStatus.parse(value)
          if (expected !== (command === 'SKIP_RESULT' ? 'skipped' : 'failed'))
            throw new Error('Native modifier ACK expectation mismatch')
          const annotation = parseAnnotation(f.slice(7))
          if (annotation.type !== (command === 'SKIP_RESULT' ? 'skip' : 'fail'))
            throw new Error('Native modifier ACK annotation type mismatch')
          pending.callback.expectedStatus = expected
          pending.callback.annotations = Object.freeze([
            ...pending.callback.annotations,
            annotation,
          ])
          if (command === 'SKIP_RESULT') {
            if (!pending.callback.skipControl || pending.callback.skipRequest !== request)
              throw new Error('Native accepted skip ACK has no owning callback control')
            pending.callback.skipAccepted = true
            attachmentCallbacks.acceptTestSkip(
              { token, scope: owner, generation },
              request,
              pending.callback.skipControl,
            )
          }
        }
        pending.resolve()
      } else pending.reject(new Error(unhex(value)))
    } else if (command === 'BODY') {
      const [owner, generation, retryText, status, duration] = z
          .tuple([
            z.string(),
            z.string(),
            z.string(),
            z.enum(['passed', 'failed', 'timedOut', 'interrupted', 'skipped']),
            z.coerce.number().finite().nonnegative(),
          ])
          .parse(f.slice(1, 6)),
        index = z.coerce.number().int().min(1).max(tests.length).parse(owner) - 1
      const test = tests[index]
      const repeatContext = repeatCallbacks.snapshot(owner)
      if (
        !test ||
        bodies.size ||
        (suite.projects !== undefined && projectMembership.get(owner) !== activeProjectId)
      )
        throw new Error('Invalid native body/project admission')
      const retry = z.coerce.number().int().min(0).max(100).parse(retryText),
        abort = new AbortController()
      const snapshots = snapshotCallbacks.admit(owner, retry)
      const body: Body = {
        owner,
        generation,
        abort,
        raw: Promise.resolve(),
        timeout: z.number().parse(admittedTimeouts.get(owner)),
        amendments: new Set(),
        acceptsAmendments: true,
        ...parseAdmissionMetadata(f, 6),
      }
      bodies.set(owner, body)
      body.raw = (async () => {
        try {
          await runAuthorCallback(
            { token: '0', scope: owner, generation },
            () => body.acceptsAmendments && !body.abort.signal.aborted && !closing,
            () =>
              test.run(dependentValues(test.fixtures ?? [], owner), {
                ...callbackInfo(),
                ...outputCallbacks.snapshot(owner),
                get snapshotDir() {
                  return snapshots.snapshotDir
                },
                get snapshotSuffix() {
                  return snapshots.snapshotSuffix
                },
                set snapshotSuffix(value: string) {
                  snapshots.snapshotSuffix = value
                },
                snapshotPath: snapshots.snapshotPath,
                testId: repeatContext.logicalTestId,
                repeatEachIndex: repeatContext.repeatEachIndex,
                title: test.title,
                titlePath: testTitlePath(owner),
                retry,
                workerIndex,
                get timeout() {
                  return body.timeout
                },
                get expectedStatus() {
                  return body.expectedStatus
                },
                get annotations() {
                  return body.annotations
                },
                get errors() {
                  return softCallbacks.errors({ token: '0', scope: owner, generation })
                },
                attachments: attachmentCallbacks.view({ token: '0', scope: owner, generation }),
                attach: attachmentCallbacks.bind(
                  { token: '0', scope: owner, generation },
                  () => body.acceptsAmendments && !body.abort.signal.aborted && !closing,
                ),
                step: stepCallbacks.bind(
                  { token: '0', scope: owner, generation },
                  () => body.acceptsAmendments && !body.abort.signal.aborted && !closing,
                ),
                setTimeout: (timeout) => amendTimeout(body, '0', timeout),
                skip: callbackSkip(body, '0', 'body'),
                fail: callbackFail(body, '0', 'body'),
                status,
                duration,
                signal: abort.signal,
              }),
          )
          body.acceptsAmendments = false
          await Promise.all(body.amendments)
          if (body.amendmentError) throw body.amendmentError
          await send('BODY_RESULT', owner, generation, 1, '-')
        } catch (error) {
          body.acceptsAmendments = false
          await Promise.allSettled(body.amendments)
          if (error === body.skipControl && body.skipAccepted && !body.amendmentError)
            await send('BODY_SKIPPED', owner, generation)
          else {
            const actualError = (
              error === body.skipControl ? (body.amendmentError ?? error) : error
            ) as UntrustedInput
            const originating = stepCallbacks.originating(
              { token: '0', scope: owner, generation },
              actualError,
            )
            await send(
              originating ? 'BODY_RESULT_FROM_STEP_UTF16' : 'BODY_RESULT_UTF16',
              owner,
              generation,
              0,
              Buffer.from(describe(actualError), 'utf16le').toString('hex') || '-',
              ...(originating === undefined ? [] : [originating]),
            )
          }
        } finally {
          maybeClose()
        }
      })()
      stepCallbacks.trackCallback({ token: '0', scope: owner, generation }, body.raw, () => {
        retryCallbacks.retireCallback({ token: '0', scope: owner, generation }, () =>
          softCallbacks.retireCallback({ token: '0', scope: owner, generation }, () =>
            attachmentCallbacks.retireCallback({ token: '0', scope: owner, generation }, () => {
              bodies.delete(owner)
              maybeClose()
            }),
          ),
        )
      })
    } else if (command === 'CALL') {
      const [
        token,
        owner,
        generation,
        kind,
        callbackText,
        retryText,
        testId,
        timeoutText,
        status,
        duration,
      ] = z
        .tuple([
          z.string(),
          z.string(),
          z.string(),
          z.enum(['body', ...HookKind.options]),
          z.string(),
          z.string(),
          z.string(),
          z.string(),
          z.enum(['passed', 'failed', 'timedOut', 'interrupted', 'skipped']),
          z.coerce.number().finite().nonnegative(),
        ])
        .parse(f.slice(1, 11))
      const callbackIndex = z.coerce.number().int().min(1).max(8192).parse(callbackText) - 1,
        retry = z.coerce.number().int().min(0).max(100).parse(retryText),
        timeout = z.coerce.number().int().min(0).max(120000).parse(timeoutText),
        abort = new AbortController()
      const repeatContext = repeatCallbacks.snapshot(testId)
      if (callbacks.has(token) || callbacks.size + bodies.size >= 64)
        throw new Error('Native hook physical callback admission failed')
      const callback: Body = {
        owner,
        generation,
        abort,
        raw: Promise.resolve(),
        timeout,
        amendments: new Set(),
        acceptsAmendments: true,
        ...parseAdmissionMetadata(f, 11),
      }
      callbacks.set(token, callback)
      callback.raw = (async () => {
        try {
          if (kind === 'body') {
            const test = tests[callbackIndex]
            if (
              !test ||
              testId !== String(callbackIndex + 1) ||
              (suite.projects !== undefined && projectMembership.get(testId) !== activeProjectId)
            )
              throw new Error('Invalid native test callback identity')
            const snapshots = snapshotCallbacks.admit(testId, retry)
            await runAuthorCallback(
              { token, scope: owner, generation },
              () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
              () =>
                test.run(dependentValues(test.fixtures ?? [], owner), {
                  ...callbackInfo(),
                  ...outputCallbacks.snapshot(testId),
                  get snapshotDir() {
                    return snapshots.snapshotDir
                  },
                  get snapshotSuffix() {
                    return snapshots.snapshotSuffix
                  },
                  set snapshotSuffix(value: string) {
                    snapshots.snapshotSuffix = value
                  },
                  snapshotPath: snapshots.snapshotPath,
                  testId: repeatContext.logicalTestId,
                  repeatEachIndex: repeatContext.repeatEachIndex,
                  title: test.title,
                  titlePath: testTitlePath(testId),
                  retry,
                  workerIndex,
                  get timeout() {
                    return callback.timeout
                  },
                  get expectedStatus() {
                    return callback.expectedStatus
                  },
                  get annotations() {
                    return callback.annotations
                  },
                  get errors() {
                    return softCallbacks.errors({ token, scope: owner, generation })
                  },
                  attachments: attachmentCallbacks.view({ token, scope: owner, generation }),
                  attach: attachmentCallbacks.bind(
                    { token, scope: owner, generation },
                    () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
                  ),
                  step: stepCallbacks.bind(
                    { token, scope: owner, generation },
                    () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
                  ),
                  setTimeout: (timeout) => amendTimeout(callback, token, timeout),
                  skip: callbackSkip(callback, token, kind),
                  fail: callbackFail(callback, token, kind),
                  status,
                  duration,
                  signal: abort.signal,
                }),
            )
          } else {
            const hook = hookCallbacks.get(kind + ':' + (callbackIndex + 1))
            if (!hook) throw new Error('Invalid native hook callback identity')
            const test = tests[z.coerce.number().int().min(1).max(tests.length).parse(testId) - 1]
            const ownerInfo = hookOwners.get(kind + ':' + (callbackIndex + 1))
            const titlePath = testTitlePath(testId)
            if (
              !test ||
              !ownerInfo ||
              (suite.projects !== undefined && projectMembership.get(testId) !== activeProjectId)
            )
              throw new Error('Missing actual associated test/suite metadata for hook')
            const snapshots = snapshotCallbacks.admit(testId, retry)
            const info: NativeHookInfo = {
              hook: kind,
              scopeId: owner,
              ...callbackInfo(),
              ...outputCallbacks.snapshot(testId),
              get snapshotDir() {
                return snapshots.snapshotDir
              },
              get snapshotSuffix() {
                return snapshots.snapshotSuffix
              },
              set snapshotSuffix(value: string) {
                snapshots.snapshotSuffix = value
              },
              snapshotPath: snapshots.snapshotPath,
              testId: repeatContext.logicalTestId,
              repeatEachIndex: repeatContext.repeatEachIndex,
              title: test.title,
              titlePath,
              retry,
              workerIndex,
              get timeout() {
                return callback.timeout
              },
              get expectedStatus() {
                return callback.expectedStatus
              },
              get annotations() {
                return callback.annotations
              },
              get errors() {
                return softCallbacks.errors({ token, scope: owner, generation })
              },
              attachments: attachmentCallbacks.view({ token, scope: owner, generation }),
              attach: attachmentCallbacks.bind(
                { token, scope: owner, generation },
                () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
              ),
              step: stepCallbacks.bind(
                { token, scope: owner, generation },
                () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
              ),
              setTimeout: (timeout) => amendTimeout(callback, token, timeout),
              skip: callbackSkip(callback, token, kind),
              fail: callbackFail(callback, token, kind),
              status,
              duration,
              signal: abort.signal,
              ...(ownerInfo.id === undefined ? {} : { suiteId: ownerInfo.id }),
              suiteTitlePath: [...ownerInfo.titlePath],
            }
            await runAuthorCallback(
              { token, scope: owner, generation },
              () => callback.acceptsAmendments && !callback.abort.signal.aborted && !closing,
              () => hook.run(dependentValues(hook.fixtures ?? [], owner), info),
            )
          }
          callback.acceptsAmendments = false
          await Promise.all(callback.amendments)
          if (callback.amendmentError) throw callback.amendmentError
          await send('CALL_RESULT', token, owner, generation, 1, '-')
        } catch (error) {
          callback.acceptsAmendments = false
          await Promise.allSettled(callback.amendments)
          if (error === callback.skipControl && callback.skipAccepted && !callback.amendmentError)
            await send('CALL_SKIPPED', token, owner, generation)
          else {
            const actualError = (
              error === callback.skipControl ? (callback.amendmentError ?? error) : error
            ) as UntrustedInput
            const originating = stepCallbacks.originating(
              { token: token, scope: owner, generation },
              actualError,
            )
            await send(
              originating ? 'CALL_RESULT_FROM_STEP_UTF16' : 'CALL_RESULT_UTF16',
              token,
              owner,
              generation,
              0,
              Buffer.from(describe(actualError), 'utf16le').toString('hex') || '-',
              ...(originating === undefined ? [] : [originating]),
            )
          }
        } finally {
          maybeClose()
        }
      })()
      stepCallbacks.trackCallback({ token, scope: owner, generation }, callback.raw, () => {
        retryCallbacks.retireCallback({ token, scope: owner, generation }, () =>
          softCallbacks.retireCallback({ token, scope: owner, generation }, () =>
            attachmentCallbacks.retireCallback({ token, scope: owner, generation }, () => {
              callbacks.delete(token)
              maybeClose()
            }),
          ),
        )
      })
    } else if (command === 'ABORT_CALL') {
      const [token, owner, generation, error] = z
        .tuple([z.string(), z.string(), z.string(), z.string()])
        .parse(f.slice(1))
      const callback = callbacks.get(token)
      if (callback) {
        if (callback.owner !== owner || callback.generation !== generation)
          throw new Error('Stale native hook abort')
        callback.abort.abort(new Error(unhex(error)))
      }
    } else if (command === 'ABORT') {
      const [owner, generation, error] = z
          .tuple([z.string(), z.string(), z.string()])
          .parse(f.slice(1)),
        body = bodies.get(owner)
      if (body?.generation !== generation) throw new Error('Stale native test abort')
      body.abort.abort(new Error(unhex(error)))
    } else if (command === 'CLOSE') {
      closing = true
      maybeClose()
    } else throw new Error('Unknown native runner host command: ' + String(command))
  }
} catch (error) {
  await send('HOST_ERROR', jsonHex(describe(error as UntrustedInput)))
  process.exitCode = 1
  closing = true
  maybeClose()
}
