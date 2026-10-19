// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { inspect } from 'node:util'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeConsoleMessage } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-console')
await mkdir(directory, { recursive: true })
const paths = [
  executable,
  'dist/native-console.js',
  'dist/native-page.js',
  'dist/native-browser.js',
  'dist/native-protocol.js',
]
const hashes = await Promise.all(
  paths.map(async (path) =>
    createHash('sha256')
      .update(await readFile(path))
      .digest('hex'),
  ),
)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 320,
  height: 180,
  headless: true,
  onDiagnostic(message) {
    stderr = (stderr + message).slice(-1048576)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  const observed = z.array(z.instanceof(NativeConsoleMessage)).parse([])
  page.on('console', (message) => observed.push(message))
  const before = Date.now() - 100
  await page.evaluate(() => console.log('hello', 5, { foo: 'bar' }))
  const message = observed[0]
  assert(message)
  assert.equal(message.text(), 'hello 5 {foo: bar}')
  assert.equal(inspect(message), message.text())
  assert(message.timestamp() >= before && message.timestamp() <= Date.now() + 100)
  const args = message.args()
  assert.equal(args.length, 3)
  await page.clearConsoleMessages()
  assert.deepEqual(await Promise.all(args.map((arg) => arg.jsonValue())), [
    'hello',
    5,
    { foo: 'bar' },
  ])
  await Promise.all(args.map((arg) => arg.dispose()))
  checks.push(
    'real CDP argument handles survive immediate clear before materialization, with actual timestamp and inspection text',
  )

  await page.evaluate(() => {
    const value = { foo: 'before' }
    Reflect.set(globalThis, '__suitecutConsoleObject', value)
    console.log(value)
  })
  const objectMessage = observed.at(-1)
  assert(objectMessage)
  const object = objectMessage.args()[0]
  assert(object)
  await page.evaluate("globalThis.__suitecutConsoleObject.foo = 'after'")
  await page.clearConsoleMessages()
  assert.deepEqual(await object.jsonValue(), { foo: 'after' })
  await object.dispose()
  checks.push(
    'retained console object observes subsequent mutation of original renderer identity, not a reconstructed preview',
  )

  observed.length = 0
  await page.evaluate(() => {
    for (let index = 0; index < 301; ++index) console.log('message' + index)
  })
  assert.equal(observed.length, 301)
  for (let index = 1; index < observed.length; ++index)
    assert(observed[index].timestamp() >= observed[index - 1].timestamp())
  const journal = await page.consoleMessages()
  assert.equal(journal.length, 100)
  assert.equal(journal[0].text(), 'message201')
  assert.equal(journal.at(-1)?.text(), 'message300')
  await page.clearConsoleMessages()
  assert.equal((await page.consoleMessages()).length, 0)
  checks.push(
    '301 synchronous native logs retain exact last100 journal and coalesced delivery/claims recover on clear',
  )

  await page.evaluate(() => console.log('before navigation'))
  await page.goto('data:text/html,<body>new document</body>')
  await page.evaluate(() => console.log('after navigation'))
  assert(
    (await page.consoleMessages({ filter: 'all' })).some(
      (value) => value.text() === 'before navigation',
    ),
  )
  assert.deepEqual(
    (await page.consoleMessages()).map((value) => value.text()),
    ['after navigation'],
  )
  const errors = z.array(z.instanceof(Error)).parse([])
  page.on('pageerror', (error) => errors.push(error))
  await page.goto('data:text/html,<script>throw new Error("first actual error")</script>')
  await page.goto('data:text/html,<script>throw new Error("second actual error")</script>')
  assert.deepEqual(
    errors.map((error) => error.message),
    ['first actual error', 'second actual error'],
  )
  assert.deepEqual(
    (await page.pageErrors({ filter: 'all' })).map((error) => error.message),
    ['first actual error', 'second actual error'],
  )
  assert.deepEqual(
    (await page.pageErrors()).map((error) => error.message),
    ['second actual error'],
  )
  assert(errors.every((error) => error.stack?.includes('data:text/html')))
  await page.clearConsoleMessages()
  const diagnostics = z
    .object({
      activeHandles: z.number(),
      pendingConsoleArguments: z.number(),
      consoleArgumentBytes: z.number(),
      consoleMessages: z.number(),
      consoleJournalBytes: z.number(),
      pendingConsoleMaterializations: z.number(),
      otherOperations: z.number(),
      pendingDevTools: z.number(),
      pageErrors: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.activeHandles, 0)
  assert.equal(diagnostics.pendingConsoleArguments, 0)
  assert.equal(diagnostics.consoleArgumentBytes, 0)
  assert.equal(diagnostics.consoleMessages, 0)
  assert.equal(diagnostics.consoleJournalBytes, 0)
  assert.equal(diagnostics.pendingConsoleMaterializations, 0)
  assert.equal(diagnostics.otherOperations, 0)
  assert.equal(diagnostics.pendingDevTools, 0)
  assert.equal(diagnostics.pageErrors, 2)
  checks.push(
    'native main-page navigation filters console/error journals; actual Runtime errors retain original stack and transport identity',
  )
  for (let index = 0; index < paths.length; ++index)
    assert.equal(
      createHash('sha256')
        .update(await readFile(paths[index]))
        .digest('hex'),
      hashes[index],
      'Console proof runtime changed',
    )
  const report = {
    status: 'passed',
    checks,
    cefVersion: source.cefVersion,
    paths,
    hashes,
    diagnostics,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, stderr)
}
