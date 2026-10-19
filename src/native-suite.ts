// SPDX-License-Identifier: MIT
import { isNativeFixtureLayers, type NativeFixtureLayers } from './native-fixture-layers.js'
import { captureNativeFixtureLocations } from './native-fixture-location.js'
import {
  type NativeFixtureLiteral,
  type NativeSelectableFixtureDefinition,
} from './native-fixture-values.js'
import { type NativeTestAttachments } from './native-runner-attachment-view.js'
import { type NativeTestAttach } from './native-runner-attachments.js'
import { type NativeTestOutput } from './native-runner-output.js'
import {
  type NativeSnapshotConfiguration,
  type NativeTestSnapshots,
} from './native-runner-snapshot-paths.js'
import { type NativeTestStep } from './native-runner-steps.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeTestAnnotation {
  readonly type: 'skip' | 'fail'
  readonly description?: string
  readonly location?: { readonly file: string; readonly line: number; readonly column: number }
}

/** Native-selected project context; metadata is serialized JSON, not executable configuration. */
export interface NativeProjectInfo {
  /** Native-resolved explicitly configured root; each attempt receives its own outputDir child. */
  readonly outputDir?: string
  readonly repeatEach?: number
  readonly name: string
  readonly metadata: Readonly<Record<string, UntrustedInput>>
}
/** Callback declarations only. Native C++ owns their execution order and lifetime. */
export interface NativeTestInfo extends NativeTestOutput, NativeTestSnapshots {
  readonly project?: NativeProjectInfo
  /** Native canonical suite path for admitted anonymous parallel file groups. */
  readonly file?: string
  readonly step: NativeTestStep
  /** Native-owned artifact copy/write; resolves only after actual committed file and writer exit. */
  readonly attach: NativeTestAttach
  /** Actual author declarations and native committed attachments; append/read only. */
  /** Readonly snapshots of original mismatch Errors acknowledged by the native soft journal. */
  readonly errors: readonly Error[]
  readonly attachments: NativeTestAttachments
  readonly testId: string
  readonly title: string
  /** Exact declared suite titles followed by this test title; no synthetic file/project prefix. */
  readonly titlePath: readonly string[]
  readonly repeatEachIndex: number
  readonly retry: number
  readonly workerIndex: number
  readonly timeout: number
  /** Raw native phase outcome at callback admission, before expected-failure classification. */
  /** Latest native admission/ACK observation. Await fail() before reading its accepted change. */
  readonly expectedStatus: 'passed' | 'failed' | 'skipped'
  /** Readonly snapshots of actual native-admitted runtime skip/fail annotations. */
  readonly annotations: readonly NativeTestAnnotation[]
  readonly status: 'passed' | 'failed' | 'timedOut' | 'interrupted' | 'skipped'
  /** Native monotonic milliseconds elapsed in the associated attempt at admission. */
  readonly duration: number
  /** Await the real native amendment. Elapsed phase time and the global cap are retained. */
  readonly setTimeout: (timeout: number) => Promise<void>
  /** Stops the current body/beforeEach or beforeAll subtree after native admission; actual cleanup still runs. */
  readonly skip: {
    (): never
    (reason: string): never
    (condition: boolean, reason?: string): void
  }
  /** Marks this actual body/beforeEach attempt as expected to fail after genuine native admission. */
  readonly fail: (conditionOrReason?: boolean | string, reason?: string) => Promise<void>
  readonly signal: AbortSignal
}
export type NativeFixtureInfo = (
  | { readonly scope: 'worker'; readonly workerIndex: number }
  | { readonly scope: 'test'; readonly workerIndex: number; readonly testId: string }
  | {
      readonly scope: 'hook'
      readonly workerIndex: number
      readonly scopeId: string
      readonly hook: 'beforeAll' | 'afterAll'
    }
) & {
  readonly project?: NativeProjectInfo
  readonly timeout?: number
  readonly signal?: AbortSignal
  readonly setTimeout?: (timeout: number) => Promise<void>
}
export type NativeFixtureValues = Readonly<Record<string, UntrustedInput>>
export interface NativeFixtureDefinition {
  readonly title?: string
  /** Native independent setup/teardown slot; zero disables only the fixture limit. */
  readonly timeout?: number
  readonly option?: boolean
  readonly scope?: 'worker' | 'test'
  readonly dependencies?: readonly string[]
  readonly automatic?: boolean
  readonly setup: (
    dependencies: NativeFixtureValues,
    use: (value: UntrustedInput) => Promise<void>,
    info: NativeFixtureInfo,
  ) => void | Promise<void>
}
export interface NativeTestDefinition {
  readonly title: string
  /** Declared tags follow this test component in the native grep title. */
  readonly tag?: string | readonly string[]
  readonly lock?: string | readonly string[]
  readonly fixtures?: readonly string[]
  readonly retries?: number
  readonly timeout?: number
  readonly expectedFailure?: boolean
  /** Static declaration skip; string values preserve the declared reason. */
  readonly skip?: boolean | string
  /** Focus this declaration; native C++ applies focus before explicit filters/shards. */
  readonly only?: boolean
  readonly run: (fixtures: NativeFixtureValues, info: NativeTestInfo) => void | Promise<void>
}
export type NativeHookKind = 'beforeAll' | 'beforeEach' | 'afterEach' | 'afterAll'
export type NativeHookInfo = NativeTestInfo & {
  readonly hook: NativeHookKind
  readonly scopeId: string
  /** Actual declaring suite identity; absent for the legacy unnamed flat root. */
  readonly suiteId?: string
  readonly suiteTitlePath: readonly string[]
}
export interface NativeHookDefinition {
  readonly fixtures?: readonly string[]
  /** Independent deadline for beforeAll/afterAll; other hooks share the test budget. */
  readonly timeout?: number
  readonly run: (fixtures: NativeFixtureValues, info: NativeHookInfo) => void | Promise<void>
}
/** Ordered nested declarations; native C++ derives execution and scope order. */
export interface NativeNestedSuiteDefinition {
  readonly title: string
  /** Declared tags follow this suite component in the native grep title. */
  readonly tag?: string | readonly string[]
  readonly lock?: string | readonly string[]
  /** Native parallel groups require declared projects; serial groups retry as one unit. */
  readonly mode?: 'default' | 'serial' | 'parallel'
  /** Static skip inherited by descendants; a nested explicit reason takes precedence. */
  readonly skip?: boolean | string
  /** Focus this subtree; more specific focused declarations narrow it. */
  readonly only?: boolean
  readonly hooks?: Partial<Readonly<Record<NativeHookKind, readonly NativeHookDefinition[]>>>
  readonly entries: readonly NativeSuiteEntry[]
}
export type NativeSuiteEntry = NativeTestDefinition | NativeNestedSuiteDefinition
export type NativeProjectDefinition = NativeSnapshotConfiguration & {
  readonly repeatEach?: number
  /** Native output root; relative paths resolve beside this actual suite module. */
  readonly outputDir?: string
  /** Native default for omitted test limits and ordinary worker fixture slots. Zero disables the local limit. */
  readonly timeout?: number
  /** Overrides the inherited suite config include/exclude field. */
  readonly grep?: RegExp | readonly RegExp[]
  readonly grepInvert?: RegExp | readonly RegExp[]
  /** Overrides the root setting; explicit false preserves ordered project declarations. */
  readonly fullyParallel?: boolean
  readonly name: string
  readonly dependencies?: readonly string[]
  readonly teardown?: string
  readonly default?: boolean
  readonly metadata?: Readonly<Record<string, UntrustedInput>>
  readonly hooks?: Partial<Readonly<Record<NativeHookKind, readonly NativeHookDefinition[]>>>
} & (
    | { readonly tests: readonly NativeTestDefinition[]; readonly entries?: never }
    | { readonly entries: readonly NativeSuiteEntry[]; readonly tests?: never }
  )
export type NativeSuiteDefinition = NativeSnapshotConfiguration & {
  readonly repeatEach?: number
  /** Native root default; omitted defaults to30000ms. Project and explicit test limits override it. */
  readonly timeout?: number
  /** Config tags follow the actual file component in native grep titles. */
  readonly tag?: string | readonly string[]
  /** Native config patterns filter each catalog; CLI grep further filters primary tests. */
  readonly grep?: RegExp | readonly RegExp[]
  readonly grepInvert?: RegExp | readonly RegExp[]
  /** Splits selected declared-project tests into native worker groups (workers > 1). */
  readonly fullyParallel?: boolean
  readonly lock?: string | readonly string[]
  readonly fixtures?:
    Readonly<Record<string, NativeSelectableFixtureDefinition>> | NativeFixtureLayers
  readonly fixtureOptions?: Readonly<Record<string, NativeFixtureLiteral>>
  readonly hooks?: Partial<Readonly<Record<NativeHookKind, readonly NativeHookDefinition[]>>>
} & (
    | {
        readonly tests: readonly NativeTestDefinition[]
        readonly entries?: never
        readonly projects?: never
      }
    | {
        readonly entries: readonly NativeSuiteEntry[]
        readonly tests?: never
        readonly projects?: never
      }
    | {
        readonly projects: readonly NativeProjectDefinition[]
        readonly tests?: never
        readonly entries?: never
      }
  )
/** Declares a native suite without invoking any test or fixture callback. */
export function defineNativeSuite<T extends NativeSuiteDefinition>(suite: T): T {
  if (suite.fixtures && !isNativeFixtureLayers(suite.fixtures))
    captureNativeFixtureLocations(suite.fixtures)
  return suite
}
