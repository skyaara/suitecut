// SPDX-License-Identifier: MIT
import { expect, it } from 'vitest'

import { NativeContextRoutes } from '../src/native-context-routes.js'
import { NativeRouteURLMatcherPolicy } from '../src/native-route-url-matcher.js'
import { type UntrustedInput } from '../src/untrusted.js'

it('preserves a registration admitted after a default removal selection', async () => {
  const commands: Record<string, UntrustedInput>[] = []
  let acknowledge = () => undefined as void
  const routes = new NativeContextRoutes(async (params) => {
    commands.push(params)
    if (params.action === 'remove')
      await new Promise<void>((done) => {
        acknowledge = done
      })
    return undefined
  })
  const first = () => undefined,
    later = () => undefined
  await routes.route('**/first', first)
  const removal = routes.unrouteAll()
  await routes.route('**/later', later)
  const firstId = commands[0]!.registrationId as string,
    laterId = commands[2]!.registrationId as string
  expect(commands[1]).toEqual({ action: 'remove', registrationIds: [firstId] })
  acknowledge()
  await removal
  expect(routes.resolve(firstId, false)).toBeUndefined()
  expect(routes.resolve(laterId, false)).toBe(later)
  routes.close()
})

it('keeps regexp callbacks independent of author state and of each other', () => {
  const original = /wire/gy
  original.lastIndex = 5
  original.test = () => {
    throw new Error('author test must not execute')
  }
  const policy = new NativeRouteURLMatcherPolicy(original)
  const first = policy.callback(),
    second = policy.callback()
  for (let i = 0; i < 1000; ++i) {
    expect(first('wire')).toEqual({ matched: true })
    expect(first('other')).toEqual({ matched: false })
    expect(second('wire')).toEqual({ matched: true })
  }
  expect(original.lastIndex).toBe(5)
  policy.dispose()
  expect(first('wire')).toEqual({ matched: true })
})
