// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = '.suitecut/native-value-intrinsics'
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  await page.goto(
    'data:text/html;charset=utf-8,' +
      encodeURIComponent(
        '<script>Object.create=null;Object.defineProperty=null;Object.getOwnPropertyDescriptor=null;Object.getOwnPropertyNames=null;Object.getPrototypeOf=null;Object.prototype.hasOwnProperty=null;</script><iframe srcdoc="<p>Actual child</p>"></iframe>',
      ),
  )
  assert.equal(await page.evaluate('1+2'), 3)
  checks.push(
    'actual author scripts replace legacy Object descriptor helpers before evaluation, while primitive native result serialization remains correct',
  )
  const handle = await page.evaluateHandle('1+2')
  assert.equal(await handle.jsonValue(), 3)
  await handle.dispose()
  checks.push(
    'actual retained primitive handle serialization and disposal preserve native value semantics under the same replacement',
  )
  const key = 'own\ud800\0\\"',
    value = { nested: ['actual\udfff', undefined], ['__proto__']: 'own', [key]: key }
  assert.deepEqual(await page.evaluate((input) => input, value), value)
  checks.push(
    'native typed object and array arguments create exact own fields including __proto__ and isolated UTF16 metadata without author Object.defineProperty',
  )
  assert.equal(await page.evaluate('globalThis'), 'ref: <Window>')
  await page.locator('iframe').waitFor({ timeout: 1500 })
  assert.equal(
    await page.evaluate(() => document.querySelector('iframe').contentWindow),
    'ref: <Window>',
  )
  checks.push(
    'actual same-realm and cross-realm Window receivers still pass genuine platform branding',
  )
  const fake = { window: null, caption: 'author' }
  const expected = { window: null, caption: 'author' }
  assert.deepEqual(await page.evaluate((value) => value, fake), expected)
  const spoof = await page.evaluate(() => {
    const value = { window: null, [Symbol.toStringTag]: 'Window' }
    value.window = value
    return value
  })
  assert(spoof && typeof spoof === 'object' && Reflect.get(spoof, 'window') === spoof)
  checks.push(
    'ordinary author self-window and Symbol.toStringTag values remain author data rather than native Window captions',
  )
  await assert.rejects(
    page.evaluate('({get actual(){throw new Error("Actual enumerable getter failure")}})'),
    /Actual enumerable getter failure/,
  )
  checks.push(
    'genuine enumerable author getter failures remain errors under descriptor replacement',
  )
  const schema = z.object({
    otherOperations: z.number(),
    activeHandles: z.number(),
    pendingHandles: z.number(),
    evaluationGroups: z.number(),
    nativeSelectorWaits: z.number(),
    nativeSelectorWaitPhysical: z.number(),
  })
  for (let attempt = 0; attempt < 200; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const state = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, count] of Object.entries(state)) assert.equal(count, 0, name)
  checks.push(
    'all actual value/handle/native preparation and group ownership retires after genuine success and serialization errors',
  )
  await writeFile(directory + '/report.json', JSON.stringify({ checks, state }, null, 2) + '\n')
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native value intrinsic diagnostics must remain empty')
