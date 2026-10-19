// Controlled protocol fuzzing: no CEF WebSocket support is inferred from these ACKs.
// node --max-old-space-size=512 --expose-gc scripts/sdk-routes-fuzz.mjs EMITTED_SDK_DIR [seed]
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers'
import { pathToFileURL } from 'node:url'

const load = (name) => import(pathToFileURL(resolve(process.argv[2], name + '.js')))
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Deliberate emitted-module boundary.
const { NativeWebSocketRoutes } =
  /** @type {typeof import('../src/native-websocket-routes.js')} */ (
    await load('native-websocket-routes')
  )
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Deliberate emitted-module boundary.
const { NativeContextRoutes } = /** @type {typeof import('../src/native-context-routes.js')} */ (
  await load('native-context-routes')
)
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Deliberate emitted-module boundary.
const { runNativeWebSocketPayload } =
  /** @type {typeof import('../src/native-websocket-payload.js')} */ (
    await load('native-websocket-payload')
  )
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Deliberate emitted-module boundary.
const { nativeRouteCallbackPhysicalCount } =
  /** @type {typeof import('../src/native-route-callback.js')} */ (
    await load('native-route-callback')
  )
let seed = Number(process.argv[3] ?? 0x57a9c21) >>> 0
const initialSeed = seed,
  random = () => {
    seed ^= seed << 13
    seed ^= seed >>> 17
    seed ^= seed << 5
    return seed >>> 0
  }
const drain = () => new Promise((done) => setImmediate(done))
const deferred = () => {
  let resolve = () => undefined
  const promise = new Promise(
    /**
     * Deliver the actual controlled receipt.
     * @param {(value?: void) => void} done
     */ (done) => {
      resolve = done
    },
  )
  return { promise, resolve }
}
let operations = 0,
  websocketEvents = 0,
  callbackStarts = 0,
  callbackFinishes = 0,
  authorMessages = 0,
  actualCommands = 0
const heap = [],
  start = performance.now()

for (let batch = 0; batch < 10; ++batch) {
  for (let cycle = 0; cycle < 400; ++cycle) {
    const scope = new AbortController(),
      process = new AbortController()
    /** @type {((info: import('../src/native-websocket-route-protocol.js').NativeWebSocketRouteInfo) => void) | undefined} */
    let listener
    let registration = '',
      index = 0,
      closed = 0,
      matched
    const router = new NativeWebSocketRoutes({
      signal: scope.signal,
      processSignal: process.signal,
      baseURL: () => 'https://native.test/',
      subscribe: (fn) => {
        assert.equal(listener, undefined)
        listener = fn
        return () => {
          listener = undefined
        }
      },
      callback: async (action) => {
        await Promise.resolve()
        ++operations
        if (action === 'start') ++callbackStarts
        else ++callbackFinishes
      },
      run: async (params) => {
        await Promise.resolve()
        ++operations
        ++actualCommands
        if (params.action === 'routeRegister') registration = params.registrationId
        if (params.operation === 'match') matched = params.matched
      },
    })
    const mode = random() % 3
    await router.route(
      mode === 0 ? '**/*' : mode === 1 ? /native\.test/g : (url) => url.hostname === 'native.test',
      async (socket) => {
        socket.onMessage(async (message) => {
          assert.equal(message.payload(), String(index++))
          ++authorMessages
          const choice = random() % 4
          if (choice === 0) await drain()
          if (choice === 1) await message.drop()
          else if (choice === 2) await message.replace('replacement')
          else await message.forward()
          await assert.rejects(message.drop(), /already consumed/)
        })
        socket.onClose(() => {
          ++closed
        })
        await socket.connectToServer()
      },
    )
    let token = 0
    /**
     * Emit a controlled native transport event.
     * @param {'created' | 'message' | 'closed'} stage
     */
    const emit = (stage, number = 0) => {
      ++operations
      ++websocketEvents
      assert.ok(listener)
      listener({
        scope: 'context',
        contextId: 1,
        pageId: 0,
        targetId: 'worker-target',
        registrationId: registration,
        routingToken: '1',
        requestId: 'channel',
        stage,
        url: 'ws://native.test/wire',
        messageId: stage === 'message' ? String(number + 1) : '',
        outgoing: Boolean(number % 2),
        opcode: stage === 'message' ? 1 : 0,
        payload: stage === 'message' ? Buffer.from(String(number)) : Buffer.alloc(0),
        callback: { invocation: 'invocation-' + ++token, token: String(token), scope: '1' },
      })
    }
    if (mode) {
      emit('match')
      while (router.chains.size) await drain()
      assert.equal(matched, true)
    }
    emit('created')
    for (let number = 0; number < 24; ++number) emit('message', number)
    emit('closed')
    while (router.chains.size) await drain()
    assert.equal(index, 24)
    assert.equal(closed, 1)
    assert.equal(router.sockets.size, 0)
    assert.equal(router.invocations.pending(), 0)
    assert.equal(nativeRouteCallbackPhysicalCount(process.signal), 0)
    assert.deepEqual(router.errors(), [])
    await router.unrouteAll({ behavior: ['default', 'wait', 'ignoreErrors'][random() % 3] })
    assert.equal(router.registrations.size, 0)
    scope.abort()
    assert.equal(listener, undefined)
  }
  globalThis.gc?.()
  heap.push(process.memoryUsage())
}

// Scope closure and removal retain the actual authored promise and finish ACK.
for (let cycle = 0; cycle < 1000; ++cycle) {
  const scope = new AbortController(),
    process = new AbortController(),
    author = deferred(),
    finish = deferred(),
    entered = deferred()
  let listener,
    registration,
    finishEntered = false,
    settled = false
  const router = new NativeWebSocketRoutes({
    signal: scope.signal,
    processSignal: process.signal,
    subscribe: (fn) => {
      listener = fn
      return () => {
        listener = undefined
      }
    },
    run: async (params) => {
      await Promise.resolve()
      ++operations
      if (params.action === 'routeRegister') registration = params.registrationId
    },
    callback: async (action) => {
      await Promise.resolve()
      ++operations
      if (action === 'finish') {
        ++callbackFinishes
        finishEntered = true
        await finish.promise
      } else ++callbackStarts
    },
  })
  await router.route('**/*', async () => {
    entered.resolve()
    await author.promise
    throw new Error('selected authored failure')
  })
  listener({
    scope: 'page',
    contextId: 1,
    pageId: 1,
    targetId: 'page-target',
    registrationId: registration,
    routingToken: '1',
    requestId: 'channel',
    stage: 'created',
    url: 'ws://native.test/wire',
    messageId: '',
    outgoing: false,
    opcode: 0,
    payload: Buffer.alloc(0),
    callback: { invocation: 'actual', token: '1', scope: '1' },
  })
  ++operations
  ++websocketEvents
  await entered.promise
  const remove = router.unrouteAll({ behavior: cycle % 2 ? 'wait' : 'ignoreErrors' }).then(() => {
    settled = true
  })
  await drain()
  if (cycle % 2) assert.equal(settled, false)
  else {
    await remove
    scope.abort()
  }
  assert.equal(router.invocations.pending(), 1)
  author.resolve()
  while (!finishEntered) await drain()
  assert.equal(router.invocations.pending(), 1)
  assert.equal(nativeRouteCallbackPhysicalCount(process.signal), 1)
  if (cycle % 2) assert.equal(settled, false)
  finish.resolve()
  await remove
  while (router.chains.size) await drain()
  assert.equal(router.invocations.pending(), 0)
  assert.equal(nativeRouteCallbackPhysicalCount(process.signal), 0)
  assert.equal(router.errors().length, cycle % 2 ? 1 : 0)
  scope.abort()
  assert.equal(listener, undefined)
}

// Staging preserves <=64 KiB command frames and one native commit, with actual abort cleanup.
let stagedBytes = 0,
  stagingCommits = 0,
  stagingAborts = 0
for (let cycle = 0; cycle < 160; ++cycle) {
  const length = cycle === 0 ? 2_097_152 : 24_577 + (random() % 2_072_575)
  const payload = Buffer.alloc(length, random() & 255).toString('base64')
  let appended = '',
    beginBytes = 0,
    commitCount = 0,
    abortCount = 0
  const fail = cycle % 8,
    actualFailure = new Error('actual staging command rejected')
  /**
   * Run a controlled native staging command.
   * @param {Record<string, import('../src/untrusted.js').UntrustedInput>} params
   */
  const run = async (params) => {
    await Promise.resolve()
    ++operations
    assert.ok(Buffer.byteLength(JSON.stringify(params)) <= 65_536)
    if (params.action === 'routePayloadBegin') {
      assert.equal(typeof params.bytes, 'number')
      beginBytes = /** @type {number} */ (params.bytes)
      return { payloadToken: 'actual-stage' }
    }
    if (params.action === 'routePayloadAppend') {
      if (fail === 1) throw actualFailure
      if (typeof params.base64 !== 'string') throw new Error('Expected base64 staging text')
      appended += params.base64
    }
    if (params.action === 'routePayloadCommit') {
      ++commitCount
      if (fail === 2) throw actualFailure
    }
  }
  /**
   * Observe actual staging cleanup.
   * @param {Record<string, import('../src/untrusted.js').UntrustedInput>} params
   */
  const cleanup = async (params) => {
    await Promise.resolve()
    ++operations
    assert.equal(params.action, 'routePayloadAbort')
    ++abortCount
  }
  const work = runNativeWebSocketPayload(
    run,
    { action: 'routePolicyCommand', payloadBase64: payload, operation: 'client' },
    cleanup,
  )
  if (fail === 1 || fail === 2) {
    await assert.rejects(work, (error) => error === actualFailure)
    assert.equal(abortCount, 1)
  } else {
    await work
    assert.equal(commitCount, 1)
    assert.equal(appended, payload)
    assert.equal(abortCount, 0)
  }
  assert.equal(beginBytes, length)
  stagedBytes += length
  stagingCommits += commitCount
  stagingAborts += abortCount
}

// Default HTTP removal ACKs must not erase later registrations.
for (let cycle = 0; cycle < 2000; ++cycle) {
  const pending = deferred()
  /** @type {string[]} */
  const ids = []
  const routes = new NativeContextRoutes(async (params) => {
    ++operations
    if (params.action === 'register') {
      assert.equal(typeof params.registrationId, 'string')
      ids.push(/** @type {string} */ (params.registrationId))
    } else await pending.promise
  })
  await routes.route('**/*', () => undefined)
  const remove = routes.unrouteAll()
  const later = () => undefined
  await routes.route(/wire/g, later)
  pending.resolve()
  await remove
  assert.equal(routes.resolve(ids[0], false), undefined)
  assert.equal(routes.resolve(ids[1], false), later)
  routes.close()
}
globalThis.gc?.()
assert.ok(operations >= 100_000)
assert.equal(callbackStarts, callbackFinishes)
const heapGrowth = heap.at(-1).heapUsed - heap[2].heapUsed
assert.ok(heapGrowth < 8 * 1024 * 1024, `retained heap increased ${heapGrowth} bytes after warmup`)
console.log(
  JSON.stringify(
    {
      seed: initialSeed,
      operations,
      websocketEvents,
      lifecycleCancellationCases: 1000,
      httpRemovalRaces: 2000,
      stagedMessages: 160,
      stagedBytes,
      stagingCommits,
      stagingAborts,
      callbackStarts,
      callbackFinishes,
      authorMessages,
      actualCommands,
      elapsedMs: performance.now() - start,
      heap,
      heapGrowthAfterWarmup: heapGrowth,
      finalMemory: process.memoryUsage(),
      scope:
        'Controlled protocol SDK stress; not custom-CEF WebSocket execution or native leak proof',
    },
    null,
    2,
  ),
)
