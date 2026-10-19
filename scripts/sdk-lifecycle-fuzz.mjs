#!/usr/bin/env node
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Controlled fuzz/process-model harness selects emitted modules and deliberately malformed protocol values; production SDK validation is tested separately. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
// Run with: node --expose-gc --max-old-space-size=512 --experimental-transform-types scripts/sdk-lifecycle-fuzz.mjs
// This exercises the real SDK against a deterministic process/pipe model, not CEF.
import childProcess from 'node:child_process'
import { createHash } from 'node:crypto'
import { getEventListeners } from 'node:events'
import { existsSync } from 'node:fs'
import { readFile, writeFile } from 'node:fs/promises'
import { registerHooks, syncBuiltinESMExports } from 'node:module'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { PassThrough, Writable } from 'node:stream'
import { setImmediate } from 'node:timers'
import { fileURLToPath } from 'node:url'

const sourceRoot = resolve(
  process.env.SUITECUT_LIFECYCLE_SOURCE ?? dirname(fileURLToPath(import.meta.url)) + '/..',
)
registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier.endsWith('.js') && context.parentURL?.startsWith('file:')) {
      const candidate = new URL(specifier.slice(0, -3) + '.ts', context.parentURL)
      if (existsSync(candidate)) return nextResolve(candidate.href, context)
    }
    return nextResolve(specifier, context)
  },
})
const originalSpawn = childProcess.spawn
let current
let actualCommands = 0
let actualReplies = 0
class ProcessModel {
  constructor() {
    this.child = new childProcess.ChildProcess()
    this.output = new PassThrough()
    this.requests = new Map()
    this.commands = 0
    this.canceled = new Set()
    this.lateReplies = 0
    this.auto = false
    this.child.stdin = new Writable({
      write: (chunk, _encoding, done) => {
        for (const line of chunk.toString().trim().split('\n')) {
          const command = JSON.parse(line)
          this.commands++
          actualCommands++
          if (command.command === 'cancel') this.canceled.add(command.target)
          if (this.auto) globalThis.queueMicrotask(() => this.reply(command.id))
          else this.requests.set(command.id, command)
        }
        done()
      },
    })
    this.child.stdout = this.output
    this.child.stderr = new PassThrough()
    this.child.stdin.once('finish', () =>
      globalThis.queueMicrotask(() => this.child.emit('close', 0, null)),
    )
    this.child.kill = () => {
      globalThis.queueMicrotask(() => this.child.emit('close', 1, null))
      return true
    }
  }
  emit(header, data = Buffer.alloc(0)) {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.allocUnsafe(4)
    prefix.writeUInt32LE(json.length)
    this.output.write(Buffer.concat([prefix, json, data]))
  }
  reply(id, data = {}) {
    this.requests.delete(id)
    actualReplies++
    if (this.canceled.delete(id)) this.lateReplies++
    this.emit({ type: 'response', id, ok: true }, Buffer.from(JSON.stringify(data)))
  }
  async drain() {
    while (this.requests.size) {
      const ids = [...this.requests.keys()]
      for (let i = ids.length - 1; i >= 0; i--) this.reply(ids[i])
      await turn()
    }
  }
}
childProcess.spawn = () => {
  current = new ProcessModel()
  return current.child
}
syncBuiltinESMExports()
const sourceEntry = new URL(
  process.env.SUITECUT_LIFECYCLE_ENTRY ?? 'src/native-browser.ts',
  `file://${sourceRoot}/`,
)
const { launchNativeBrowser } = await import(sourceEntry)
const turn = () => new Promise((resolve) => setImmediate(resolve))
async function launch() {
  current = undefined
  const promise = launchNativeBrowser({
    executablePath: '/model/suitecut-browser',
    width: 2,
    height: 2,
  })
  while (!current) await turn()
  const model = current
  model.emit({ type: 'ready', timestampMs: 1, cefVersion: 'process-model' })
  return { browser: await promise, model }
}
async function collect() {
  await turn()
  for (let i = 0; i < 4; i++) {
    globalThis.gc?.()
    await turn()
  }
}
let seed = Number(process.env.SUITECUT_LIFECYCLE_SEED ?? 0x5107c0de) >>> 0
const initialSeed = seed
const random = () => {
  seed ^= seed << 13
  seed ^= seed >>> 17
  seed ^= seed << 5
  return seed >>> 0
}
const operations = Number(process.env.SUITECUT_LIFECYCLE_OPERATIONS ?? 120000)
const benchmarkOperations = Number(process.env.SUITECUT_LIFECYCLE_BENCHMARK ?? 30000)
const result = {
  mode: process.env.SUITECUT_LIFECYCLE_BENCHMARK_ONLY ? 'benchmark' : 'fuzz',
  seed: initialSeed,
  nodeVersion: process.version,
  sourceSha256: createHash('sha256')
    .update(await readFile(sourceEntry))
    .digest('hex'),
  harnessSha256: createHash('sha256')
    .update(await readFile(fileURLToPath(import.meta.url)))
    .digest('hex'),
  requestedOperations: operations,
  scope: 'SDK process/pipe model; no CEF runtime or native allocation coverage',
  counts: {},
  heap: [],
  benchmark: {},
  failures: [],
}
async function benchmark() {
  const fixture = await launch()
  fixture.model.auto = true
  const samples = []
  for (let i = 0; i < 10000; i++)
    await fixture.browser.runAutomation('networkBody', { bodyId: 'warmup', timeoutMs: 30000 })
  for (let sample = 0; sample < 5; sample++) {
    const started = performance.now()
    for (let i = 0; i < benchmarkOperations; i++)
      await fixture.browser.runAutomation('networkBody', {
        bodyId: 'body',
        stage: 'consume',
        timeoutMs: 30000,
      })
    samples.push(performance.now() - started)
  }
  await fixture.browser.close()
  await collect()
  return {
    operationsPerSample: benchmarkOperations,
    samplesMs: samples,
    medianMs: [...samples].sort((a, b) => a - b)[2],
  }
}
try {
  assert.equal(typeof globalThis.gc, 'function', 'Use --expose-gc for retention evidence')
  if (process.env.SUITECUT_LIFECYCLE_BENCHMARK_ONLY) result.benchmark = await benchmark()
  else {
    const { browser, model } = await launch()
    const counts = {
      success: 0,
      aborted: 0,
      capacity: 0,
      timeout: 0,
      queued: 0,
      lateReplies: 0,
      subscriptions: 0,
      pageChurn: 0,
    }
    const heapRounds = 8
    for (let round = 0; round < heapRounds; round++) {
      for (let index = 0; index < Math.ceil(operations / heapRounds / 192); index++) {
        const batch = []
        const controllers = []
        for (let j = 0; j < 192; j++) {
          const controller = new AbortController()
          controllers.push(controller)
          const control = (random() & 3) !== 0
          if (control) counts.queued++
          const pending = browser
            .runAutomation(
              control ? 'networkBody' : 'evaluate',
              { bodyId: String(j), expression: String(random()), timeoutMs: 30000 },
              controller.signal,
            )
            .then(
              () => {
                counts.success++
              },
              (error) => {
                if (error.message.includes('seeded abort')) counts.aborted++
                else if (/Too many|capacity/.test(error.message)) counts.capacity++
                else throw error
              },
            )
          batch.push(pending)
          if ((random() & 3) === 0) controller.abort(new Error('seeded abort'))
          if ((random() & 7) === 0 && model.requests.size) {
            const ids = [...model.requests.keys()]
            model.reply(ids[random() % ids.length])
          }
        }
        // Cancel some queued controls and reply out of order, including canceled originals.
        for (const controller of controllers)
          if ((random() & 15) === 0) controller.abort(new Error('seeded abort'))
        await model.drain()
        await Promise.all(batch)
        for (const controller of controllers)
          assert.equal(getEventListeners(controller.signal, 'abort').length, 0)
        const callback = () => undefined
        const remove = [
          browser.onContextRoute(callback),
          browser.onContextNetwork(callback),
          browser.onContextConsole(callback),
          browser.onFrame(callback),
          browser.onAudio(callback),
        ]
        for (const unsubscribe of remove.reverse()) {
          unsubscribe()
          unsubscribe()
          counts.subscriptions++
        }
      }
      await collect()
      result.heap.push(process.memoryUsage())
    }
    const timeout = browser.runAutomation('evaluate', { timeoutMs: 1 }).then(
      () => {
        throw new Error('missing timeout')
      },
      (error) => {
        assert.match(error.message, /timed out/)
        counts.timeout++
      },
    )
    await timeout
    await model.drain()
    // Actual page-close frames retire each page, rather than synthesizing retirement on abort.
    for (let id = 2; id < 2050; id++) {
      model.emit(
        { type: 'context' },
        Buffer.from(
          JSON.stringify({ event: 'page-created', contextId: 1, pageId: id, sequence: id * 2 }),
        ),
      )
      const page = browser.pageSource(id)
      page.onFrame(() => undefined)
      model.emit(
        { type: 'context' },
        Buffer.from(
          JSON.stringify({ event: 'page-closed', contextId: 1, pageId: id, sequence: id * 2 + 1 }),
        ),
      )
      await page.closed
      counts.pageChurn++
    }
    // Deliberately leave subscribers registered; physical close must release captured author state.
    const weak = []
    for (const subscribe of [
      browser.onContextRoute,
      browser.onContextNetwork,
      browser.onContextConsole,
    ]) {
      let callback = (() => {
        const captured = new Array(16384).fill('author state')
        return () => captured.length
      })()
      weak.push(new WeakRef(callback))
      subscribe(callback)
      callback = undefined
    }
    await browser.close()
    await collect()
    result.closedSubscriberReachable = weak.map((ref) => Boolean(ref.deref()))
    for (const reachable of result.closedSubscriberReachable)
      if (reachable) result.failures.push('Actual process close retained an author subscriber')
    result.postCloseSubscriptions = []
    for (const method of [
      'onContextWorkerMessage',
      'onContextWorkerPredicate',
      'onHTTPLookupMessage',
      'onStreamingMessage',
      'onRecordingMessage',
      'onLocatorHandlerStage',
    ]) {
      let rejected = false
      try {
        browser[method](() => undefined)
      } catch {
        rejected = true
      }
      result.postCloseSubscriptions.push({ method, rejected })
      if (!rejected) result.failures.push(`Closed source admitted ${method}`)
    }
    result.benchmark = await benchmark()
    counts.lateReplies = model.lateReplies
    result.completedScheduleOperations = counts.success + counts.aborted + counts.capacity
    result.counts = counts
    result.actualCommands = actualCommands
    result.actualReplies = actualReplies
    const heapSizes = result.heap.slice(2).map((item) => item.heapUsed)
    result.steadyHeapRangeBytes = Math.max(...heapSizes) - Math.min(...heapSizes)
    assert.ok(
      result.steadyHeapRangeBytes < 8 * 1024 * 1024,
      'Steady-state SDK heap must plateau within 8 MiB',
    )
    if (result.failures.length) process.exitCode = 1
  }
} finally {
  childProcess.spawn = originalSpawn
  syncBuiltinESMExports()
  const output = JSON.stringify(result, null, 2) + '\n'
  if (process.env.SUITECUT_LIFECYCLE_OUTPUT)
    await writeFile(process.env.SUITECUT_LIFECYCLE_OUTPUT, output)
  process.stdout.write(output)
}
