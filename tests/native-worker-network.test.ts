import { describe, expect, it, vi } from 'vitest'

import { NativeContextNetworkInfoSchema } from '../src/native-context-network-protocol.js'
import { NativeNetwork, NativeRequest, NativeResponse } from '../src/native-network.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

const info = {
  id: 'worker-request:1',
  requestId: 'actual-request',
  session: 'actual-broker-session',
  frameId: '',
  url: 'https://worker.test/request',
  method: 'POST',
  resourceType: 'Fetch',
  headers: { 'content-type': 'application/octet-stream' },
  isNavigationRequest: false,
  serviceWorker: true,
  postDataBodyId: '8',
  postDataOwner: 'broker:6',
}
const response = {
  url: info.url,
  status: 200,
  statusText: 'OK',
  frameId: '',
  method: 'POST',
  headers: { 'content-type': 'application/octet-stream' },
  session: info.session,
  requestId: info.requestId,
}

describe('native context worker request projection', () => {
  it('keeps canonical request and response identity on a genuine owner channel without Page or Frame', async () => {
    const lifetime = new AbortController()
    let receive: ((event: NativeNetworkInfo) => void) | undefined
    const command = vi.fn(() => Promise.resolve({} as UntrustedInput))
    const body = vi.fn(() =>
      Promise.resolve({ body: 'AP8B', base64Encoded: true } as UntrustedInput),
    )
    const takeBody = vi.fn(() => Buffer.from([0, 255, 13, 10]))
    const network = new NativeNetwork(undefined, undefined, {
      signal: lifetime.signal,
      defaultTimeout: () => 321,
      command,
      body,
      takeBody,
      subscribe: (listener) => {
        receive = listener
        return () => {
          receive = undefined
        }
      },
    })
    const event = {
      event: 'request' as const,
      sequence: 1,
      request: { ...info },
    }
    receive?.(event)
    const request = network.eventValue('request', event)
    expect(request).toBeInstanceOf(NativeRequest)
    if (!(request instanceof NativeRequest)) throw new Error('Missing actual Request')
    expect(request.postDataBuffer()).toEqual(Buffer.from([0, 255, 13, 10]))
    expect(takeBody).toHaveBeenCalledWith(
      '8',
      expect.objectContaining({ postDataOwner: 'broker:6' }),
    )
    expect(() => request.frame()).toThrow('service worker')
    receive?.({
      event: 'response',
      sequence: 2,
      request: { ...info },
      response,
    })
    const observed = network.eventValue('response', {
      event: 'response',
      sequence: 2,
      request: { ...info },
      response,
    })
    expect(observed).toBeInstanceOf(NativeResponse)
    if (!(observed instanceof NativeResponse)) throw new Error('Missing actual Response')
    expect(observed.request()).toBe(request)
    expect(request.existingResponse()).toBe(observed)
    expect(await observed.body()).toEqual(Buffer.from([0, 255, 1]))
    expect(body).toHaveBeenCalledWith(
      expect.objectContaining({ requestId: 'actual-request' }),
      request,
    )
    expect(await observed.body()).toEqual(Buffer.from([0, 255, 1]))
    expect(body).toHaveBeenCalledTimes(1)
    lifetime.abort()
    expect(receive).toBeUndefined()
    expect(network.metadataDiagnostics().cachedRequests).toBe(0)
    expect(request.postDataBuffer()).toEqual(Buffer.from([0, 255, 13, 10]))
  })
  it('retains actual response metadata when a native context wait first observes requestfinished', () => {
    const lifetime = new AbortController()
    const network = new NativeNetwork(undefined, undefined, {
      signal: lifetime.signal,
      defaultTimeout: () => 321,
      command: () => Promise.resolve(undefined),
      body: () => Promise.resolve(undefined),
      takeBody: () => Buffer.from([0, 255]),
    })
    const request = network.eventValue('requestfinished', {
      event: 'requestfinished',
      sequence: 3,
      request: info,
      response,
    })
    expect(request).toBeInstanceOf(NativeRequest)
    if (!(request instanceof NativeRequest)) throw new Error('Missing actual Request')
    expect(request.existingResponse()?.request()).toBe(request)
    expect(request.existingResponse()?.status()).toBe(200)
    lifetime.abort()
  })
  it('requires exactly a genuine Page or worker owner in context records', () => {
    const base = {
      contextId: 1,
      sequence: 1,
      network: { event: 'request', sequence: 1, request: info },
    }
    const worker = {
      id: 'actual-target',
      url: 'https://worker.test/sw.js',
      type: 'service_worker',
    }
    expect(NativeContextNetworkInfoSchema.safeParse({ ...base, worker }).success).toBe(true)
    expect(NativeContextNetworkInfoSchema.safeParse({ ...base, pageId: 1 }).success).toBe(true)
    expect(NativeContextNetworkInfoSchema.safeParse({ ...base, pageId: 1, worker }).success).toBe(
      false,
    )
    expect(NativeContextNetworkInfoSchema.safeParse(base).success).toBe(false)
    const update = {
      ...base,
      network: {
        event: 'requestupdated',
        sequence: 2,
        request: { ...info, postDataBodyId: '23', postDataOwner: 'broker:2' },
      },
    }
    expect(NativeContextNetworkInfoSchema.safeParse({ ...update, worker }).success).toBe(true)
    expect(NativeContextNetworkInfoSchema.safeParse({ ...update, pageId: 1 }).success).toBe(false)
    expect(
      NativeContextNetworkInfoSchema.safeParse({
        ...update,
        worker,
        network: {
          ...update.network,
          request: { ...info, postDataBodyId: undefined, postDataOwner: undefined },
        },
      }).success,
    ).toBe(false)
  })
})
