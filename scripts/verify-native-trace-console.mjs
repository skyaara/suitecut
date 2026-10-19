// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

import { parseTraceRaw } from './native-trace-reader.mjs'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable && isAbsolute(executable))
const directory = resolve(
  process.env.SUITECUT_NATIVE_TRACE_CONSOLE_PROOF ?? '.suitecut/native-trace-console',
)
await mkdir(directory, { recursive: true })
const Console = z.object({
  type: z.literal('console'),
  text: z.string(),
  messageType: z.string(),
  pageId: z.string(),
  time: z.number(),
  args: z.array(z.object({ preview: z.string(), value: z.unknown().optional() })),
})
const checks = []
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic(message) {
    stderr += z.string().parse(message)
  },
})
try {
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const page = await context.newPage()
  await context.tracing.start()
  let count = 0
  const observed = page.waitForEvent('console', {
    timeout: 10_000,
    predicate: (message) => message.text() === 'actual-native-99',
  })
  page.on('console', (message) => {
    if (message.text().startsWith('actual-native-')) ++count
  })
  await page.evaluate(`setTimeout(()=>{for(let i=0;i<100;++i)console.log('actual-native-'+i)},0)`)
  await observed
  assert.equal(count, 100)
  const firstPath = resolve(directory, 'first.zip')
  await context.tracing.stopChunk({ path: firstPath })
  const first = await parseTraceRaw(firstPath)
  const events = first.events
    .filter((event) => event.type === 'console')
    .map((event) => Console.parse(event))
  assert.equal(events.length, 100)
  assert.deepEqual(
    events.map((event) => event.text),
    Array.from({ length: 100 }, (_, i) => `actual-native-${i}`),
  )
  assert(
    events.every(
      (event) => event.messageType === 'log' && event.pageId === `page@${page.source.pageId}`,
    ),
  )
  checks.push('all 100 actual asynchronous console callbacks precede stopChunk archive publication')
  await context.tracing.startChunk()
  const text = 'actual-UTF16-\ud800-\udc00-\udfff-\0'
  await page.evaluate(
    `console.log(${JSON.stringify(text)},true,42,null,({__suitecutNativeUTF16Scalar:'d800'}))`,
  )
  const secondPath = resolve(directory, 'second.zip')
  await context.tracing.stopChunk({ path: secondPath })
  const second = await parseTraceRaw(secondPath)
  const exact = Console.parse(second.events.find((event) => event.type === 'console'))
  assert(exact.text.startsWith(text))
  assert.equal(exact.args[0]?.value, text)
  assert.equal(exact.args[1]?.value, true)
  assert.equal(exact.args[2]?.value, 42)
  assert.equal(exact.args[3]?.value, null)
  assert.equal(exact.args[4]?.value, undefined)
  assert.equal(second.events.filter((event) => event.type === 'console').length, 1)
  checks.push(
    'typed UTF16 console scalars and primitive values survive the actual ZIP; author object is not generically decoded',
  )
  checks.push('console events belong only to the exact chunk that received them')
  await context.tracing.startChunk()
  const dialogClosed = page.waitForEvent('dialogclosed', { timeout: 10_000 })
  let dialogReply = Promise.resolve()
  let answered = 0
  page.on('dialog', (dialog) => {
    ++answered
    dialogReply = dialog.dismiss()
  })
  await page.evaluate('alert("actual dialog stack")')
  await dialogClosed
  await dialogReply
  assert.equal(answered, 1)
  const thirdPath = resolve(directory, 'dialog.zip')
  await context.tracing.stop({ path: thirdPath })
  const third = await parseTraceRaw(thirdPath)
  const dialog = third.actionObjects.find((action) => action.class === 'Dialog')
  assert(dialog)
  const stack = z
    .array(z.object({ file: z.string(), line: z.number(), column: z.number() }))
    .parse(third.stacks.get(z.string().parse(dialog.callId)))
  assert(
    stack.length > 0 &&
      stack.every(
        (frame) =>
          frame.file === new URL(import.meta.url).pathname && frame.line > 0 && frame.column > 0,
      ),
  )
  checks.push('actual dialog API action records the real caller stack')
  await context.close()
} finally {
  await source.close()
}
assert.equal(stderr, '')
const report = {
  checks,
  stderr,
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
process.stdout.write(JSON.stringify(report) + '\n')
