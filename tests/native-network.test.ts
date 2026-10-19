import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { type NativeEventPredicateSnapshot } from '../src/native-event-predicate.js'
import { hasNativeAPIResponseReceipt, NativeAPIResponse } from '../src/native-http.js'
import { NativeNetwork, NativeRequest, NativeRoute } from '../src/native-network.js'
import { NativePage } from '../src/native-page.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  let listener: ((event: NativeNetworkInfo) => void) | undefined
  let predicateListener: ((event: NativeEventPredicateSnapshot) => void) | undefined
  let frames = [{ id: 'root', parentId: '', name: '', url: 'https://native.test/' }]
  const commands: Record<string, UntrustedInput>[] = []
  const execute = vi.fn((command: string, params: Record<string, UntrustedInput> = {}) => {
    commands.push({ command, ...params })
    return Promise.resolve({} as UntrustedInput)
  })
  const controller = new AbortController()
  const source: NativeBrowser = {
    state: 'running',
    url: 'https://native.test/',
    signal: controller.signal,
    pixelFormat: 'bgra',
    width: 2,
    height: 2,
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation: execute,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => frames,
    onNetworkMessage: (fn) => {
      listener = fn
      return () => {
        listener = undefined
      }
    },
    networkSequence: () => 9,
    eventSequence: () => 77,
    onEventPredicate: (fn) => {
      predicateListener = fn
      return () => {
        predicateListener = undefined
      }
    },
    close: () => {
      controller.abort()
      return Promise.resolve()
    },
  }
  const owner = new NativePage(source),
    network = new NativeNetwork(owner)
  const info = {
    id: '1',
    requestId: 'cef-1',
    session: '',
    frameId: 'root',
    url: 'https://native.test/data',
    method: 'POST',
    resourceType: 'Fetch',
    headers: { 'content-type': 'application/json' },
    postData: '{"hello":"world"}',
    isNavigationRequest: false,
  }
  return {
    owner,
    source,
    network,
    info,
    execute,
    commands,
    emit: (event: NativeNetworkInfo) => listener?.(event),
    emitPredicate: (event: NativeEventPredicateSnapshot) => predicateListener?.(event),
    detachFrame: () => {
      frames = []
    },
  }
}

describe('native network ownership and transport', () => {
  it('fetches without resolving, retains the native response receipt, then fulfills modified bytes', async () => {
    const f = fixture()
    f.execute.mockImplementation((command, params = {}) => {
      f.commands.push({ command, ...params })
      if (command === 'http' && params.action === 'create')
        return Promise.resolve({ apiContextId: 7, browserContextId: 9 })
      if (params.action === 'fetch')
        return Promise.resolve({
          responseId: 'actual-fetch',
          apiContextId: 7,
          browserContextId: 9,
          url: 'https://native.test/upstream',
          method: 'POST',
          status: 201,
          statusText: 'Created',
          headers: [{ name: 'content-type', value: 'application/json' }],
        })
      if (command === 'http' && params.action === 'body')
        return Promise.resolve({ body: Buffer.from('{"original":true}').toString('base64') })
      return Promise.resolve({})
    })
    const route = new NativeRoute(
      f.network,
      'actual-pause',
      new NativeRequest(f.network, f.info),
      undefined,
      '123',
    )
    const response = await route.fetch({
      url: 'https://native.test/upstream',
      timeout: 0,
      maxRedirects: 0,
      maxRetries: 2,
      postData: Buffer.from([0, 255]),
    })
    expect(f.commands).toHaveLength(1)
    expect(f.commands[0]?.command).toBe('network')
    expect(response).toBeInstanceOf(NativeAPIResponse)
    expect(hasNativeAPIResponseReceipt(response)).toBe(true)
    expect(response.status()).toBe(201)
    expect(await response.json()).toEqual({ original: true })
    expect(route.isHandled()).toBe(false)
    expect(f.commands.find((entry) => entry.action === 'fetch')).toMatchObject({
      command: 'network',
      action: 'fetch',
      routeId: 'actual-pause',
      postData: 'AP8=',
      callbackScope: '123',
      timeoutMs: 0,
      maxRedirects: 0,
      maxRetries: 2,
    })
    await route.fulfill({ response, json: { modified: true } })
    expect(f.commands.at(-1)).toMatchObject({
      action: 'fulfill',
      status: 201,
      body: Buffer.from('{"modified":true}').toString('base64'),
    })
    expect(route.isHandled()).toBe(true)
    await response.dispose()
    expect(hasNativeAPIResponseReceipt(response)).toBe(false)
    await expect(route.fetch()).rejects.toThrow('already handled')
  })
  it('carries the original fetch options on the first native large-body admission', async () => {
    const f = fixture()
    f.execute.mockImplementation((command, params = {}) => {
      f.commands.push({ command, ...params })
      if (params.action === 'fetch')
        return Promise.resolve({
          responseId: 'large',
          apiContextId: 7,
          browserContextId: 9,
          url: f.info.url,
          method: 'POST',
          status: 200,
          statusText: 'OK',
          headers: [],
        })
      return Promise.resolve({})
    })
    const body = Buffer.alloc(48 * 1024 + 3, 255)
    const route = new NativeRoute(
      f.network,
      'actual-pause',
      new NativeRequest(f.network, f.info),
      undefined,
      '123',
    )
    await route.fetch({ postData: body, timeout: 100 })
    expect(f.commands[0]).toMatchObject({
      action: 'resetBody',
      callbackScope: '123',
      fetchOptions: { action: 'fetch', routeId: 'actual-pause', timeoutMs: 100 },
    })
    const chunks = f.commands
      .filter((entry) => entry.action === 'bodyChunk')
      .map((entry) => Buffer.from(entry.body as string, 'base64'))
    expect(Buffer.concat(chunks)).toEqual(body)
    expect(f.commands.at(-1)).toMatchObject({ action: 'fetch', bodyUpload: true, timeoutMs: 100 })
    expect(route.isHandled()).toBe(false)
  })
  it('rejects malformed native response metadata without consuming the pause', async () => {
    const f = fixture()
    f.execute.mockImplementation((command, params = {}) => {
      if (command === 'http' && params.action === 'create')
        return Promise.resolve({ apiContextId: 7, browserContextId: 9 })
      return Promise.resolve({
        responseId: 'foreign',
        apiContextId: 8,
        browserContextId: 0,
        url: f.info.url,
        status: 200,
        statusText: 'OK',
        headers: [],
      })
    })
    const route = new NativeRoute(f.network, 'actual-pause', new NativeRequest(f.network, f.info))
    await expect(route.fetch()).rejects.toThrow()
    expect(route.isHandled()).toBe(false)
    await expect(route.fetch({ maxRetries: 101 })).rejects.toThrow()
    await expect(route.fetch({ timeout: -1 })).rejects.toThrow()
  })

  it('exposes only observed server/TLS details and returns detached API values', async () => {
    const f = fixture(),
      request = new NativeRequest(f.network, f.info)
    const basic = {
      url: f.info.url,
      status: 200,
      statusText: 'OK',
      headers: {},
      requestId: 'cef-1',
      session: '',
      frameId: 'root',
      method: 'POST',
    }
    const absent = f.network.createResponse(request, basic)
    expect(await absent.serverAddr()).toBeNull()
    expect(await absent.securityDetails()).toBeNull()
    const details = {
      issuer: 'Observed Certificate Authority',
      subjectName: 'native.test',
      protocol: 'TLS 1.3',
      validFrom: 1_700_000_000,
      validTo: 2_200_000_000,
    }
    const response = f.network.createResponse(request, {
      ...basic,
      serverAddr: { ipAddress: '::1', port: 443 },
      securityDetails: details,
    })
    expect(await response.serverAddr()).toEqual({ ipAddress: '::1', port: 443 })
    expect(await response.securityDetails()).toEqual(details)
    const address = await response.serverAddr(),
      security = await response.securityDetails()
    if (!address || !security) throw new Error('Observed metadata was lost')
    address.port = 1
    security.issuer = 'caller mutation'
    expect((await response.serverAddr())?.port).toBe(443)
    expect((await response.securityDetails())?.issuer).toBe(details.issuer)
    expect(() =>
      f.network.createResponse(request, {
        ...basic,
        serverAddr: { ipAddress: '::1', port: 65536 },
      }),
    ).toThrow()
  })

  it('keeps native routing retryable after actual command rejection and locks concurrent handling', async () => {
    const f = fixture(),
      route = new NativeRoute(f.network, 'native-route', new NativeRequest(f.network, f.info))
    f.execute.mockRejectedValueOnce(new Error('native validation rejected'))
    await expect(
      route.fallback({ method: 'POST', postData: Buffer.from([0, 255, 13, 10]) }),
    ).rejects.toThrow('validation')
    expect(route.isHandled()).toBe(false)
    await route.fallback({ method: 'POST', postData: Buffer.from([0, 255, 13, 10]) })
    expect(f.commands.at(-1)).toMatchObject({
      action: 'fallback',
      routeId: 'native-route',
      postData: 'AP8NCg==',
    })
    await expect(route.continue()).rejects.toThrow('already handled')
    const concurrent = new NativeRoute(
      f.network,
      'concurrent-route',
      new NativeRequest(f.network, f.info),
    )
    let release: ((value: UntrustedInput) => void) | undefined
    f.execute.mockImplementationOnce(
      () =>
        new Promise<UntrustedInput>((resolve) => {
          release = resolve
        }),
    )
    const handling = concurrent.continue()
    await expect(concurrent.abort()).rejects.toThrow('already handled')
    if (!release) throw new Error('Expected actual pending native command')
    release({})
    await handling
  })
  it('uses exact native binary post bytes and clears replaced body representations', () => {
    const f = fixture(),
      request = new NativeRequest(f.network, { ...f.info, postDataBase64: 'AP8NCg==' })
    expect(request.postDataBuffer()).toEqual(Buffer.from([0, 255, 13, 10]))
    request.observe({ ...f.info, postData: 'actual replacement' }, 1)
    expect(request.postData()).toBe('actual replacement')
    request.observe({ ...f.info, postDataUnavailable: true }, 2)
    expect(() => request.postDataBuffer()).toThrow('capacity')
  })
  it('falls back through the native frozen match plan if a queued handler was removed', async () => {
    const f = fixture()
    f.emit({
      event: 'route',
      sequence: 10,
      request: f.info,
      route: {
        id: 'actual-pause',
        registrationId: 'removed',
        exhausted: false,
        callbackScope: '1',
        callbackToken: '1',
      },
    })
    await vi.waitFor(() =>
      expect(
        f.commands.some(
          (command) => command.action === 'fallback' && command.routeId === 'actual-pause',
        ),
      ).toBe(true),
    )
  })

  it('keeps asynchronous network predicates inside one native journal owner and deadline', async () => {
    const f = fixture()
    let finish: ((value: UntrustedInput) => void) | undefined
    f.execute.mockImplementation((command, params = {}) => {
      f.commands.push({ command, ...params })
      if (command === 'waitEventPredicate')
        return new Promise<UntrustedInput>((resolve) => {
          finish = resolve
        })
      if (command === 'continueEventPredicate' && params.matches === true) finish?.({})
      return Promise.resolve({} as UntrustedInput)
    })
    const seen: NativeRequest[] = []
    const wait = f.network.waitForRequest(
      async (request) => {
        seen.push(request)
        await Promise.resolve()
        return request.url().endsWith('/target')
      },
      { timeout: 123 },
    )
    const started = f.commands[0]
    expect(started).toMatchObject({
      command: 'waitEventPredicate',
      event: 'request',
      timeoutMs: 123,
      afterSequence: 77,
    })
    const clientToken = started?.clientToken
    if (typeof clientToken !== 'string')
      throw new Error('Expected real native predicate client token')
    f.emitPredicate({
      id: 10,
      generation: 1,
      token: 1,
      clientToken,
      event: 'request',
      sequence: 78,
      value: { event: 'request', sequence: 10, request: f.info },
    })
    await vi.waitFor(() =>
      expect(
        f.commands.some(
          (command) => command.command === 'continueEventPredicate' && command.matches === false,
        ),
      ).toBe(true),
    )
    f.emitPredicate({
      id: 10,
      generation: 1,
      token: 2,
      clientToken,
      event: 'request',
      sequence: 79,
      value: {
        event: 'request',
        sequence: 11,
        request: { ...f.info, id: '2', url: 'https://native.test/target' },
      },
    })
    const result = await wait
    expect(result).toBe(seen[1])
    expect(seen).toHaveLength(2)
    expect(f.commands.filter((command) => command.command === 'waitEventPredicate')).toHaveLength(1)
    expect(result.postDataJSON()).toEqual({ hello: 'world' })
  })
  it('preserves request, response and detached-frame identity across network events', () => {
    const f = fixture(),
      received: NativeRequest[] = [],
      responses: UntrustedInput[] = []
    f.network.on('request', (request) => received.push(request))
    f.network.on('response', (response) => responses.push(response))
    f.emit({ event: 'request', sequence: 1, request: f.info })
    const response = {
      url: f.info.url,
      status: 200,
      statusText: 'OK',
      headers: { 'Set-Cookie': 'a=1\nb=2' },
      requestId: 'cef-1',
      session: '',
      frameId: 'root',
      method: 'POST',
    }
    f.detachFrame()
    f.emit({ event: 'response', sequence: 2, request: f.info, response })
    f.emit({ event: 'requestfinished', sequence: 3, request: f.info, response })
    expect(received).toHaveLength(1)
    expect(received[0]!.frame().identity()).toBe('root')
    expect(received[0]!.frame().isDetached()).toBe(true)
    expect(received[0]!.postDataJSON()).toEqual({ hello: 'world' })
    expect(responses).toHaveLength(1)
    const navigationResponse = f.network.navigationResponse({ ...response, headers: {} })
    expect(navigationResponse).toBe(responses[0])
    expect(navigationResponse.headers()['set-cookie']).toBe('a=1\nb=2')
    expect(received[0]!.existingResponse()).toBe(navigationResponse)
  })
  it('uses the native journal and deadline to wait for a response', async () => {
    const f = fixture()
    f.execute.mockResolvedValue({
      event: 'response',
      sequence: 10,
      request: f.info,
      response: {
        url: f.info.url,
        status: 201,
        statusText: 'Created',
        headers: {},
        requestId: 'cef-1',
        session: '',
        frameId: 'root',
        method: 'POST',
      },
    })
    const response = await f.network.waitForResponse('**/data', { timeout: 321 })
    expect(response.status()).toBe(201)
    expect(response.request().url()).toBe(f.info.url)
    expect(f.execute.mock.calls[0]?.[0]).toBe('network')
    expect(f.execute.mock.calls[0]?.[1]).toMatchObject({
      action: 'waitEvent',
      event: 'response',
      url: '**/data',
      afterSequence: 9,
      timeoutMs: expect.any(Number),
    })
    expect(f.execute.mock.calls[0]?.[1]?.timeoutMs).toBeLessThanOrEqual(321)
  })
  it('keeps newer native finish metadata when an older response wait resolves afterward', async () => {
    const f = fixture()
    const initialTiming = {
      startTime: 10,
      domainLookupStart: -1,
      domainLookupEnd: -1,
      connectStart: -1,
      secureConnectionStart: -1,
      connectEnd: -1,
      requestStart: 0,
      responseStart: 2,
      responseEnd: -1,
    }
    const oldInfo = { ...f.info, timing: initialTiming }
    const oldResponse = {
      url: f.info.url,
      status: 200,
      statusText: 'OK',
      headers: { 'x-native-state': 'old' },
      requestId: 'cef-1',
      session: '',
      frameId: 'root',
      method: 'POST',
    }
    f.emit({ event: 'request', sequence: 9, request: { ...oldInfo, timing: { ...initialTiming } } })
    f.execute.mockResolvedValue({
      event: 'response',
      sequence: 10,
      request: oldInfo,
      response: oldResponse,
    })
    const pending = f.network.waitForResponse('**/data')
    f.emit({
      event: 'requestfinished',
      sequence: 11,
      request: { ...oldInfo, timing: { ...initialTiming, responseEnd: 5 } },
      response: { ...oldResponse, headers: { 'x-native-state': 'new' } },
    })
    const response = await pending
    expect(response.request().timing().responseEnd).toBe(5)
    expect(response.headers()['x-native-state']).toBe('new')
    expect(await response.finished()).toBeNull()
  })
  it('uploads exact binary route bodies in bounded control chunks', async () => {
    const f = fixture(),
      request = new NativeRequest(f.network, f.info),
      route = new NativeRoute(f.network, 'pause-1', request)
    const body = Buffer.alloc(100_001)
    for (let index = 0; index < body.length; index++) body[index] = index % 256
    await route.fulfill({ body, contentType: 'application/octet-stream' })
    const uploads = f.commands.filter((command) => command.action === 'bodyChunk')
    expect(uploads).toHaveLength(5)
    const combined = Buffer.concat(
      uploads.map((command) =>
        Buffer.from(typeof command.body === 'string' ? command.body : '', 'base64'),
      ),
    )
    expect(combined.equals(body)).toBe(true)
    for (const command of uploads)
      expect(Buffer.byteLength(JSON.stringify(command))).toBeLessThan(65536)
    expect(f.commands.at(-1)).toMatchObject({
      action: 'fulfill',
      routeId: 'pause-1',
      bodyUpload: true,
      status: 200,
      headers: expect.arrayContaining([{ name: 'content-length', value: String(body.length) }]),
    })
    await expect(route.abort()).rejects.toThrow('already handled')
  })
  it('rejects unsafe header bytes before interception resolution', async () => {
    const f = fixture(),
      route = new NativeRoute(f.network, 'pause-1', new NativeRequest(f.network, f.info))
    await expect(route.continue({ headers: { test: 'first\r\nsecond' } })).rejects.toThrow(
      'newline',
    )
    expect(f.commands).toHaveLength(0)
    expect(route.isHandled()).toBe(false)
  })
  it('retains route-handler failures and aborts their real paused requests', async () => {
    const f = fixture()
    await f.network.route('**/data', () => {
      throw new Error('handler exploded')
    })
    const registration = f.commands[0]!.registrationId as string
    f.emit({
      event: 'route',
      sequence: 1,
      request: f.info,
      route: { id: 'paused', registrationId: registration, callbackScope: '1', callbackToken: '2' },
    })
    await vi.waitFor(() =>
      expect(f.commands.some((command) => command.action === 'abort')).toBe(true),
    )
    expect(f.network.takeRouteErrors().map((error) => error.message)).toEqual(['handler exploded'])
    expect(f.commands.at(-1)).toMatchObject({
      action: 'abort',
      routeId: 'paused',
      errorReason: 'Failed',
    })
  })
  it('evicts completed binding metadata by bytes while preserving externally held identity', () => {
    const f = fixture()
    let first: NativeRequest | undefined
    f.network.on('request', (request) => {
      first ??= request
    })
    for (let index = 0; index < 40; index++) {
      const info = {
        ...f.info,
        id: String(index),
        postData: 'x'.repeat(1024 * 1024),
      }
      f.emit({ event: 'request', sequence: index * 2 + 1, request: info })
      f.emit({
        event: 'requestfinished',
        sequence: index * 2 + 2,
        request: info,
      })
    }
    expect(f.network.metadataDiagnostics().cachedRequests).toBeLessThan(40)
    expect(f.network.metadataDiagnostics().cachedRequestBytes).toBeLessThanOrEqual(32 * 1024 * 1024)
    let replay: NativeRequest | undefined
    f.network.on('request', (request) => {
      replay = request
    })
    f.emit({
      event: 'request',
      sequence: 100,
      request: { ...f.info, id: '0', postData: 'actual' },
    })
    expect(replay).toBe(first)
    expect(replay!.postData()).toBe('actual')
  })
  it('measures long redirect metadata iteratively without duplicating shared graphs', () => {
    const f = fixture()
    const requests = Array.from(
      { length: 12000 },
      (_, index) => new NativeRequest(f.network, { ...f.info, id: String(index) }),
    )
    for (let index = 1; index < requests.length; index++)
      requests[index]!.update(undefined, undefined, requests[index - 1])
    const seen = new Set<NativeRequest>()
    const bytes = requests[0]!.retainedMetadataBytes(seen)
    expect(seen.size).toBe(12000)
    expect(bytes).toBe(requests.reduce((total, item) => total + item.ownMetadataBytes(), 0))
    expect(requests.at(-1)!.retainedMetadataBytes(seen)).toBe(0)
  })
  it('accounts for response metadata grown by a native wait without changing wrapper identity', () => {
    const f = fixture()
    const response = {
      requestId: f.info.requestId,
      session: '',
      frameId: 'root',
      method: 'POST',
      url: f.info.url,
      status: 200,
      statusText: 'OK',
      headers: {},
      mimeType: 'text/plain',
    }
    const initial = f.network.eventValue('response', {
      event: 'response',
      sequence: 1,
      request: f.info,
      response,
    })
    const before = f.network.metadataDiagnostics().cachedRequestBytes
    const observed = f.network.eventValue('response', {
      event: 'response',
      sequence: 2,
      request: f.info,
      response: { ...response, headers: { observed: 'x'.repeat(512 * 1024) } },
    })
    expect(observed).toBe(initial)
    expect(f.network.metadataDiagnostics().cachedRequestBytes - before).toBeGreaterThanOrEqual(
      512 * 1024,
    )
  })
  it('returns null for an actual request with no post body', () => {
    const f = fixture()
    const info = { ...f.info }
    Reflect.deleteProperty(info, 'postData')
    expect(new NativeRequest(f.network, info).postDataJSON()).toBeNull()
  })
  it('waits on native wire header snapshots and preserves exact own field names', async () => {
    const f = fixture()
    const request = new NativeRequest(f.network, f.info)
    f.execute.mockResolvedValue(
      JSON.parse('{"__proto__":"wire","User-Agent":"actual-wire-agent"}') as UntrustedInput,
    )
    expect(await request.allHeaders()).toEqual(
      Object.fromEntries([
        ['__proto__', 'wire'],
        ['user-agent', 'actual-wire-agent'],
      ]),
    )
    expect(await request.headersArray()).toEqual([
      { name: '__proto__', value: 'wire' },
      { name: 'User-Agent', value: 'actual-wire-agent' },
    ])
    expect(await request.headerValue('__proto__')).toBe('wire')
    expect(f.commands.every((command) => command.action === 'requestHeaders')).toBe(true)
  })
  it.each(['older-snapshot', 'older-null'] as const)(
    'preserves the canonical response event when a plain response query returns %s afterward',
    async (kind) => {
      const f = fixture()
      const requests: NativeRequest[] = []
      f.network.on('request', (request) => requests.push(request))
      f.emit({ event: 'request', sequence: 10, request: f.info })
      const request = requests[0]
      expect(request).toBeDefined()
      if (!request) throw new Error('Actual request observation unavailable')
      let resolveCommand!: (value: UntrustedInput) => void
      const command = new Promise<UntrustedInput>((resolve) => {
        resolveCommand = resolve
      })
      f.execute.mockReturnValueOnce(command)
      const pending = request.response()
      const responseInfo = {
        url: f.info.url,
        status: 200,
        statusText: 'OK',
        headers: { 'Actual-Wire': 'newer-response-event' },
        requestId: f.info.requestId,
        session: '',
        frameId: 'root',
        method: 'POST',
        headersReady: true,
      }
      f.emit({ event: 'requestfinished', sequence: 12, request: f.info, response: responseInfo })
      const observed = request.existingResponse()
      expect(observed).not.toBeNull()
      resolveCommand(
        kind === 'older-null'
          ? null
          : {
              ...responseInfo,
              headers: { 'Actual-Wire': 'older-command-snapshot' },
              headersReady: false,
            },
      )
      expect(await pending).toBe(observed)
      expect(observed?.headers()).toEqual({ 'actual-wire': 'newer-response-event' })
    },
  )
  it.each(['native-error', 'invalid-dto'] as const)(
    'keeps a response query %s observable after a newer canonical response arrives',
    async (kind) => {
      const f = fixture()
      const requests: NativeRequest[] = []
      f.network.on('request', (request) => requests.push(request))
      f.emit({ event: 'request', sequence: 10, request: f.info })
      const request = requests[0]
      if (!request) throw new Error('Actual request observation unavailable')
      let resolveCommand!: (value: UntrustedInput) => void
      let rejectCommand!: (error: Error) => void
      const command = new Promise<UntrustedInput>((resolve, reject) => {
        resolveCommand = resolve
        rejectCommand = reject
      })
      f.execute.mockReturnValueOnce(command)
      const rejected = expect(request.response()).rejects.toThrow()
      const responseInfo = {
        url: f.info.url,
        status: 200,
        statusText: 'OK',
        headers: { 'Actual-Wire': 'newer-response-event' },
        requestId: f.info.requestId,
        session: '',
        frameId: 'root',
        method: 'POST',
        headersReady: true,
      }
      f.emit({ event: 'response', sequence: 12, request: f.info, response: responseInfo })
      if (kind === 'native-error') rejectCommand(new Error('original native deadline'))
      else resolveCommand({ ...responseInfo, status: 'invalid native status type' })
      await rejected
      expect(request.existingResponse()?.headers()).toEqual({
        'actual-wire': 'newer-response-event',
      })
    },
  )
  it.each(['request', 'response'] as const)(
    'keeps a newer ready %s header event when an earlier metadata command settles afterward',
    async (kind) => {
      const f = fixture()
      const request = new NativeRequest(f.network, { ...f.info, headersReady: false }, 10)
      const responseInfo = {
        url: f.info.url,
        status: 200,
        statusText: 'OK',
        headers: { 'Actual-Wire': 'early' },
        requestId: f.info.requestId,
        session: '',
        frameId: 'root',
        method: 'POST',
        headersReady: false,
      }
      const response = f.network.createResponse(request, responseInfo, 10)
      let resolveCommand!: (value: UntrustedInput) => void
      const command = new Promise<UntrustedInput>((resolve) => {
        resolveCommand = resolve
      })
      f.execute.mockReturnValueOnce(command)
      const target = kind === 'request' ? request : response
      const pending = target.allHeaders()
      const complete = { 'Actual-Wire': 'later-ready-observed-metadata' }
      if (kind === 'request')
        request.observe({ ...f.info, headers: complete, headersReady: true }, 11)
      else response.observe({ ...responseInfo, headers: complete, headersReady: true }, 11)
      resolveCommand({ 'Actual-Wire': 'earlier-command-snapshot' })
      expect(await pending).toEqual({ 'actual-wire': 'later-ready-observed-metadata' })
      expect(target.headers()).toEqual({ 'actual-wire': 'later-ready-observed-metadata' })
    },
  )
  it('preserves the original command failure after a newer ready header event', async () => {
    const f = fixture()
    const request = new NativeRequest(f.network, { ...f.info, headersReady: false }, 10)
    let rejectCommand!: (error: Error) => void
    const command = new Promise<UntrustedInput>((_resolve, reject) => {
      rejectCommand = reject
    })
    f.execute.mockReturnValueOnce(command)
    const rejected = expect(request.allHeaders()).rejects.toThrow('original native deadline')
    request.observe(
      { ...f.info, headers: { 'Actual-Wire': 'later-ready' }, headersReady: true },
      11,
    )
    rejectCommand(new Error('original native deadline'))
    await rejected
    expect(request.headers()).toEqual({ 'actual-wire': 'later-ready' })
  })
  it('accepts the actual header command when a later event still lacks a complete receipt', async () => {
    const f = fixture()
    const request = new NativeRequest(f.network, { ...f.info, headersReady: false }, 10)
    let resolveCommand!: (value: UntrustedInput) => void
    const command = new Promise<UntrustedInput>((resolve) => {
      resolveCommand = resolve
    })
    f.execute.mockReturnValueOnce(command)
    const pending = request.allHeaders()
    request.observe({ ...f.info, headersReady: false }, 11)
    resolveCommand({ 'Actual-Wire': 'command-observed-complete-fields' })
    expect(await pending).toEqual({ 'actual-wire': 'command-observed-complete-fields' })
  })
  it('waits for marked raw request headers after genuine request completion', async () => {
    const f = fixture()
    const request = new NativeRequest(f.network, { ...f.info, headersReady: false })
    request.update(undefined, undefined, undefined, true)
    f.execute.mockResolvedValue({ 'Actual-Wire': 'late-original-hop' })
    expect(await request.headersArray()).toEqual([
      { name: 'Actual-Wire', value: 'late-original-hop' },
    ])
    expect(f.execute).toHaveBeenCalledTimes(1)
    expect(f.execute).toHaveBeenCalledWith(
      'network',
      expect.objectContaining({ action: 'requestHeaders', requestId: f.info.id }),
      expect.any(AbortSignal),
    )
    expect(await request.allHeaders()).toEqual({ 'actual-wire': 'late-original-hop' })
    expect(f.execute).toHaveBeenCalledTimes(1)
  })
  it('waits for the genuine raw-header receipt on an already-finished redirect response', async () => {
    const f = fixture()
    const request = new NativeRequest(f.network, f.info)
    request.update(undefined, undefined, undefined, true)
    const response = f.network.createResponse(request, {
      url: f.info.url,
      status: 302,
      statusText: 'Found',
      headers: { Location: '/next' },
      requestId: f.info.requestId,
      session: '',
      frameId: 'root',
      method: 'POST',
      headersReady: false,
    })
    f.execute.mockResolvedValue({
      Location: '/next',
      'Set-Cookie': 'one=1\ntwo=2',
    })
    expect(await response.allHeaders()).toEqual({
      location: '/next',
      'set-cookie': 'one=1\ntwo=2',
    })
    expect(f.execute.mock.calls.at(-1)?.[1]?.action).toBe('responseHeaders')
    await response.allHeaders()
    expect(f.execute).toHaveBeenCalledTimes(1)
  })
})
