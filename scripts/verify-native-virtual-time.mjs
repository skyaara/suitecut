// SPDX-License-Identifier: MIT
// Genuine Chromium clocks; Node delays below only measure real-time freeze.
/* global XMLHttpRequest, requestIdleCallback, performance, setTimeout, clearTimeout, setInterval, clearInterval, requestAnimationFrame, cancelAnimationFrame */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as realDelay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sdk = z.string().parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY)
const modulePath =
  process.env.SUITECUT_NATIVE_VIRTUAL_TIME_MODULE ?? `${sdk}/native-virtual-time.js`
const directory = resolve(
  process.env.SUITECUT_NATIVE_VIRTUAL_TIME_PROOF_DIRECTORY ?? '.suitecut/native-virtual-time',
)
await mkdir(directory, { recursive: true })
const InfoSchema = z.object({
  installed: z.boolean(),
  lastAcknowledgedPaused: z.boolean(),
  pendingPhysicalCommand: z.boolean(),
  activeOwner: z.boolean(),
  queued: z.number().int(),
  closing: z.boolean(),
  capabilities: z.record(z.string(), z.boolean()),
})
/** @typedef {z.infer<typeof InfoSchema>} ClockInfo */
/** @typedef {{status(options?:{timeout?:number}):Promise<ClockInfo>,installPaused(options?:{time?:number|string|Date,timeout?:number}):Promise<ClockInfo>,advance(ms:number,options?:{timeout?:number,signal?:AbortSignal}):Promise<ClockInfo>,pause(options?:{timeout?:number}):Promise<ClockInfo>}} Clock */
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The actual sealed SDK inputs are hashed below.
const native = await import(pathToFileURL(`${sdk}/native.js`).href)
/** @type {{NativeVirtualTime:new(source:import('../src/native-browser.js').NativeBrowser)=>Clock}} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Authored staged clock module is independently typechecked and hashed.
const clockModule = await import(pathToFileURL(modulePath).href)
const inputs = await Promise.all(
  [
    executable,
    modulePath,
    `${sdk}/native-browser.js`,
    `${sdk}/native-protocol.js`,
    `${sdk}/native-page.js`,
  ].map(async (path) => ({
    path,
    sha256: createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  })),
)
/** @type {(()=>void)|undefined} */
let releaseHeld
/** @type {(()=>void)|undefined} */
let announceHeld
const server = createServer((request, response) => {
  if (request.url === '/held') {
    releaseHeld = () => response.end('released')
    announceHeld?.()
  } else {
    response.setHeader('content-type', 'text/html; charset=utf-8')
    response.end('<!doctype html><p>native time</p>')
  }
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const prefix = `http://127.0.0.1:${address.port}`
let stderr = ''
const source = await native.launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (line) => {
    stderr += line.slice(0, Math.max(0, 1048576 - stderr.length))
  },
})
const browser = new native.NativeBrowserContexts(source),
  checks = []
const observationSchema = z.object({
  date: z.number(),
  performance: z.number(),
  origin: z.number(),
  temporal: z.number().nullable(),
})
try {
  const context = await browser.newContext(),
    page = await context.newPage(),
    clock = new clockModule.NativeVirtualTime(page.source)
  await page.goto(prefix)
  const fresh = InfoSchema.parse(await clock.status())
  assert.equal(fresh.installed, false)
  for (const name of [
    'contextWide',
    'outOfProcessFrames',
    'navigation',
    'realtimeResume',
    'fixedDateWithRunningTimers',
    'systemTimeRebase',
    'fastForwardAtMostOnce',
    'animationFrameControl',
    'idleCallbackControl',
  ])
    assert.equal(fresh.capabilities[name], false)
  checks.push(
    'Native capabilities explicitly distinguish timer budgets from a full context-wide browser clock',
  )
  await page.evaluate(() => {
    Reflect.set(globalThis, 'originalTimeFunctions', [
      Date,
      setTimeout,
      clearTimeout,
      setInterval,
      clearInterval,
      requestAnimationFrame,
      cancelAnimationFrame,
      requestIdleCallback,
      /** @type {unknown} */ (Reflect.get(performance, 'now')),
    ])
  })
  const paused = InfoSchema.parse(
    await clock.installPaused({ time: new Date('2000-01-01T00:00:00Z') }),
  )
  assert(paused.installed && paused.lastAcknowledgedPaused)
  const observe = async () =>
    observationSchema.parse(
      await page.evaluate(() => {
        /** @type {number|null} */
        let temporal = null
        const candidate = /** @type {unknown} */ (Reflect.get(globalThis, 'Temporal'))
        if (candidate && typeof candidate === 'object') {
          const now = /** @type {unknown} */ (Reflect.get(candidate, 'Now'))
          if (now && typeof now === 'object') {
            const instant = /** @type {unknown} */ (Reflect.get(now, 'instant'))
            if (typeof instant === 'function') {
              const value = /** @type {unknown} */ (Reflect.apply(instant, now, []))
              if (value && typeof value === 'object') {
                const milliseconds = /** @type {unknown} */ (
                  Reflect.get(value, 'epochMilliseconds')
                )
                if (typeof milliseconds === 'number') temporal = milliseconds
              }
            }
          }
        }
        return {
          date: Date.now(),
          performance: performance.now(),
          origin: performance.timeOrigin,
          temporal,
        }
      }),
    )
  const start = await observe()
  assert.equal(start.date, 946684800000)
  assert.equal(start.temporal, 946684800000)
  await realDelay(75)
  assert.deepEqual(await observe(), start)
  assert(
    await page.evaluate(() => {
      const original = /** @type {unknown} */ (Reflect.get(globalThis, 'originalTimeFunctions'))
      return (
        Array.isArray(original) &&
        [
          Date,
          setTimeout,
          clearTimeout,
          setInterval,
          clearInterval,
          requestAnimationFrame,
          cancelAnimationFrame,
          requestIdleCallback,
          /** @type {unknown} */ (Reflect.get(performance, 'now')),
        ].every((fn, index) => fn === original[index])
      )
    }),
  )
  checks.push(
    'Actual Date/Temporal/performance freeze while every browser timer/Date function remains unchanged',
  )
  await page.evaluate(() => {
    /** @type {Array<{name:string,date:number}>} */
    const events = []
    Reflect.set(globalThis, 'timerEvents', events)
    Reflect.set(globalThis, 'intervalCount', 0)
    setTimeout(() => events.push({ name: 'timeout100', date: Date.now() }), 100)
    setTimeout(() => events.push({ name: 'timeout200', date: Date.now() }), 200)
    const interval = setInterval(() => {
      const count = Number(Reflect.get(globalThis, 'intervalCount')) + 1
      Reflect.set(globalThis, 'intervalCount', count)
      events.push({ name: 'interval', date: Date.now() })
      if (count === 10) clearInterval(interval)
    }, 50)
  })
  assert.equal((await clock.advance(500)).activeOwner, false)
  const timed = z
    .object({
      count: z.number(),
      events: z.array(z.object({ name: z.string(), date: z.number() })),
    })
    .parse(
      await page.evaluate(() => ({
        count: /** @type {unknown} */ (Reflect.get(globalThis, 'intervalCount')),
        events: /** @type {unknown} */ (Reflect.get(globalThis, 'timerEvents')),
      })),
    )
  assert.equal(timed.count, 10)
  assert.equal((await observe()).date, start.date + 500)
  assert.deepEqual(
    timed.events
      .filter((event) => event.name === 'interval')
      .map((event) => event.date - start.date),
    [50, 100, 150, 200, 250, 300, 350, 400, 450, 500],
  )
  assert(
    timed.events.findIndex((event) => event.name === 'timeout100') <
      timed.events.findIndex((event) => event.name === 'timeout200'),
  )
  await realDelay(30)
  assert.equal((await observe()).date, start.date + 500)
  checks.push(
    'Actual native timer ordering, ten repeated intervals and final policy pause complete before advance resolves',
  )
  const callbackError = new Promise((accept) =>
    page.once('pageerror', (error) => accept(error.message)),
  )
  await page.evaluate(() => {
    setTimeout(() => {
      throw new Error('Actual native virtual timer callback error')
    }, 20)
    setTimeout(() => Reflect.set(globalThis, 'callbackAfterError', true), 30)
  })
  await clock.advance(40)
  assert.match(z.string().parse(await callbackError), /Actual native virtual timer callback error/u)
  assert.equal(
    await page.evaluate(
      () => /** @type {unknown} */ (Reflect.get(globalThis, 'callbackAfterError')),
    ),
    true,
  )
  checks.push(
    'Browser callback exceptions remain actual page errors and do not prevent later native timers',
  )
  const beforeSpoof = (await observe()).date
  await page.evaluate(() => {
    performance.now = () => 1e99
  })
  await clock.advance(10)
  assert.equal((await observe()).date, beforeSpoof + 10)
  assert.equal(await page.evaluate(() => performance.now()), 1e99)
  await page.evaluate(() => {
    const original = /** @type {unknown} */ (Reflect.get(globalThis, 'originalTimeFunctions'))
    if (Array.isArray(original))
      Reflect.set(performance, 'now', /** @type {unknown} */ (original[8]))
  })
  checks.push(
    'Native protocol Timestamp, independent of author performance.now, guards actual budget completion',
  )
  await assert.rejects(clock.installPaused({ time: 0 }), /already installed/u)
  await assert.rejects(
    page.source.sendDevToolsCommand('Emulation.setVirtualTimePolicy', {
      policy: 'advance',
      budget: 100,
    }),
    /owns.*policy/u,
  )
  await assert.rejects(page.goto(`${prefix}/unsupported`), /unsupported.*virtual time/u)
  assert.equal(await page.evaluate(() => Date.now()), beforeSpoof + 10)
  checks.push(
    'Repeated rebasing, competing raw policy and unsupported navigation reject without changing the native clock',
  )
  await page.close()
  for (const mode of ['deadline', 'abort']) {
    const target = await context.newPage(),
      controlled = new clockModule.NativeVirtualTime(target.source)
    await target.goto(prefix)
    await controlled.installPaused({ time: 946684800000 })
    await target.evaluate((url) => {
      setTimeout(() => {
        const request = new XMLHttpRequest()
        request.open('GET', url, false)
        request.send()
      }, 10)
    }, `${prefix}/held`)
    const held = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    const controller = new AbortController(),
      reason = new Error('Exact native virtual-time cancellation cause')
    const first = controlled.advance(
      1000,
      mode === 'deadline' ? { timeout: 150 } : { timeout: 2000, signal: controller.signal },
    )
    const rejected = assert.rejects(
      first,
      mode === 'deadline' ? /Timeout|timed out/iu : (error) => error === reason,
    )
    await held
    if (mode === 'abort') controller.abort(reason)
    await rejected
    const next = controlled.advance(5, { timeout: 2000 })
    void next.catch(() => undefined)
    const retained = InfoSchema.parse(await controlled.status())
    assert(retained.activeOwner && retained.pendingPhysicalCommand)
    assert.equal(retained.queued, 1)
    assert(releaseHeld)
    releaseHeld()
    await next
    const idle = InfoSchema.parse(await controlled.status())
    assert.equal(idle.activeOwner, false)
    assert.equal(idle.pendingPhysicalCommand, false)
    assert.equal(idle.queued, 0)
    const actualBefore = z.number().parse(await target.evaluate(() => Date.now()))
    await controlled.advance(5)
    assert.equal(await target.evaluate(() => Date.now()), actualBefore + 5)
    await target.close()
    checks.push(
      `Original ${mode} rejects while native pause ACK retains the physical clock owner and blocks the next budget`,
    )
  }
  {
    const target = await context.newPage(),
      controlled = new clockModule.NativeVirtualTime(target.source)
    await target.goto(prefix)
    await controlled.installPaused({ time: 946684800000 })
    await target.evaluate((url) => {
      setTimeout(() => {
        const request = new XMLHttpRequest()
        request.open('GET', url, false)
        request.send()
      }, 10)
    }, `${prefix}/held`)
    const held = new Promise((accept) => {
      announceHeld = () => accept(undefined)
    })
    const closed = target.waitForEvent('close', { timeout: 4000 })
    const operation = controlled.advance(1000, { timeout: 75 })
    const failed = assert.rejects(operation, /Timeout|timed out/iu)
    await held
    await failed
    await closed
    assert(target.isClosed())
    releaseHeld?.()
    const recovered = await context.newPage()
    await recovered.goto(prefix)
    assert(
      Math.abs(z.number().parse(await recovered.evaluate(() => Date.now())) - Date.now()) < 10000,
    )
    await recovered.close()
    checks.push(
      'A never-acknowledged pause closes the actual CEF target after its native cleanup watchdog; a fresh target has real time',
    )
  }
  await context.close()
} finally {
  releaseHeld?.()
  await source.close()
  server.closeAllConnections()
  await new Promise((accept) => server.close(accept))
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
for (const input of inputs)
  assert.equal(
    createHash('sha256')
      .update(await readFile(input.path))
      .digest('hex'),
    input.sha256,
  )
await writeFile(
  `${directory}/report.json`,
  JSON.stringify(
    {
      passed: checks.length,
      checks,
      inputs,
      inputsUnchanged: true,
      scope:
        'Native current-document Chromium virtual-time budgets; full context clock and unsupported semantics remain explicit',
    },
    null,
    2,
  ) + '\n',
)
assert.equal(stderr, '')
console.log(`Actual native virtual time: ${checks.length} groups passed`)
