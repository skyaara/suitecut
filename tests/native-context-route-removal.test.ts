import { describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { type NativeContextRouteMutation } from '../src/native-context-route-protocol.js'
import { NativeContextRoutes } from '../src/native-context-routes.js'
import { nativeRouteHandlerInvoked } from '../src/native-route-handler-observations.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred() {
  let resolve: (value: UntrustedInput) => void = () => undefined
  let reject: (error: Error) => void = () => undefined
  const promise = new Promise<UntrustedInput>((done, fail) => {
    resolve = done
    reject = fail
  })
  return { promise, resolve, reject }
}
const Removal = z.object({ mutationId: z.string(), action: z.literal('remove') })
const Registration = z.object({ registrationId: z.string() })
function fixture() {
  let listener: ((record: NativeContextRouteMutation) => void) | undefined
  const command = vi
    .fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
    .mockResolvedValue(undefined)
  const routes = new NativeContextRoutes(command, (receive) => {
    listener = receive
    return () => {
      listener = undefined
    }
  })
  const removal = () =>
    Removal.parse(command.mock.calls.findLast(([params]) => params.action === 'remove')?.[0])
  const publish = (
    phase: NativeContextRouteMutation['phase'],
    ids: string[],
    mutationId = removal().mutationId,
  ) => {
    listener?.({ contextId: 1, mutationId, phase, registrationIds: ids })
  }
  return { routes, command, publish, removal }
}

describe('native context removal admission and ACK receipts', () => {
  it('releases handler capacity at actual admission while waiting through the original native operation', async () => {
    const f = fixture(),
      ids: string[] = []
    for (let index = 0; index < 128; ++index) {
      await f.routes.route('**/*', vi.fn())
      ids.push(Registration.parse(f.command.mock.calls.at(-1)?.[0]).registrationId)
    }
    const held = deferred()
    f.command.mockImplementation((params) =>
      params.action === 'remove' ? held.promise : Promise.resolve(undefined),
    )
    let completed = false
    const removing = f.routes.unrouteAll({ behavior: 'wait' }).then(() => {
      completed = true
    })
    expect(f.command).toHaveBeenLastCalledWith(
      expect.objectContaining({ behavior: 'wait', registrationIds: [] }),
    )
    await expect(f.routes.route('**/new', vi.fn())).rejects.toThrow('capacity')
    f.publish('admitted', ids)
    f.publish('applied', ids)
    await f.routes.route('**/new', vi.fn())
    expect(completed).toBe(false)
    held.resolve(undefined)
    await removing
    expect(completed).toBe(true)
    f.routes.close()
  })

  it('retains registered handlers when removal is rejected before actual admission', async () => {
    const f = fixture(),
      handler = vi.fn()
    await f.routes.route('**/*', handler)
    const id = Registration.parse(f.command.mock.calls[0]?.[0]).registrationId
    f.command.mockRejectedValueOnce(new Error('actual policy removal rejected'))
    await expect(f.routes.unrouteAll({ behavior: 'ignoreErrors' })).rejects.toThrow(
      'actual policy removal rejected',
    )
    expect(f.routes.resolve(id, false)).toBe(handler)
    const old = f.removal().mutationId
    f.publish('admitted', [id], old)
    expect(f.routes.resolve(id, false)).toBe(handler)
    f.routes.close()
  })

  it('does not resurrect native-deactivated handlers after a later real ACK failure', async () => {
    const f = fixture(),
      handler = vi.fn()
    await f.routes.route('**/*', handler)
    const id = Registration.parse(f.command.mock.calls[0]?.[0]).registrationId,
      held = deferred()
    f.command.mockReturnValueOnce(held.promise)
    const removing = f.routes.unrouteAll({ behavior: 'ignoreErrors' })
    const failed = expect(removing).rejects.toThrow('actual target channel closed')
    f.publish('admitted', [id])
    held.reject(new Error('actual target channel closed'))
    await failed
    expect(f.routes.resolve(id, false)).toBeUndefined()
    f.routes.close()
  })

  it('requires genuine admission and matching ACK identities rather than command success alone', async () => {
    const f = fixture(),
      held = deferred()
    await f.routes.route('**/*', vi.fn())
    const id = Registration.parse(f.command.mock.calls[0]?.[0]).registrationId
    f.command.mockReturnValueOnce(held.promise)
    const removing = f.routes.unrouteAll({ behavior: 'wait' })
    expect(() => f.publish('applied', [id])).toThrow('actual admission')
    f.publish('admitted', [id])
    expect(() => f.publish('applied', ['another-registration'])).toThrow('actual admission')
    f.publish('applied', [id])
    held.resolve(undefined)
    await removing
    await expect(f.routes.unrouteAll({ behavior: 'wait' })).rejects.toThrow(
      'without its actual policy mutation receipts',
    )
    f.routes.close()
  })

  it('does not credit native completion as host settlement and releases the observer on close', async () => {
    const f = fixture(),
      native = deferred(),
      host = deferred()
    await f.routes.route('**/*', vi.fn())
    const id = Registration.parse(f.command.mock.calls[0]?.[0]).registrationId
    const wrapped = f.routes.resolveInvocation(id, true)
    expect(wrapped).toBeDefined()
    if (!wrapped) throw new Error('Actual invocation missing')
    nativeRouteHandlerInvoked(
      wrapped,
      host.promise.then(() => undefined),
    )
    f.command.mockReturnValueOnce(native.promise)
    let completed = false
    const removing = f.routes.unrouteAll({ behavior: 'wait' }).then(() => {
      completed = true
    })
    const rejected = expect(removing).rejects.toThrow('closed before removal observer')
    f.publish('admitted', [id])
    f.publish('applied', [id])
    native.resolve(undefined)
    await Promise.resolve()
    await Promise.resolve()
    expect(completed).toBe(false)
    f.routes.close()
    await rejected
    // Actual host receipt still belongs to its invocation after observer close.
    host.resolve(undefined)
    await host.promise
  })

  it('retains original command failure precedence when logical context close precedes observer delivery', async () => {
    const f = fixture(),
      held = deferred()
    f.command.mockReturnValueOnce(held.promise)
    const removing = f.routes.unrouteAll({ behavior: 'wait' })
    const failed = expect(removing).rejects.toThrow('original native deadline')
    f.routes.close()
    held.reject(new Error('original native deadline'))
    await failed
    const second = fixture(),
      late = deferred()
    second.command.mockReturnValueOnce(late.promise)
    const stopped = second.routes.unrouteAll({ behavior: 'wait' })
    const closed = expect(stopped).rejects.toThrow('closed before removal observer')
    second.publish('admitted', [])
    second.publish('applied', [])
    second.routes.close()
    late.resolve(undefined)
    await closed
  })

  it('rejects receipt-dependent behavior on old transports before issuing a mutation', async () => {
    const command = vi
      .fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
      .mockResolvedValue(undefined)
    const routes = new NativeContextRoutes(command)
    await expect(routes.unrouteAll({ behavior: 'wait' })).rejects.toThrow(
      'receipts are unavailable',
    )
    await expect(routes.unrouteAll({ behavior: 'ignoreErrors' })).rejects.toThrow(
      'receipts are unavailable',
    )
    expect(command).not.toHaveBeenCalled()
  })
})
