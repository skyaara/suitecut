// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'

export const test = (id, tags, run, timeout = 90) => ({ id, tags, run, timeout })
export const settle = (promise) =>
  promise.then(
    (value) => ({ status: 'fulfilled', value }),
    (error) => ({ status: 'rejected', error: String(error) }),
  )
export const rejection = (result, pattern) => {
  assert.equal(result.status, 'rejected', JSON.stringify(result))
  assert.match(result.error, pattern)
}
export const wait = async (page, expression) => {
  const handle = await page.waitForFunction(expression, undefined, { timeout: 8000 })
  await handle.dispose()
}
export const login = async (h, page, email = 'operator@example.test') => {
  await page.goto(h.fixture.origin + '/app')
  await page.getByLabel('Email', { exact: true }).fill(email)
  await page.getByLabel('Password', { exact: true }).fill('local-fixture-password')
  await page.getByRole('button', { name: 'Sign in', exact: true }).click()
  await wait(page, `document.querySelector('#identity').textContent===${JSON.stringify(email)}`)
}
