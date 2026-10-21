// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { test } from './helpers.mjs'

export const frames = [
  test('frames.cross-origin-input', ['smoke', 'journey', 'frames', 'input'], async (h) => {
    const { page } = await h.page()
    await page.goto(h.fixture.origin + '/frame-host')
    const remote = page.frameLocator('#child').locator('#field')
    await remote.fill('Remote λ')
    await page.frameLocator('#child').locator('#send').click()
    assert.equal(await remote.evaluate(() => globalThis.sent), 'Remote λ')
    assert.equal(await page.locator('#root').inputValue(), 'untouched')
    assert.equal(await page.frameLocator('#sibling').locator('#field').inputValue(), 'child')
    const targets = await h.source.sendDevToolsCommand('Target.getTargets')
    assert(
      targets.targetInfos.some((t) => t.type === 'iframe' && t.url === h.fixture.cross + '/child'),
    )
    h.check(
      'Native input reaches an actual OOPIF without changing the root or same-selector sibling',
    )
  }),
  test('bootstrap.restricted-frame-tree', ['smoke', 'race', 'bootstrap', 'frames'], async (h) => {
    const { page, context } = await h.page()
    for (const version of [h.seed, h.seed + 1, h.seed + 2]) {
      await context.addInitScript(`globalThis.regressionSeed=${version};void 0`)
      await page.goto(h.fixture.origin + '/restricted')
      const expected = { seed: version, early: true, leaked: false, xhrBlocked: true }
      const main = await page.evaluate('globalThis.firstAuthor')
      h.observe('first-author-main', { version, actual: main })
      assert.deepEqual(main, expected)
      const child = page.frameLocator('#remote')
      for (const [name, locator] of [
        ['local', page.frameLocator('#local').locator('#field')],
        ['remote', child.locator('#field')],
        ['nested', child.frameLocator('#nested').locator('#field')],
      ]) {
        const actual = await locator.evaluate(() => globalThis.firstAuthor)
        h.observe('first-author-' + name, { version, actual })
        assert.deepEqual(actual, expected)
      }
    }
    h.check(
      'Three ACKed init mutations reach first-author main/local/remote/nested documents while sync-XHR policy remains enforced',
    )
  }),
]
