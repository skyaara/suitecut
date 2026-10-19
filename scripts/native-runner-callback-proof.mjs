// SPDX-License-Identifier: MIT
// Real callback executor for the native ownership tests; C++ schedules all work.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import process from 'node:process'
import { createInterface } from 'node:readline'
import { setTimeout as delay } from 'node:timers/promises'

/** @typedef {{pid:number,counter:number}} WorkerValue */
/** @typedef {{worker:WorkerValue,text:string}} PageValue */
/** @typedef {WorkerValue|PageValue|null} FixtureValue */
/** @typedef {(value:FixtureValue)=>Promise<void>} Use */
/** @typedef {{owner:string,generation:string,used:boolean,release:()=>void}} Lease */

const host = createInterface({ input: process.stdin })
/** @type {Map<string,Lease>} */
const leases = new Map()
/** @type {Map<string,FixtureValue>} */
const values = new Map()
/** @type {Set<Promise<void>>} */
const bodies = new Set()
/**
 * Write an actual callback protocol reply.
 * @param {...(string|number)} parts
 */
const reply = (...parts) => process.stdout.write(parts.join(' ') + '\n')
/**
 * Encode the actual callback error text.
 * @param {string} text
 */
const hex = (text) => Buffer.from(text).toString('hex') || '-'
let closing = false
const close = () => {
  if (closing && !leases.size && !bodies.size) {
    host.close()
    process.stdin.destroy()
  }
}
/** @type {Record<string,(dependencies:Map<string,FixtureValue>,use:Use)=>Promise<void>>} */
const fixtures = {
  async worker(_dependencies, use) {
    await use({ pid: process.pid, counter: 0 })
  },
  async page(dependencies, use) {
    const worker = dependencies.get('worker')
    assert(worker && 'counter' in worker)
    worker.counter++
    try {
      await use({ worker, text: `actual fixture value ${worker.counter}` })
    } finally {
      await delay(35)
    }
  },
  async never(_dependencies, use) {
    await new Promise(() => {
      // This fixture deliberately never settles; native ownership must survive it.
    })
    await use(null)
  },
}

for await (const line of host) {
  const [kind, token, owner, generation, name] = line.split(' ')
  if (kind === 'SETUP') {
    assert(token && owner && generation && name)
    const factory = fixtures[name]
    assert(factory && !leases.has(token))
    /** @type {(()=>void)|undefined} */
    let release
    const lease = { owner, generation, used: false, release: () => release?.() }
    leases.set(token, lease)
    void (async () => {
      try {
        await factory(values, async (value) => {
          assert(!lease.used, 'fixture use must be called once')
          lease.used = true
          values.set(name, value)
          /** @type {Promise<void>} */
          const resumed = new Promise((resolve) => {
            release = resolve
          })
          reply('READY', token, owner, generation)
          await resumed
        })
        reply('SETTLED', token, owner, generation, '1', '-')
      } catch (reason) {
        const error =
          reason instanceof Error ? reason : new Error('Fixture callback threw', { cause: reason })
        reply('SETTLED', token, owner, generation, '0', hex(error.stack ?? error.message))
      } finally {
        leases.delete(token)
        values.delete(name)
        close()
      }
    })()
  } else if (kind === 'TEARDOWN' || kind === 'CANCEL') {
    assert(token)
    const lease = leases.get(token)
    assert(lease && lease.owner === owner && lease.generation === generation)
    lease.release()
  } else if (kind === 'BODY') {
    assert(token && owner && name)
    const retry = Number(name),
      id = token
    const raw = Promise.resolve().then(() => {
      try {
        const page = values.get('page')
        assert(page && 'worker' in page)
        assert.equal(page.worker.pid, process.pid)
        assert.equal(page.worker.counter, id === '2' && retry === 0 ? 2 : 1)
        assert.match(page.text, /^actual fixture value /)
        if (id === '2' && retry === 0) throw new Error('genuine first-attempt user exception')
        reply('BODY_RESULT', id, owner, '1', '-', process.pid, page.worker.counter)
      } catch (reason) {
        const error =
          reason instanceof Error ? reason : new Error('Test callback threw', { cause: reason })
        const worker = values.get('worker')
        reply(
          'BODY_RESULT',
          id,
          owner,
          '0',
          hex(error.stack ?? error.message),
          process.pid,
          worker && 'counter' in worker ? worker.counter : 0,
        )
      }
    })
    bodies.add(raw)
    void raw.finally(() => {
      bodies.delete(raw)
      close()
    })
  } else if (kind === 'CLOSE') {
    closing = true
    close()
  } else {
    throw new Error(`Unknown native host command ${kind}`)
  }
}
