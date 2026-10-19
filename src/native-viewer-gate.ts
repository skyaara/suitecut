// SPDX-License-Identifier: MIT
import { AsyncLocalStorage } from 'node:async_hooks'

import { toError, type UntrustedInput } from './untrusted.js'

export type NativeViewerMode = 'agent' | 'taking-over' | 'human' | 'releasing' | 'closed'
interface ViewerRole {
  gate: NativeViewerGate
  kind: 'viewer' | 'agent'
  generation: number
  active: boolean
}
const roles = new AsyncLocalStorage<ViewerRole>()
const gates = new WeakMap<AbortSignal, NativeViewerGate>()

/** Retains SDK queue ownership as well as already dispatched native commands. */
export function runNativeViewerRequest<T>(
  processSignal: AbortSignal,
  work: () => Promise<T>,
): Promise<T> {
  const gate = gates.get(processSignal)
  const role = roles.getStore()
  return gate && !(role?.gate === gate && role.kind === 'viewer') ? gate.trackRequest(work) : work()
}

/** Stops new authored SDK commands while an attached viewer owns the helper. */
export function assertNativeViewerAdmission(
  processSignal: AbortSignal,
  command: string,
  fields: Record<string, UntrustedInput>,
  continuation: boolean,
): void {
  const gate = gates.get(processSignal)
  if (!gate) return
  const role = roles.getStore()
  const parameters =
    fields.params && typeof fields.params === 'object'
      ? (fields.params as Record<string, UntrustedInput>)
      : {}
  if (role?.gate === gate && role.kind === 'viewer') {
    if (role.active && role.generation === gate.generation && gate.mode !== 'closed') return
    throw new Error('Browser viewer control lease expired')
  }
  if (gate.mode === 'agent') return
  if (gate.mode === 'taking-over' && role?.gate === gate && role.kind === 'agent' && role.active)
    return
  // Finish actual callbacks and releases needed by already admitted work. An
  // authored context close/recovery remains blocked; process close stays usable.
  if (
    continuation &&
    ([
      'routeCallback',
      'networkBody',
      'continueAction',
      'continueURLPredicate',
      'continueEventPredicate',
      'claimConsoleArguments',
      'acknowledgeConsole',
      'locatorHandlerOperationsRetired',
      'locatorHandlerOperationEnd',
    ].includes(command) ||
      (command === 'handle' && (fields.action === 'dispose' || fields.action === 'disposeMany')) ||
      (command === 'highlight' && fields.action === 'hide') ||
      (command === 'debugger' && fields.action === 'close') ||
      ((command === 'recording' || command === 'streaming') &&
        (fields.action === 'stop' || fields.action === 'cancel')) ||
      (command === 'context' &&
        (fields.action === 'workerNetworkBody' ||
          (fields.action === 'screenshotInput' &&
            typeof parameters.action === 'string' &&
            ['discard', 'watch'].includes(parameters.action)))) ||
      (command === 'bindings' &&
        typeof fields.action === 'string' &&
        ['resolve', 'argument', 'stage'].includes(fields.action)))
  )
    return
  throw new Error('Browser is paused for human control; submit AI steps through viewer.runAgent()')
}

/** One viewer per real helper, including every existing page/context alias. */
export class NativeViewerGate {
  mode: NativeViewerMode = 'agent'
  generation = 1
  private activeSteps = 0
  private requests = 0
  private readonly changes = new Set<() => void>()
  constructor(private readonly processSignal: AbortSignal) {
    if (gates.has(processSignal)) throw new Error('This native browser already has a viewer')
    processSignal.throwIfAborted()
    gates.set(processSignal, this)
  }
  transition(mode: NativeViewerMode): void {
    this.mode = mode
    ++this.generation
    for (const change of this.changes) change()
  }
  async runViewer<T>(work: () => Promise<T>): Promise<T> {
    const role: ViewerRole = {
      gate: this,
      kind: 'viewer',
      generation: this.generation,
      active: true,
    }
    try {
      return await roles.run(role, work)
    } finally {
      role.active = false
    }
  }
  async runAgent<T>(work: () => Promise<T>, signal?: AbortSignal): Promise<T> {
    if (roles.getStore()?.gate === this) throw new Error('Viewer agent steps cannot be nested')
    await this.wait(() => this.mode === 'agent', signal)
    signal?.throwIfAborted()
    this.processSignal.throwIfAborted()
    // No await between admission and increment: takeover cannot miss this step.
    if (this.mode !== 'agent') return this.runAgent(work, signal)
    const role: ViewerRole = {
      gate: this,
      kind: 'agent',
      generation: this.generation,
      active: true,
    }
    ++this.activeSteps
    try {
      return await roles.run(role, work)
    } finally {
      role.active = false
      --this.activeSteps
      for (const change of this.changes) change()
    }
  }
  drain(signal: AbortSignal): Promise<void> {
    return this.wait(() => this.activeSteps === 0 && this.requests === 0, signal)
  }
  async trackRequest<T>(work: () => Promise<T>): Promise<T> {
    ++this.requests
    try {
      return await work()
    } finally {
      --this.requests
      for (const change of this.changes) change()
    }
  }
  private wait(ready: () => boolean, signal?: AbortSignal): Promise<void> {
    signal?.throwIfAborted()
    this.processSignal.throwIfAborted()
    if (this.mode === 'closed') return Promise.reject(new Error('Browser viewer is closed'))
    if (ready()) return Promise.resolve()
    if (this.changes.size >= 64)
      return Promise.reject(new Error('Too many waiting viewer agent steps'))
    return new Promise((resolve, reject) => {
      const cleanup = (): void => {
        this.changes.delete(check)
        signal?.removeEventListener('abort', check)
        this.processSignal.removeEventListener('abort', check)
      }
      const check = (): void => {
        if (signal?.aborted || this.processSignal.aborted || this.mode === 'closed') {
          cleanup()
          reject(
            toError(
              (signal?.reason ??
                this.processSignal.reason ??
                new Error('Browser viewer is closed')) as UntrustedInput,
            ),
          )
        } else if (ready()) {
          cleanup()
          resolve()
        }
      }
      this.changes.add(check)
      signal?.addEventListener('abort', check, { once: true })
      this.processSignal.addEventListener('abort', check, { once: true })
      check()
    })
  }
  dispose(): void {
    this.transition('closed')
    if (gates.get(this.processSignal) === this) gates.delete(this.processSignal)
  }
}
