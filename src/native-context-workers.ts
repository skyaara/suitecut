// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { acknowledgeNativeConsoleEvent } from './native-console.js'
import { type NativeContextConsoleInfo } from './native-context-console-protocol.js'
import { type NativeEventPredicateSnapshot } from './native-event-predicate.js'
import { type NativeConsoleMessage, type NativeWorkerConsoleOwner } from './native-page.js'
import { type NativeConsoleInfo } from './native-protocol.js'
import { type NativeWorkerEvent, type NativeWorkerInfo } from './native-worker-protocol.js'
import { type NativeWorkerRetirementSnapshot } from './native-worker-retirements.js'
import { NativeWorkerEvents, type NativeWorker, type NativeWorkerBrowser } from './native-worker.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeContextWorkerTransport {
  signal: AbortSignal
  processSignal?: AbortSignal | undefined
  processClosed?: Promise<void> | undefined
  processExit?: NativeWorkerBrowser['processExit']
  runContext(
    action: string,
    params?: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  onContextConsole?(listener: (info: NativeContextConsoleInfo) => void): () => void
  onContextWorkerPredicate?(
    listener: (record: NativeEventPredicateSnapshot & { contextId: number }) => void,
  ): () => void
  contextWorkerInfos?(context: number): NativeWorkerInfo[]
  contextWorkerRetirements?(context: number): NativeWorkerRetirementSnapshot
  contextWorkerSequence?(context: number): number
  onContextWorkerMessage?(
    listener: (record: { contextId: number; workerEvent: NativeWorkerEvent }) => void,
  ): () => void
}
export type NativeContextWorkerKind = 'service_worker' | 'shared_worker'
/** This adapter represents a native context worker channel; no Page fields exist. */
export class NativeContextWorkerEvents {
  private readonly source: NativeWorkerBrowser
  private readonly workers: NativeWorkerEvents
  private readonly listeners = new Map<
    NativeContextWorkerKind,
    ((worker: NativeWorker) => void)[]
  >()
  constructor(
    transport: NativeContextWorkerTransport,
    private readonly contextId: number,
    signal: AbortSignal,
    private readonly defaultTimeout: () => number,
    isPageHandle: (value: UntrustedInput) => boolean,
    private readonly createConsole: (
      owner: NativeWorkerConsoleOwner,
      info: NativeConsoleInfo,
    ) => NativeConsoleMessage,
  ) {
    if (
      !transport.onContextWorkerMessage ||
      !transport.contextWorkerInfos ||
      !transport.onContextConsole ||
      !transport.onContextWorkerPredicate
    )
      throw new Error('Native context worker channel is unavailable')
    const workerInfos = transport.contextWorkerInfos.bind(transport)
    const onWorkerMessage = transport.onContextWorkerMessage.bind(transport)
    const onConsole = transport.onContextConsole.bind(transport)
    const onPredicate = transport.onContextWorkerPredicate.bind(transport)
    this.source = {
      signal,
      processSignal: transport.processSignal ?? transport.signal,
      ...(transport.processClosed ? { processClosed: transport.processClosed } : {}),
      processExit: () => transport.processExit?.(),
      workerRetirements: () =>
        transport.contextWorkerRetirements?.(contextId) ?? { records: [], truncated: false },
      runAutomation: (command, params = {}, actionSignal) => {
        if (command === 'worker')
          return transport.runContext('worker', { contextId, ...params }, actionSignal)
        if (command === 'console')
          return transport.runContext(
            'worker',
            { contextId, action: 'console', params },
            actionSignal,
          )
        if (
          ![
            'claimConsoleArguments',
            'acknowledgeConsole',
            'waitEventPredicate',
            'continueEventPredicate',
          ].includes(command)
        )
          throw new Error('Unsupported native context worker command')
        return transport.runContext(
          'worker',
          { contextId, ...params, action: command },
          actionSignal,
        )
      },
      onEventPredicate: (listener) =>
        onPredicate((record) => {
          if (record.contextId === contextId) listener(record)
        }),
      onConsoleMessage: (listener) =>
        onConsole((record) => {
          if (record.contextId === contextId && record.pageId === null) {
            listener(record.console)
            acknowledgeNativeConsoleEvent(this.source, record.console.sequence ?? 0)
          }
        }),
      workerInfos: () => workerInfos(contextId),
      eventSequence: () => transport.contextWorkerSequence?.(contextId) ?? 0,
      onWorkerMessage: (listener) =>
        onWorkerMessage((record) => {
          if (record.contextId === contextId) listener(record.workerEvent)
        }),
    }
    this.workers = NativeWorkerEvents.forOwner({
      source: this.source,
      runWorker: (params, actionSignal) =>
        this.source.runAutomation('worker', params, actionSignal),
      defaultTimeout,
      isPageHandle,
      createConsole: (info) => this.consoleFromInfo(info),
    })
    const detach = this.source.onWorkerMessage?.((event) => {
      if (
        event.event !== 'worker' ||
        (event.worker.type !== 'service_worker' && event.worker.type !== 'shared_worker')
      )
        return
      const worker = this.workers.fromRecord(event)
      for (const listener of [...(this.listeners.get(event.worker.type) ?? [])]) listener(worker)
    })
    const closed = () => {
      detach?.()
      this.listeners.clear()
    }
    signal.addEventListener('abort', closed, { once: true })
    // Lazy projection of a late network receipt can create this adapter after
    // context close. An already-aborted signal will not dispatch again.
    if (signal.aborted) {
      signal.removeEventListener('abort', closed)
      closed()
    }
  }
  consoleFromInfo(info: NativeConsoleInfo): NativeConsoleMessage {
    if (!info.workerInfo || info.workerInfo.id !== info.worker)
      throw new Error('Native context worker console has no actual target identity')
    return this.createConsole(
      { source: this.source, workerFromConsole: (value) => this.workers.fromConsole(value) },
      info,
    )
  }
  fromObservedInfo(info: NativeWorkerInfo): NativeWorker {
    return this.workers.fromObservedInfo(info)
  }
  list(kind: NativeContextWorkerKind): NativeWorker[] {
    return this.workers.allTargets().filter((worker) => worker.info.type === kind)
  }
  async wait(
    kind: NativeContextWorkerKind,
    options: { timeout?: number | undefined; signal?: AbortSignal | undefined } = {},
  ): Promise<NativeWorker> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
      })
      .parse(options)
    this.source.signal.throwIfAborted()
    const record = await this.source.runAutomation(
      'worker',
      {
        contextId: this.contextId,
        action: 'waitEvent',
        kind,
        afterSequence: this.source.eventSequence?.() ?? 0,
        timeoutMs: config.timeout ?? this.defaultTimeout(),
      },
      config.signal,
    )
    return this.workers.fromRecord(record)
  }
  on(kind: NativeContextWorkerKind, listener: (worker: NativeWorker) => void): void {
    this.source.signal.throwIfAborted()
    let listeners = this.listeners.get(kind)
    if (!listeners) {
      listeners = []
      this.listeners.set(kind, listeners)
    }
    listeners.push(listener)
  }
  off(kind: NativeContextWorkerKind, listener: (worker: NativeWorker) => void): void {
    const listeners = this.listeners.get(kind)
    if (!listeners) return
    const index = listeners.lastIndexOf(listener)
    if (index >= 0) listeners.splice(index, 1)
  }
}
