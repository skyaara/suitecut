// SPDX-License-Identifier: MIT
/* global setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-page-errors-utf16')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html;charset=utf-8')
  if (request.url === '/favicon.ico') {
    response.writeHead(204)
    response.end()
    return
  }
  response.end('<body>Native actual error source</body>')
})
await new Promise((done) => server.listen(0, '0.0.0.0', () => done(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const base = `http://localhost:${address.port}`
const checks = []
try {
  const page = createNativePage(source)
  await page.goto(base)
  const value = '\ud800a\0\\"\udfff😀'
  let observed = ''
  const unsubscribe = source.onPageErrorMessage?.((event) => {
    observed += JSON.stringify(event) + '\n'
  })
  assert(unsubscribe)
  try {
    for (const [name, message, customStack] of [
      ['Error', 'ordinary complete error', undefined],
      ['Kind: ' + value, value + '\ncomplete second line', 'independent custom stack\n' + value],
      ['Name'.repeat(300) + value, 'complete long-name message', undefined],
    ]) {
      const wait = page.waitForEvent('pageerror', {
        predicate: (event) => event.message === message,
        timeout: 5000,
      })
      await page.evaluate(
        ({ name, message, customStack }) => {
          setTimeout(() => {
            const error = new Error(message)
            error.name = name
            if (customStack !== undefined) error.stack = customStack
            Reflect.set(globalThis, 'actualThrownMetadata', {
              name: error.name,
              message: error.message,
              stack: error.stack,
            })
            throw error
          }, 0)
        },
        { name, message, customStack },
      )
      const event = await wait
      const actual = z
        .object({ name: z.string(), message: z.string(), stack: z.string() })
        .parse(
          JSON.parse(
            await page.evaluate(
              () => JSON.stringify(Reflect.get(globalThis, 'actualThrownMetadata')) ?? '',
            ),
          ),
        )
      assert.equal(event.name, actual.name)
      assert.equal(event.message, actual.message)
      assert.equal(event.stack, actual.stack)
    }
    checks.push(
      'complete genuine error descriptors preserve ordinary/custom/long names, multiline messages and independent stacks without preview inference',
    )
    const stringWait = page.waitForEvent('pageerror', {
      predicate: (event) => event.message === value,
      timeout: 5000,
    })
    await page.evaluate('setTimeout(()=>{throw ' + JSON.stringify(value) + '},0)')
    assert.equal((await stringWait).message, value)
    checks.push(
      'genuine thrown primitive STRING retains isolated units, NUL, backslash, quote and valid pair',
    )
    const authorWait = page.waitForEvent('pageerror', { timeout: 5000 })
    await page.evaluate("setTimeout(()=>{throw {__suitecutNativeUTF16Scalar:'d800'}},0)")
    assert.notEqual((await authorWait).message, '\ud800')
    checks.push(
      'ordinary thrown author tag object remains opaque and cannot become a native string sidecar',
    )
    await page.setContent(`<iframe src="http://127.0.0.1:${address.port}/child"></iframe>`)
    const child = page.frameLocator('iframe')
    await child.locator('body').waitFor({ timeout: 5000 })
    const frame = page.frames().find((entry) => entry.parentFrame())
    assert(frame)
    const childWait = page.waitForEvent('pageerror', {
      predicate: (event) => event.message === value + 'child',
      timeout: 5000,
    })
    await frame.evaluate((text) => {
      setTimeout(() => {
        const error = new Error(text)
        error.stack = 'actual child stack ' + text
        throw error
      }, 0)
    }, value + 'child')
    assert.equal((await childWait).stack, 'actual child stack ' + value + 'child')
    const packets = observed
      .trim()
      .split('\n')
      .map((line) =>
        z
          .object({
            name: z.string(),
            message: z.string(),
            stack: z.string(),
            frame: z.object({ id: z.string() }).optional(),
          })
          .parse(JSON.parse(line)),
      )
    const childMetadata = packets.find((event) => event.message === value + 'child')
    const childInfo = source
      .frameInfos?.()
      .find((entry) => entry.parentId && entry.url.includes('/child'))
    assert(childInfo)
    assert.equal(childMetadata?.frame?.id, childInfo.id)
    checks.push(
      'genuine child renderer session and exact execution-context frame identity survive asynchronous descriptor capture',
    )
    const journal = await page.pageErrors({ filter: 'all' })
    for (const event of packets) {
      assert(
        journal.some(
          (entry) =>
            entry.name === event.name &&
            entry.message === event.message &&
            entry.stack === event.stack,
        ),
      )
    }
    checks.push(
      'listener, native event predicate and bounded error journal retain the same complete native metadata',
    )
  } finally {
    unsubscribe()
  }
  await page.waitForTimeout(20)
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      pendingPageErrorCaptures: z.number(),
      pendingPageErrorPhysical: z.number(),
      pageErrorRetainedBytes: z.number(),
      pageErrorGroups: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const count of Object.values(state)) assert.equal(count, 0)
  const report = {
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await new Promise((done) => server.close(done))
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
