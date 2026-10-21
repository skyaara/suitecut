// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import { deferred } from '../fixtures/app.mjs'
import { test } from './helpers.mjs'
import { settle } from './helpers.mjs'
import { rejection } from './helpers.mjs'
import { wait } from './helpers.mjs'

export const races = [
  test('race.search-out-of-order', ['race', 'network', 'journey'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/search')
    await page.locator('#query').fill('old')
    await page.locator('#search').click()
    await h.fixture.gate('search-old').entered.promise
    await page.locator('#query').fill('new')
    await page.locator('#search').click()
    await h.fixture.gate('search-new').entered.promise
    h.fixture.gate('search-new').release.resolve('new result')
    await wait(page, `result.textContent==='new result'`)
    h.fixture.gate('search-old').release.resolve('stale result')
    await h.until(
      () => h.fixture.audit.find((r) => r.path === '/gate/search-old')?.ended,
      'Old response did not finish',
    )
    assert.equal(await page.locator('#result').textContent(), 'new result')
    h.check(
      'A later search remains selected after real HTTP responses finish in the opposite order',
    )
  }),
  test('race.navigation-close', ['race', 'navigation', 'lifecycle'], async (h) => {
    const { page, context } = await h.page()
    const pending = settle(page.goto(h.fixture.origin + '/gate/navigation', { timeout: 0 }))
    await h.fixture.gate('navigation').entered.promise
    await context.close({ reason: 'regression closes held navigation' })
    rejection(await pending, /regression closes held navigation|ERR_ABORTED|closed/i)
    h.fixture.gate('navigation').release.resolve('too late')
    assert.equal(await h.root.evaluate('6*7'), 42)
    h.check(
      'Server-observed in-flight navigation rejects on owner close and does not block the healthy root',
    )
  }),
  test('race.navigation-replaces-document', ['race', 'navigation', 'handles'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/old-document')
    const handle = await page.locator('#field').elementHandle()
    assert(handle)
    const pending = settle(page.evaluate(`new Promise(()=>{})`))
    await h.until(
      async () => (await page.source.runAutomation('diagnostics')).evaluationGroups > 0,
      'Evaluation was not admitted',
    )
    await page.goto(h.fixture.origin + '/replacement')
    rejection(await pending, /context|navigat|destroy|closed|releas/i)
    await assert.rejects(
      handle.fill('must not leak', { timeout: 500 }),
      /context|navigat|destroy|closed|handle|detach|releas/i,
    )
    assert.equal(await page.locator('#field').inputValue(), '')
    await handle.dispose()
    h.check(
      'Navigation cancels admitted evaluation and prevents an old handle typing into a replacement document',
    )
  }),
  test('race.detached-handle', ['race', 'handles', 'input'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/handle')
    const handle = await page.locator('#field').elementHandle()
    assert(handle)
    await handle.evaluate((node) => {
      const replacement = document.createElement('input')
      replacement.id = 'field'
      replacement.value = 'replacement'
      node.replaceWith(replacement)
    })
    await assert.rejects(handle.fill('wrong target', { timeout: 500 }), /detach|attached/i)
    assert.equal(await page.locator('#field').inputValue(), 'replacement')
    await handle.dispose()
    h.check(
      'A detached retained handle cannot silently retarget a newly inserted same-selector input',
    )
  }),
  test('race.frame-detach-binding', ['race', 'frames', 'bindings'], async (h) => {
    const { page } = await h.page()
    const entered = deferred(),
      release = deferred()
    h.onCleanup(() => release.resolve(99))
    await page.exposeFunction('heldHostCall', async () => {
      entered.resolve()
      return release.promise
    })
    await page.goto(h.fixture.origin + '/frame-host')
    const locator = page.frameLocator('#child').locator('#field')
    const pending = settle(locator.evaluate(() => globalThis.heldHostCall()))
    await entered.promise
    await page.locator('#child').evaluate((node) => node.remove())
    rejection(await pending, /detach|frame|context|destroy|clos|releas/i)
    release.resolve(99)
    await h.until(
      async () => (await page.source.runAutomation('diagnostics')).bindingCalls === 0,
      'Detached host binding did not retire',
    )
    assert.equal(await page.locator('#root').inputValue(), 'untouched')
    h.check('Detaching an OOPIF cancels its active host binding and retires its late reply')
  }),
  test('race.binding-navigation', ['race', 'bindings', 'navigation'], async (h) => {
    const { page } = await h.page()
    const entered = deferred(),
      release = deferred()
    h.onCleanup(() => release.resolve())
    await page.exposeFunction('held', async () => {
      entered.resolve()
      await release.promise
      return 'old reply'
    })
    await page.goto(h.fixture.origin + '/binding-old')
    const pending = settle(page.evaluate('held()'))
    await entered.promise
    await page.goto(h.fixture.origin + '/binding-new')
    rejection(await pending, /context|navigat|destroy|clos|releas/i)
    release.resolve()
    assert.equal(await page.evaluate('globalThis.oldReply??null'), null)
    await h.until(
      async () => (await page.source.runAutomation('diagnostics')).bindingCalls === 0,
      'Old binding still owned',
    )
    h.check(
      'An admitted host callback cannot resolve into the document created by a later navigation',
    )
  }),
  test('race.route-close-late-reply', ['race', 'network', 'lifecycle'], async (h) => {
    const { page, context } = await h.page()
    const entered = deferred(),
      release = deferred(),
      completed = deferred()
    h.onCleanup(() => release.resolve())
    let late
    await page.route('**/held-route', async (route) => {
      entered.resolve()
      await release.promise
      late = await settle(route.fulfill({ status: 200, body: 'late route' }))
      completed.resolve()
    })
    await page.goto(h.fixture.origin + '/route-app')
    const pending = settle(page.evaluate(`fetch('/held-route').then(r=>r.text())`))
    await entered.promise
    await context.close({ reason: 'route owner gone' })
    release.resolve()
    await completed.promise
    rejection(await pending, /route owner gone|closed|context|destroy/i)
    assert(late, 'Callback must physically finish after release')
    // NativeRoute intentionally ignores already-retired replies in some transports.
    // Both an explicit closure rejection and a retired no-op are safe outcomes.
    if (late.status === 'rejected') assert.match(late.error, /closed|context|route|cancel|handled/i)
    assert.equal(await h.root.evaluate('40+2'), 42)
    h.check(
      'Closing a paused request retires its response; the host callback drains and healthy pages remain usable',
    )
  }),
  test('race.chunked-body-sibling-navigation', ['race', 'network'], async (h) => {
    const { page, context } = await h.page()
    const sibling = await context.newPage()
    await page.goto(h.fixture.origin + '/body-reader')
    const responsePending = page.waitForResponse(h.fixture.origin + '/gate/body?headers=1')
    await page.evaluate(
      `void fetch('/gate/body?headers=1').then(r=>r.text()).then(t=>globalThis.bodyText=t)`,
    )
    await h.fixture.gate('body').entered.promise
    const response = await responsePending,
      pending = response.body()
    await sibling.goto(h.fixture.cross + '/sibling-new')
    h.fixture.gate('body').release.resolve('λ-body-after-sibling-navigation')
    assert.equal((await pending).toString(), 'prefix:λ-body-after-sibling-navigation')
    await wait(page, `globalThis.bodyText==='prefix:λ-body-after-sibling-navigation'`)
    h.check(
      'A chunked body retains its original page/request owner while a sibling changes renderer',
    )
  }),
  test('race.parallel-command-identity', ['race', 'transport'], async (h) => {
    const a = await h.page(),
      b = await h.page()
    await Promise.all([a.page.goto(h.fixture.origin + '/a'), b.page.goto(h.fixture.origin + '/b')])
    const jobs = Array.from({ length: 24 }, (_, i) => ({
      page: i % 2 ? a.page : b.page,
      id: `${h.seed}:${i}`,
    }))
    const values = await Promise.all(
      jobs.map(({ page, id }) =>
        page.evaluate(`Promise.resolve({id:${JSON.stringify(id)},path:location.pathname})`),
      ),
    )
    for (let i = 0; i < jobs.length; ++i)
      assert.deepEqual(values[i], { id: jobs[i].id, path: i % 2 ? '/a' : '/b' })
    h.check('24 concurrently outstanding replies retain command and page identity')
  }),
  test('race.close-at-command-capacity', ['race', 'transport', 'lifecycle'], async (h) => {
    const { page, context } = await h.page()
    await page.goto(h.fixture.origin + '/capacity')
    const pending = Array.from({ length: 63 }, () => settle(page.waitForNavigation({ timeout: 0 })))
    // Observe admission while one ordinary slot remains. A diagnostics request
    // at 64/64 would itself be correctly refused by the bounded command queue.
    await h.until(
      async () => (await page.source.runAutomation('diagnostics')).navigations === 63,
      '63 native waits were not admitted',
    )
    pending.push(settle(page.waitForNavigation({ timeout: 0 })))
    await context.close({ reason: 'capacity-owner-close' })
    for (const result of await Promise.all(pending))
      rejection(result, /capacity-owner-close|closed/i)
    assert.equal(await h.root.evaluate('42'), 42)
    h.check(
      'Owner cleanup remains admissible while all 64 ordinary native navigation slots are occupied',
    )
  }),
]
