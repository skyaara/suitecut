// SPDX-License-Identifier: MIT
/* global matchMedia */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const iterations = z.coerce
  .number()
  .int()
  .min(1)
  .max(10000)
  .parse(process.argv[2] ?? 100)
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const binarySHA256 = await hash()
const queries = [
  'print',
  '(prefers-color-scheme: dark)',
  '(prefers-reduced-motion: reduce)',
  '(forced-colors: active)',
  '(prefers-contrast: more)',
]
const beforeAuthor = `<script>globalThis.beforeAuthorMedia=${JSON.stringify(queries)}.map(q=>matchMedia(q).matches)</script>`
let origin = ''
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.setHeader('Cache-Control', 'no-store')
  response.end(
    `<link rel=icon href=data:,>${beforeAuthor}` +
      (request.url === '/child'
        ? '<p>child</p>'
        : `<iframe name=same src=/child></iframe><iframe name=cross src="${origin.replace('127.0.0.1', '127.0.0.2')}/child"></iframe>`),
  )
})
await new Promise((done) => server.listen(0, '0.0.0.0', done))
const address = server.address()
assert(address && typeof address !== 'string')
origin = `http://127.0.0.1:${address.port}`
let nativeStderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  onDiagnostic: (message) => {
    nativeStderr += message
  },
})
const page = createNativePage(source)
const read = () =>
  Promise.all(
    page.frames().map(async (frame) => ({
      name: frame.name(),
      values: await frame.evaluate(
        (conditions) => conditions.map((query) => matchMedia(query).matches),
        queries,
      ),
    })),
  )
const checks = []
try {
  await page.goto(origin)
  const baseline = await read()
  assert.deepEqual(baseline.map((frame) => frame.name).sort(), ['', 'cross', 'same'])
  await page.emulateMedia({
    media: 'print',
    colorScheme: 'dark',
    reducedMotion: 'reduce',
    forcedColors: 'active',
    contrast: 'more',
  })
  const overridden = await read()
  for (const frame of overridden)
    assert.deepEqual(frame.values, [true, true, true, true, true], frame.name)
  checks.push(
    'all five actual CSS media overrides reach main, same-process and cross-origin frames',
  )

  await page.emulateMedia({ media: 'screen' })
  for (const frame of await read())
    assert.deepEqual(frame.values, [false, true, true, true, true], frame.name)
  checks.push('omitted settings retain their real previously applied values')
  await page.goto(origin)
  for (const frame of await read())
    assert.deepEqual(frame.values, [false, true, true, true, true], frame.name)
  for (const frame of page.frames()) {
    const encoded = z
      .string()
      .parse(
        await frame.evaluate(() => JSON.stringify(Reflect.get(globalThis, 'beforeAuthorMedia'))),
      )
    const actual = z.array(z.boolean()).length(5).parse(JSON.parse(encoded))
    assert.deepEqual(
      actual,
      [false, true, true, true, true],
      `${frame.name()} before author execution`,
    )
  }
  checks.push(
    'new actual renderer targets observe persisted overrides before their first author script',
  )
  await page.emulateMedia({
    media: null,
    colorScheme: null,
    reducedMotion: null,
    forcedColors: null,
    contrast: null,
  })
  const restored = await read()
  assert.deepEqual(restored, baseline)
  checks.push('null resets each actual frame to its measured platform defaults')
  for (let iteration = 0; iteration < iterations; ++iteration) {
    const dark = iteration % 2 === 0
    await page.emulateMedia({ colorScheme: dark ? 'dark' : 'light' })
    for (const frame of await read()) assert.equal(frame.values[1], dark, frame.name)
  }
  checks.push(`${iterations} actual cross-frame media update cycles retain matching state`)
  const diagnostics = z
    .object({
      otherOperations: z.literal(0),
      artifacts: z.literal(0),
      artifactPhysicalCapture: z.literal(0),
      pendingMediaPhysicalCommands: z.literal(0),
      queuedMediaCommands: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(nativeStderr, '')
  assert.equal(await hash(), binarySHA256)
  const directory = resolve('.suitecut/native-media-emulation')
  await mkdir(directory, { recursive: true })
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: source.cefVersion,
        checks,
        baseline,
        overridden,
        restored,
        diagnostics,
        nativeStderr,
      },
      null,
      2,
    ),
  )
  console.log(`Native media emulation verified: ${directory}/report.json`)
} finally {
  await source.close()
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
