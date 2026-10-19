// SPDX-License-Identifier: MIT
import { AsyncLocalStorage } from 'node:async_hooks'

import { type NativeLocatorHandlerStage } from './native-protocol.js'

/** Actual action-admission context, retained through native response+retirement. */
export class NativeLocatorHandlerContexts {
  private readonly listeners = new Map<number, Set<(event: NativeLocatorHandlerStage) => void>>()
  private readonly actions = new Map<
    number,
    {
      page: number
      run: ReturnType<typeof AsyncLocalStorage.snapshot>
      generation?: number
      raw: boolean
      tokens: Set<number>
      resolve(): void
    }
  >()
  subscribe(page: number, callback: (event: NativeLocatorHandlerStage) => void): () => void {
    let values = this.listeners.get(page)
    if (!values) {
      values = new Set()
      this.listeners.set(page, values)
    }
    values.add(callback)
    return () => {
      values.delete(callback)
      if (!values.size) this.listeners.delete(page)
    }
  }
  capture(id: number, page: number): Promise<void> | undefined {
    if (!this.listeners.get(page)?.size) return
    if (this.actions.size >= 256)
      throw new Error('Native locator handler retained action capacity exceeded')
    let resolve!: () => void
    const lifetime = new Promise<void>((accept) => {
      resolve = accept
    })
    this.actions.set(id, {
      page,
      run: AsyncLocalStorage.snapshot(),
      raw: false,
      tokens: new Set(),
      resolve,
    })
    return lifetime
  }
  response(id: number): void {
    const value = this.actions.get(id)
    if (!value) return
    value.raw = true
    if (!value.tokens.size) {
      this.actions.delete(id)
      value.resolve()
    }
  }
  receive(page: number, event: NativeLocatorHandlerStage): void {
    const value = this.actions.get(event.id)
    if (
      value?.page !== page ||
      (value.generation !== undefined && value.generation !== event.generation)
    )
      throw new Error('Native locator handler has no actual admitted action context')
    value.generation = event.generation
    const callbacks = this.listeners.get(page)
    if (!callbacks?.size) throw new Error('Native locator handler has no actual callback receiver')
    if (event.stage === 'locatorHandlerRun') {
      if (value.raw || value.tokens.has(event.token))
        throw new Error('Native locator handler replayed invocation')
      value.tokens.add(event.token)
      value.run(() => {
        for (const callback of [...callbacks]) callback(event)
      })
      return
    }
    if (!value.tokens.has(event.token))
      throw new Error('Native locator handler retirement has no actual invocation')
    for (const callback of [...callbacks]) callback(event)
    if (event.stage === 'locatorHandlerRetired') {
      value.tokens.delete(event.token)
      if (value.raw && !value.tokens.size) {
        this.actions.delete(event.id)
        value.resolve()
      }
    }
  }
  closed(): void {
    for (const value of this.actions.values()) value.resolve()
    this.actions.clear()
    this.listeners.clear()
  }
}
