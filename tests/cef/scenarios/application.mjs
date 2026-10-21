// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { digest } from '../fixtures/app.mjs'
import { inventory } from '../fixtures/app.mjs'
import { test } from './helpers.mjs'
import { settle } from './helpers.mjs'
import { wait } from './helpers.mjs'
import { login } from './helpers.mjs'

export const application = [
  test('commerce.checkout', ['smoke', 'journey', 'storage', 'network', 'input'], async (h) => {
    const { page } = await h.page()
    await login(h, page)
    await page.locator('#quantity').fill('0')
    await page.locator('#checkout').click()
    await wait(page, `document.querySelector('#order').textContent==='Quantity must be 1–20'`)
    await page.locator('#quantity').fill('3')
    await page.locator('#checkout').click()
    await wait(page, `document.querySelector('#order').textContent.includes('"total":7497')`)
    await page.locator('#checkout').click()
    assert.equal(h.fixture.orders.size, 1, 'Repeat input must not duplicate a committed order')
    const events = await page.evaluate('globalThis.trusted')
    assert(
      events.some((e) => e.type === 'click' && e.trusted) &&
        events.some((e) => e.type === 'submit' && e.trusted),
    )
    h.check(
      'Validation, native form submission, authenticated checkout and idempotent order persist on the server',
    )
  }),
  test('auth.context-isolation', ['smoke', 'journey', 'storage'], async (h) => {
    const a = await h.page(),
      b = await h.page()
    await login(h, a.page, 'alice@example.test')
    await login(h, b.page, 'bob@example.test')
    await Promise.all([a.page.reload(), b.page.reload()])
    await wait(a.page, `identity.textContent==='alice@example.test'`)
    await wait(b.page, `identity.textContent==='bob@example.test'`)
    assert.notDeepEqual(await a.context.cookies(), await b.context.cookies())
    assert.equal(
      await a.page.evaluate('document.cookie.includes("session=")'),
      false,
      'HttpOnly session must not become a page credential',
    )
    await a.page.locator('#logout').click()
    await wait(a.page, `identity.textContent==='Guest'`)
    assert.equal(await b.page.locator('#identity').textContent(), 'bob@example.test')
    h.check('Concurrent users, reload and logout retain separate actual cookie jars')
  }),
  test('auth.storage-restore', ['journey', 'storage'], async (h) => {
    const { page, context } = await h.page()
    await login(h, page)
    await page.evaluate(
      `localStorage.setItem('draft','Résumé 漢字 🙂');sessionStorage.setItem('tab-secret','ephemeral')`,
    )
    const state = await context.storageState()
    const restored = await h.page({ storageState: state })
    await restored.page.goto(h.fixture.origin + '/app')
    await wait(restored.page, `identity.textContent==='operator@example.test'`)
    assert.equal(await restored.page.evaluate(`localStorage.getItem('draft')`), 'Résumé 漢字 🙂')
    assert.equal(await restored.page.evaluate(`sessionStorage.getItem('tab-secret')`), null)
    h.check(
      'Exported HttpOnly auth and Unicode local storage restore without copying per-tab session storage',
    )
  }),
  test('editor.shadow-unicode', ['smoke', 'journey', 'input', 'selectors'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/editor')
    await page.locator('#subject').fill('Résumé – 客服 🙂')
    await page.locator('#editor').fill('First line\nSecond λ 漢字 🙂')
    await page.locator('#tag').fill('urgent-λ')
    await page.getByRole('button', { name: 'Apply tag' }).click()
    assert.equal(await page.locator('#tagged').textContent(), 'urgent-λ')
    await page.locator('#save').click()
    const saved = await page.evaluate(`JSON.parse(localStorage.getItem('draft'))`)
    assert.equal(saved.subject, 'Résumé – 客服 🙂')
    assert(saved.body.includes('Second λ 漢字 🙂'))
    h.check(
      'Shadow DOM selectors, contenteditable and Unicode input save genuine application state',
    )
  }),
  test(
    'files.upload-download-integrity',
    ['smoke', 'journey', 'artifacts', 'network'],
    async (h) => {
      const { page } = await h.page()
      await page.goto(h.fixture.origin + '/app')
      const file = join(h.output, 'inventory-λ.csv')
      await writeFile(file, inventory)
      await page.locator('#inventory').setInputFiles(file)
      await page.locator('#upload').click()
      await wait(page, `document.querySelector('#uploaded').textContent.includes('sha256')`)
      assert.deepEqual(JSON.parse(await page.locator('#uploaded').textContent()), {
        bytes: inventory.length,
        sha256: digest(inventory),
      })
      const pending = page.waitForEvent('download', { timeout: 8000 })
      await page.locator('#download').click()
      const download = await pending
      const saved = join(h.output, 'export.csv')
      await download.saveAs(saved)
      assert.equal(digest(await readFile(saved)), digest(inventory))
      assert.equal(download.suggestedFilename(), 'inventory-λ.csv')
      h.check(
        'Native file selection, real HTTP upload and downloaded artifact preserve exact bytes and Unicode filename',
      )
    },
  ),
  test('popup.approval-workflow', ['journey', 'lifecycle', 'input'], async (h) => {
    const { page, context } = await h.page()
    await login(h, page)
    const pending = page.waitForEvent('popup', { timeout: 8000 })
    await page.locator('#popup').click()
    const popup = await pending
    await popup.waitForLoadState('domcontentloaded')
    assert.equal(
      (await popup.context().cookies()).find((c) => c.name === 'session')?.value,
      (await context.cookies()).find((c) => c.name === 'session')?.value,
    )
    const closed = popup.waitForEvent('close', { timeout: 8000 })
    const action = settle(popup.locator('#approve').click())
    await closed
    const clickResult = await action
    // A real window.close() can beat the final input receipt. Require the
    // actual close event and approved opener state, never arbitrary timeouts.
    if (clickResult.status === 'rejected') assert.match(clickResult.error, /Native page is closed/)
    await wait(page, `document.querySelector('#approved').textContent==='Approved'`)
    assert(!context.pages().includes(popup))
    h.check('Popup shares its owner auth, posts an approval and retires without closing its opener')
  }),
  test('navigation.history-redirect', ['smoke', 'journey', 'navigation'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/first')
    await page.goto(h.fixture.origin + '/redirect')
    assert.equal(page.url(), h.fixture.origin + '/app')
    await page.goBack()
    assert.equal(page.url(), h.fixture.origin + '/first')
    await page.goForward()
    assert.equal(page.url(), h.fixture.origin + '/app')
    await page.reload()
    await page.locator('#email').fill('after-history@example.test')
    assert.equal(await page.locator('#email').inputValue(), 'after-history@example.test')
    h.check(
      'Redirect, history back/forward, reload and fresh-document actions use the correct navigation identity',
    )
  }),
  test(
    'lifecycle.repeated-contexts',
    ['race', 'lifecycle', 'stress'],
    async (h) => {
      for (let i = 0; i < 12; ++i) {
        const { page, context } = await h.page()
        await page.goto(h.fixture.origin + '/cycle-' + i)
        await page.locator('#field').fill(`cycle-${h.seed}-${i}`)
        assert.equal(await page.locator('#field').inputValue(), `cycle-${h.seed}-${i}`)
        await context.close()
      }
      assert.equal(await h.root.evaluate('6*7'), 42)
      const actual = await h.source.runContext('list')
      h.observe('context-inventory-after-cycles', actual)
      h.check(
        'Twelve create/navigate/type/close cycles preserve transport responsiveness and expose final native inventory',
      )
    },
    120,
  ),
  test('dialogs.prompt-and-navigation', ['journey', 'input', 'lifecycle'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/dialogs')
    const dialog = page.waitForEvent('dialog', { timeout: 8000 })
    const pending = page.evaluate(`prompt('Approval note','draft')`)
    const actual = await dialog
    assert.equal(actual.type(), 'prompt')
    await actual.accept('Approved λ')
    assert.equal(await pending, 'Approved λ')
    await page.goto(h.fixture.origin + '/after-dialog')
    await page.locator('#button').click()
    assert.equal(await page.evaluate('globalThis.clicks'), 1)
    h.check(
      'Native prompt delivery/acceptance completes evaluation and leaves navigation/input usable',
    )
  }),
]
