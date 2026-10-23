// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { createReadStream } from 'node:fs'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeMultipartStreamJob } from '../src/native-http-stream.js'
import { NativeAPIRequestContext, NativeAPIResponse } from '../src/native-http.js'
import { type UntrustedInput } from '../src/untrusted.js'

function sourceFixture() {
  const calls: Record<string, UntrustedInput>[] = []
  const execute = vi.fn(
    (_command: string, params: Record<string, UntrustedInput> = {}): Promise<UntrustedInput> => {
      calls.push(params)
      if (params.action === 'create')
        return Promise.resolve({ apiContextId: 7, browserContextId: 3 })
      if (params.action === 'fetch' || params.action === 'fetchMultipart')
        return Promise.resolve({
          apiContextId: 7,
          responseId: '9',
          url: params.url,
          status: 201,
          statusText: 'Created',
          headers: [
            { name: 'Set-Cookie', value: 'a=1' },
            { name: 'Set-Cookie', value: 'b=2' },
          ],
        })
      if (params.action === 'body')
        return Promise.resolve({ body: Buffer.from('{"native":true}').toString('base64') })
      return Promise.resolve({})
    },
  )
  const lifecycle = new AbortController()
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: lifecycle.signal,
    processSignal: lifecycle.signal,
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
    close: () => Promise.resolve(),
  }
  return { source, calls, execute }
}

describe('native API request serialization and response ownership', () => {
  it('keeps one native prepare request pending through real file chunking and commit', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-native-http-')),
      path = join(directory, 'actual.json')
    const bytes = Buffer.alloc(70 * 1024, 211)
    await writeFile(path, bytes)
    let resolvePreparation: ((value: UntrustedInput) => void) | undefined
    const original = f.execute.getMockImplementation()
    if (!original) throw new Error('Missing native transport unit fixture')
    f.execute.mockImplementation((command, params = {}) => {
      if (params.action === 'prepareMultipart') {
        f.calls.push(params)
        return new Promise<UntrustedInput>((resolve) => {
          resolvePreparation = resolve
        })
      }
      if (params.action === 'commitMultipart') {
        f.calls.push(params)
        if (!resolvePreparation) throw new Error('Commit arrived before native preparation')
        resolvePreparation({
          apiContextId: 7,
          responseId: '9',
          url: 'https://api.test/upload',
          status: 201,
          statusText: 'Created',
          headers: [],
        })
        return Promise.resolve({})
      }
      return original(command, params)
    })
    const stream = createReadStream(path)
    try {
      const response = await api.post('https://api.test/upload', {
        multipart: { before: 'first', stream },
        timeout: 19,
      })
      expect(response.status()).toBe(201)
      const prepare = f.calls.find((value) => value.action === 'prepareMultipart')
      expect(prepare).toMatchObject({ apiContextId: 7, timeoutMs: 19, method: 'POST' })
      const chunks = f.calls.filter((value) => value.action === 'appendPreparedMultipart')
      expect(chunks.length).toBeGreaterThan(1)
      expect(
        Buffer.concat(
          chunks.map((value) =>
            Buffer.from(typeof value.body === 'string' ? value.body : '', 'base64'),
          ),
        ),
      ).toEqual(Buffer.concat([Buffer.from('first'), bytes]))
      for (const chunk of chunks) expect(chunk.uploadId).toBe(prepare?.uploadId)
      expect(f.calls.at(-1)).toMatchObject({
        action: 'commitMultipart',
        uploadId: prepare?.uploadId,
        multipartParts: [
          { name: 'before', offset: 0, size: 5 },
          {
            name: 'stream',
            offset: 5,
            size: bytes.length,
            filename: 'actual.json',
            inferMime: true,
          },
        ],
      })
      if (!stream.closed) await new Promise<void>((resolve) => stream.once('close', resolve))
      expect(NativeMultipartStreamJob.physical()).toEqual({ jobs: 0, streams: 0 })
      await response.dispose()
    } finally {
      stream.destroy()
      await api.dispose()
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('uses native IPC for associated requests, preserves repeated response headers and releases response bodies', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source, {
        baseURL: 'https://api.test/root/',
        extraHTTPHeaders: { 'X-Default': 'value' },
      })
    const response = await api.post('data', {
      params: { search: 'space value' },
      data: { native: true },
    })
    expect(f.calls[0]).toMatchObject({ action: 'create', scope: 'page' })
    expect(f.calls[1]).toMatchObject({
      action: 'fetch',
      url: 'https://api.test/root/data?search=space+value',
      method: 'POST',
      headers: expect.arrayContaining([
        { name: 'X-Default', value: 'value' },
        { name: 'content-type', value: 'application/json' },
      ]),
    })
    expect(response.ok()).toBe(true)
    expect(response.headers()['set-cookie']).toBe('a=1\nb=2')
    expect(response.headersArray()).toEqual([
      { name: 'Set-Cookie', value: 'a=1' },
      { name: 'Set-Cookie', value: 'b=2' },
    ])
    expect(await response.json()).toEqual({ native: true })
    await response.dispose()
    await expect(response.body()).rejects.toThrow('disposed')
    expect(f.calls.at(-1)).toMatchObject({
      action: 'disposeResponse',
      apiContextId: 7,
      responseId: '9',
    })
    await api.dispose()
  })
  it('serializes binary uploads through control packets bounded by the native framer', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const body = Buffer.alloc(100_003, 129)
    await api.put('https://api.test/binary', { data: body })
    const chunks = f.calls.filter((params) => params.action === 'upload')
    expect(chunks).toHaveLength(5)
    expect(
      Buffer.concat(
        chunks.map((params) =>
          Buffer.from(typeof params.body === 'string' ? params.body : '', 'base64'),
        ),
      ),
    ).toEqual(body)
    for (const chunk of chunks) expect(Buffer.byteLength(JSON.stringify(chunk))).toBeLessThan(65536)
    expect(f.calls.at(-1)).toMatchObject({
      action: 'fetch',
      method: 'PUT',
      uploadId: expect.any(String),
      headers: expect.arrayContaining([
        { name: 'content-type', value: 'application/octet-stream' },
      ]),
    })
    await api.dispose()
  })
  it('marshals FormData repeated text in insertion order and rejects file coercion', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const form = new FormData()
    form.append('same', 'first')
    form.append('same', 'second + value')
    form.append('unicode', 'café 日本語')
    await api.post('https://api.test/form', { form })
    const request = f.calls.at(-1)
    const body = Buffer.from(typeof request?.body === 'string' ? request.body : '', 'base64')
    expect([...new URLSearchParams(body.toString())]).toEqual([
      ['same', 'first'],
      ['same', 'second + value'],
      ['unicode', 'café 日本語'],
    ])
    const invalid = new FormData()
    invalid.append('file', new File(['binary'], 'f.bin'))
    const count = f.calls.length
    await expect(api.post('https://api.test/form', { form: invalid })).rejects.toThrow('multipart')
    expect(f.calls.length).toBe(count)
    await api.dispose()
  })
  it('marshals ordered multipart metadata and exact binary input for the native encoder', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    await api.post('https://api.test/upload', {
      multipart: {
        'name"quoted': 'text',
        file: {
          name: 'native.bin',
          mimeType: 'application/octet-stream',
          buffer: Buffer.from([0, 255, 13, 10]),
        },
      },
    })
    const request = f.calls.at(-1)
    const body = Buffer.from(typeof request?.body === 'string' ? request.body : '', 'base64')
    expect(request?.action).toBe('fetchMultipart')
    expect(request?.multipartParts).toEqual([
      { name: 'name"quoted', offset: 0, size: 4 },
      {
        name: 'file',
        offset: 4,
        size: 4,
        filename: 'native.bin',
        mimeType: 'application/octet-stream',
      },
    ])
    expect(body).toEqual(Buffer.concat([Buffer.from('text'), Buffer.from([0, 255, 13, 10])]))
    expect(request?.headers).not.toEqual(
      expect.arrayContaining([expect.objectContaining({ name: 'content-type' })]),
    )
    await api.dispose()
  })
  it('rejects unsafe headers, multiple body encodings and unsupported options before native side effects', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    await expect(
      api.get('https://api.test', { headers: { test: 'first\r\nnext' } }),
    ).rejects.toThrow('header')
    await expect(api.post('https://api.test', { data: { a: 1 }, form: { a: 1 } })).rejects.toThrow(
      'one of',
    )
    await expect(
      api.get('https://api.test', { unknown: true } as UntrustedInput as Record<string, never>),
    ).rejects.toThrow()
    expect(f.calls).toHaveLength(0)
    await api.dispose()
  })
  it('invalidates already-read response buffers on request-context disposal and disposes once concurrently', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const response = await api.get('https://api.test/data')
    expect(await response.json()).toEqual({ native: true })
    const first = api.dispose(),
      second = api.dispose()
    expect(first).toBe(second)
    await first
    await expect(response.body()).rejects.toThrow('disposed')
    await expect(api.get('https://api.test/data')).rejects.toThrow('disposed')
    expect(f.calls.filter((call) => call.action === 'dispose')).toHaveLength(1)
    await response.dispose()
    expect(f.calls.filter((call) => call.action === 'disposeResponse')).toHaveLength(0)
  })
  it('rejects a pending body reply after native context closure instead of caching its bytes', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const response = await api.get('https://api.test/data')
    let resolveBody: ((value: UntrustedInput) => void) | undefined
    f.execute.mockImplementationOnce(
      () =>
        new Promise<UntrustedInput>((resolve) => {
          resolveBody = resolve
        }),
    )
    const body = response.body()
    await Promise.resolve()
    await Promise.resolve()
    api.markContextClosed()
    if (!resolveBody) throw new Error('Native body command was not submitted')
    resolveBody({ body: Buffer.from('released').toString('base64') })
    await expect(body).rejects.toThrow('disposed')
    await expect(response.body()).rejects.toThrow('disposed')
  })
  it('preserves valid prototype-named header fields as own string values', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    const response = await api.get('https://api.test/data')
    const special = new NativeAPIResponse(api, {
      apiContextId: 7,
      responseId: '10',
      url: response.url(),
      status: 200,
      statusText: 'OK',
      headers: [
        { name: 'constructor', value: 'native constructor' },
        { name: '__proto__', value: 'native prototype' },
        { name: '__proto__', value: 'second value' },
      ],
    })
    const headers = special.headers()
    expect(Object.getPrototypeOf(headers)).toBe(Object.prototype)
    expect(Object.hasOwn(headers, '__proto__')).toBe(true)
    expect(headers.constructor).toBe('native constructor')
    expect(headers.__proto__).toBe('native prototype, second value')
    await api.dispose()
  })
  it('waits for native storage commit before exposing a profile or issuing requests', async () => {
    const f = sourceFixture()
    let releaseCommit: ((value: UntrustedInput) => void) | undefined
    let commitStarted: (() => void) | undefined
    const started = new Promise<void>((resolve) => {
      commitStarted = resolve
    })
    f.execute.mockImplementation((_command, params = {}) => {
      f.calls.push(params)
      if (params.action === 'create')
        return Promise.resolve({ apiContextId: 7, browserContextId: 3 })
      if (params.action === 'initializeStorage' && params.storageAction === 'storageUploadBegin')
        return Promise.resolve({ uploadId: 'native-state' })
      if (params.action === 'initializeStorage' && params.storageAction === 'setStorageState') {
        commitStarted?.()
        return new Promise<UntrustedInput>((resolve) => {
          releaseCommit = resolve
        })
      }
      return Promise.resolve({})
    })
    let published = false
    const creating = NativeAPIRequestContext.create(f.source, {
      storageState: { cookies: [], origins: [] },
    }).then((api) => {
      published = true
      return api
    })
    await started
    expect(published).toBe(false)
    expect(f.calls.some((call) => call.action === 'fetch')).toBe(false)
    expect(f.calls[0]).toMatchObject({
      action: 'create',
      scope: 'independent',
      initializeStorage: true,
    })
    if (!releaseCommit) throw new Error('Native storage commit was not submitted')
    releaseCommit({})
    const api = await creating
    expect(published).toBe(true)
    await api.dispose()
    expect(f.calls.at(-1)).toMatchObject({ action: 'dispose', apiContextId: 7 })
  })
  it('retains a late native profile identity until disposal physically completes', async () => {
    const f = sourceFixture()
    let releaseCreate: ((value: UntrustedInput) => void) | undefined
    let releaseDispose: ((value: UntrustedInput) => void) | undefined
    let notifyCreate: (() => void) | undefined, notifyDispose: (() => void) | undefined
    const createStarted = new Promise<void>((resolve) => {
      notifyCreate = resolve
    })
    const disposeStarted = new Promise<void>((resolve) => {
      notifyDispose = resolve
    })
    f.execute.mockImplementation((_command, params = {}) => {
      f.calls.push(params)
      if (params.action === 'create') {
        notifyCreate?.()
        return new Promise<UntrustedInput>((resolve) => {
          releaseCreate = resolve
        })
      }
      if (params.action === 'dispose') {
        notifyDispose?.()
        return new Promise<UntrustedInput>((resolve) => {
          releaseDispose = resolve
        })
      }
      return Promise.resolve({})
    })
    const api = new NativeAPIRequestContext(f.source, {}, 'independent')
    const request = api.get('https://api.test/data')
    const rejected = expect(request).rejects.toThrow('disposed')
    await createStarted
    let disposed = false
    const closing = api.dispose().then(() => {
      disposed = true
    })
    if (!releaseCreate) throw new Error('Native profile creation was not submitted')
    releaseCreate({ apiContextId: 7, browserContextId: 3 })
    await disposeStarted
    expect(disposed).toBe(false)
    expect(f.calls.some((call) => call.action === 'fetch')).toBe(false)
    if (!releaseDispose) throw new Error('Native profile disposal was not submitted')
    releaseDispose({})
    await closing
    await rejected
    expect(disposed).toBe(true)
    expect(f.calls.filter((call) => call.action === 'dispose')).toHaveLength(1)
  })
  it('passes current associated context timeout preferences to native deadlines while preserving explicit zero and per-request overrides', async () => {
    const f = sourceFixture()
    let current = 75
    const api = new NativeAPIRequestContext(f.source, {}, 'context', 3, () => current)
    await api.get('https://api.test/initial')
    current = 150
    await api.get('https://api.test/updated')
    await api.get('https://api.test/infinite', { timeout: 0 })
    const calls = f.calls.filter((call) => call.action === 'fetch')
    expect(calls.map((call) => call.timeoutMs)).toEqual([75, 150, 0])
    const explicit = new NativeAPIRequestContext(
      f.source,
      { timeout: 50 },
      'context',
      3,
      () => current,
    )
    await explicit.get('https://api.test/explicit-context')
    await explicit.get('https://api.test/explicit-request', { timeout: 25 })
    expect(
      f.calls
        .filter((call) => call.action === 'fetch')
        .slice(-2)
        .map((call) => call.timeoutMs),
    ).toEqual([50, 25])
    await api.dispose()
    await explicit.dispose()
  })
})

describe('native API context browser-default compatibility boundary', () => {
  it('normalizes recognized browser-only defaults while forwarding actual native HTTP policy', async () => {
    const f = sourceFixture()
    const api = await NativeAPIRequestContext.create(f.source, {
      acceptDownloads: true,
      bypassCSP: true,
      javaScriptEnabled: false,
      serviceWorkers: 'block',
      colorScheme: 'dark',
      contrast: 'more',
      forcedColors: 'active',
      reducedMotion: 'reduce',
      hasTouch: true,
      isMobile: true,
      offline: true,
      locale: 'fr-FR',
      timezoneId: 'Europe/Paris',
      viewport: { width: 320, height: 200 },
      screen: { width: 800, height: 600 },
      deviceScaleFactor: 2,
      userAgent: 'native-request-agent',
      extraHTTPHeaders: { 'X-Native': 'actual-policy' },
      ignoreHTTPSErrors: true,
      httpCredentials: { username: 'native', password: 'actual-secret' },
    })
    const create = f.calls.find((call) => call.action === 'create')
    expect(create).toMatchObject({
      scope: 'independent',
      userAgent: 'native-request-agent',
      headers: [{ name: 'X-Native', value: 'actual-policy' }],
      ignoreHTTPSErrors: true,
      username: 'native',
      password: 'actual-secret',
    })
    for (const name of ['viewport', 'offline', 'locale', 'bypassCSP', 'serviceWorkers'])
      expect(create).not.toHaveProperty(name)
    const response = await api.get('https://actual-native.test/')
    expect(response.status()).toBe(201)
    expect(f.calls.some((call) => call.action === 'fetch')).toBe(true)
    await response.dispose()
    await api.dispose()
  })

  it('rejects malformed known browser defaults before a native owner is created', () => {
    const f = sourceFixture()
    expect(
      () => new NativeAPIRequestContext(f.source, { viewport: { width: NaN, height: 200 } }),
    ).toThrow()
    const invalid = { userAgent: 'native', colorScheme: 'dark' as const }
    Reflect.set(invalid, 'colorScheme', 'sepia')
    expect(() => new NativeAPIRequestContext(f.source, invalid)).toThrow()
    expect(f.calls).toHaveLength(0)
  })

  it('keeps unknown constructor fields rejected rather than masking typos', () => {
    const f = sourceFixture()
    const options = { userAgent: 'native', unknownBrowserSetting: true }
    expect(() => new NativeAPIRequestContext(f.source, options)).toThrow(/unknownBrowserSetting/)
    expect(f.calls).toHaveLength(0)
  })
})

describe('native API bounded-upload diagnostics', () => {
  it('retains the public method context on a genuine pre-wire body-budget rejection', async () => {
    const f = sourceFixture(),
      api = new NativeAPIRequestContext(f.source)
    await expect(
      api.post('https://actual-native.test/refuse', { data: Buffer.alloc(8 * 1024 * 1024 + 1) }),
    ).rejects.toThrow('apiRequestContext.post: Native API upload exceeds 8 MiB')
    expect(f.calls).toHaveLength(0)
    await api.dispose()
  })
})

describe('native API explicit extra CA marshaling', () => {
  it('preserves explicit PEM bytes for native validation without changing TLS verification', async () => {
    const f = sourceFixture()
    const pem =
      '-----BEGIN CERTIFICATE-----\nopaque-native-validated-content\n-----END CERTIFICATE-----\n'
    const api = await NativeAPIRequestContext.create(f.source, { extraCACertificates: [pem] })
    const create = f.calls.find((call) => call.action === 'create')
    expect(create).toMatchObject({ extraCACertificates: [pem] })
    expect(create).not.toHaveProperty('ignoreHTTPSErrors')
    await api.dispose()
  })

  it('rejects certificate count and UTF8 byte budgets before native admission', () => {
    const f = sourceFixture()
    expect(
      () =>
        new NativeAPIRequestContext(f.source, {
          extraCACertificates: Array.from({ length: 17 }, () => 'pem'),
        }),
    ).toThrow()
    expect(
      () => new NativeAPIRequestContext(f.source, { extraCACertificates: ['é'.repeat(17 * 1024)] }),
    ).toThrow()
    expect(f.calls).toHaveLength(0)
  })
})
