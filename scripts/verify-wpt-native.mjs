// SPDX-License-Identifier: MIT
// Independent WPT oracle: original assertions and generic harness, native driver bridge.
/* global window, document */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { chmod, cp, mkdir, readFile, readdir, symlink, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'
import { setTimeout } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

import * as z from 'zod'

const revision = 'd1c5cb6aacd9238e1cd6b999a8507a198c7d32fb'
const root = resolve('.suitecut/wpt-native')
const runDirectory = `${root}/runs/${new Date().toISOString().replaceAll(':', '-')}-${process.pid}`
const sourceExecutable = z.string().refine(isAbsolute).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const sourceSDK = z
  .string()
  .refine(isAbsolute)
  .parse(process.env.SUITECUT_NATIVE_SDK_DIRECTORY ?? resolve('dist'))
const run = promisify(execFile)
/**
 * Hash exact input bytes without retaining a large native library.
 * @param {string} path
 */
async function hash(path) {
  const digest = createHash('sha256')
  for await (const chunk of createReadStream(path))
    digest.update(z.instanceof(Uint8Array).parse(chunk))
  return digest.digest('hex')
}
/**
 * Record immutable files recursively.
 * @param {string} path
 * @returns {Promise<Array<{path:string,sha256:string}>>}
 */
async function inventory(path) {
  const entries = []
  for (const item of await readdir(path, { withFileTypes: true })) {
    const target = resolve(path, item.name)
    if (item.isDirectory()) entries.push(...(await inventory(target)))
    else entries.push({ path: target, sha256: await hash(target) })
  }
  return entries
}
await mkdir(`${runDirectory}/runtime`, { recursive: true })
await cp(sourceExecutable, `${runDirectory}/runtime/suitecut-browser`)
await chmod(`${runDirectory}/runtime/suitecut-browser`, 0o500)
await cp(sourceSDK, `${runDirectory}/sdk`, { recursive: true })
const cefSDK = resolve('.suitecut/cef-sdk')
const cefVersion = z
  .object({ cefVersion: z.string() })
  .parse(JSON.parse(await readFile('native/browser/cef-build.json', 'utf8'))).cefVersion
assert.equal(
  /^#define CEF_VERSION "([^"]+)"/mu.exec(
    await readFile(`${cefSDK}/include/cef_version.h`, 'utf8'),
  )?.[1],
  cefVersion,
)
const runtimeResources = []
for (const directory of ['Release', 'Resources']) {
  runtimeResources.push(...(await inventory(`${cefSDK}/${directory}`)))
  for (const name of await readdir(`${cefSDK}/${directory}`))
    await symlink(`${cefSDK}/${directory}/${name}`, `${runDirectory}/runtime/${name}`)
}
const oracleScope = z
  .enum(['focus', 'keyboard'])
  .parse(process.env.SUITECUT_NATIVE_WPT_SCOPE ?? 'focus')
const cases =
  oracleScope === 'keyboard'
    ? [
        'html/interaction/focus/sequential-focus-navigation-and-the-tabindex-attribute/focus-tabindex-order.html',
        'html/interaction/focus/focus-keyboard-js.html',
        'uievents/keyboard/keypress-not-fired-for-modifier-shortcuts.html',
      ]
    : [
        'legend-focusable.html',
        'legend.html',
        'focusVisible.html',
        'focus-fixup-rule-one-no-dialogs.html',
        'preventScroll.html',
        'preventScroll-textarea.html',
        'preventScroll-nested-scroll-elements.html',
        'textarea-scroll-selection.html',
      ].map((name) => `html/interaction/focus/processing-model/${name}`)
const originals = [
  'LICENSE.md',
  ...cases,
  'html/interaction/focus/processing-model/support/preventScroll-helper.html',
  ...['testharness.js', 'testharnessreport.js', 'testdriver.js', 'testdriver-actions.js'].map(
    (name) => `resources/${name}`,
  ),
]
const sourceInputManifest = process.env.SUITECUT_NATIVE_WPT_INPUT_MANIFEST
const pinnedInputs = sourceInputManifest
  ? z
      .object({
        revision: z.literal(revision),
        inputs: z.array(
          z.object({
            upstream: z.string().optional(),
            path: z.string().refine(isAbsolute),
            sha256: z.string().regex(/^[a-f0-9]{64}$/),
          }),
        ),
      })
      .parse(JSON.parse(await readFile(sourceInputManifest, 'utf8'))).inputs
  : undefined
const executableDependencies = z
  .array(z.string().refine(isAbsolute))
  .parse(JSON.parse(process.env.SUITECUT_NATIVE_EXECUTABLE_DEPENDENCIES ?? '[]'))
const sources = []
for (const original of originals) {
  const target = `${runDirectory}/original/${original}`
  await mkdir(resolve(target, '..'), { recursive: true })
  if (pinnedInputs) {
    const pinned = pinnedInputs.find((item) => item.upstream === original)
    assert(pinned, `Pinned original input absent from immutable manifest: ${original}`)
    assert.equal(
      await hash(pinned.path),
      pinned.sha256,
      `Pinned original input changed: ${original}`,
    )
    await cp(pinned.path, target)
    assert.equal(await hash(target), pinned.sha256)
  } else {
    await run('curl', [
      '--fail',
      '--silent',
      '--show-error',
      '--max-time',
      '30',
      `https://raw.githubusercontent.com/web-platform-tests/wpt/${revision}/${original}`,
      '-o',
      target,
    ])
  }
  sources.push({ upstream: original, path: target, sha256: await hash(target) })
}
const vendorPath = `${runDirectory}/testdriver-vendor.js`
await writeFile(
  vendorPath,
  `
window.test_driver_internal.in_automation = true;
window.test_driver_internal.click = (element, coords) => window.suitecutWptClick({ element, coords });
window.test_driver_internal.send_keys = (element, keys) => window.suitecutWptSendKeys({ element, keys });
`,
)
/**
 * The immutable SDK has the same public types as the checked native SDK.
 * @type {typeof import('../src/native.js')}
 */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Dynamic immutable import is checked against the public SDK type.
const native = await import(pathToFileURL(`${runDirectory}/sdk/native.js`).href)
const inputs = [
  ...sources,
  ...(sourceInputManifest
    ? [{ path: sourceInputManifest, sha256: await hash(sourceInputManifest) }]
    : []),
  ...(await Promise.all(
    executableDependencies.map(async (path) => ({ path, sha256: await hash(path) })),
  )),
  ...runtimeResources,
  ...(await inventory(`${runDirectory}/sdk`)),
  { path: vendorPath, sha256: await hash(vendorPath) },
  {
    path: `${runDirectory}/runtime/suitecut-browser`,
    sha256: await hash(`${runDirectory}/runtime/suitecut-browser`),
  },
  {
    path: resolve('scripts/verify-wpt-native.mjs'),
    sha256: await hash(resolve('scripts/verify-wpt-native.mjs')),
  },
]
await writeFile(
  `${runDirectory}/manifest.json`,
  JSON.stringify(
    {
      revision,
      oracleScope,
      cefVersion,
      sourceExecutable,
      executableDependencies,
      sourceInputManifest,
      sourceSDK,
      inputs,
      adaptations: [
        'The complete original selected WPT HTML files and all assertions are served unchanged, alongside exact pinned generic testharness and testdriver interface resources.',
        'An authored vendor adapter forwards test_driver.click and send_keys through genuine native handle bindings. Native C++ alone interprets reserved keys and owns modifier state, focus, original deadlines and physical keyboard delivery.',
        'A native initialization script subscribes to the original add_completion_callback at external-script load and reports genuine WPT status, assertions and failures; no assertions are replaced.',
        'There is no WebDriver, Playwright, Puppeteer or other external browser driver package or implementation. Runtime and SDK copies, original fixtures and authored adapter are hashed before and after.',
      ],
    },
    null,
    2,
  ) + '\n',
)
console.log(`Independent WPT native snapshot: ${runDirectory}`)
let diagnostics = ''
const source = await native.launchNativeBrowser({
  executablePath: `${runDirectory}/runtime/suitecut-browser`,
  headless: true,
  width: 1280,
  height: 720,
  onDiagnostic(message) {
    if (diagnostics.length < 1048576) diagnostics += message.slice(0, 1048576 - diagnostics.length)
  },
})
assert.equal(source.cefVersion, cefVersion)
const browser = new native.NativeBrowserContexts(source)
const server = createServer((request, response) => {
  void (async () => {
    const path = new URL(request.url ?? '/', 'http://localhost').pathname
    const file =
      path === '/resources/testdriver-vendor.js'
        ? vendorPath
        : resolve(`${runDirectory}/original`, `.${path}`)
    if (file !== vendorPath && !file.startsWith(`${runDirectory}/original/`)) {
      response.writeHead(403).end()
      return
    }
    try {
      const data = await readFile(file)
      response.setHeader(
        'Content-Type',
        path.endsWith('.js') ? 'application/javascript; charset=utf-8' : 'text/html; charset=utf-8',
      )
      response.end(data)
    } catch {
      response.writeHead(404).end('Missing pinned WPT asset')
    }
  })().catch(() => {
    if (!response.writableEnded) response.writeHead(500).end()
  })
})
await new Promise((accept, reject) => {
  server.once('error', reject)
  server.listen(0, '127.0.0.1', accept)
})
const address = server.address()
assert(address && typeof address !== 'string')
const resultSchema = z.object({
  harness: z.object({ status: z.number(), message: z.string().nullable() }),
  tests: z.array(
    z.object({
      name: z.string(),
      status: z.number(),
      message: z.string().nullable(),
      stack: z.string().nullable(),
    }),
  ),
})
const results = []
try {
  for (const testPath of cases) {
    const context = await browser.newContext()
    const page = await context.newPage()
    page.on('pageerror', (error) => {
      const message = `${testPath}: ${error.stack ?? error.message}\n`
      diagnostics += message.slice(0, Math.max(0, 1048576 - diagnostics.length))
    })
    /**
     * Settles the genuine WPT completion report.
     * @type {(report: z.infer<typeof resultSchema>) => void}
     */
    let resolveReport = () => {
      throw new Error('WPT completion promise was not initialized')
    }
    /**
     * Genuine completion report from the original WPT harness.
     * @type {Promise<z.infer<typeof resultSchema>>}
     */
    const completed = new Promise((accept) => {
      resolveReport = accept
    })
    await page.exposeFunction('suitecutWptComplete', (report) => {
      resolveReport(resultSchema.parse(report))
      return undefined
    })
    await page.exposeBinding(
      'suitecutWptClick',
      async (_info, argument) => {
        const input = /** @type {unknown} */ (argument)
        assert(input instanceof native.NativeJSHandle)
        /**
         * Actual owned element property handle.
         * @type {import('../src/native.js').NativeJSHandle | undefined}
         */
        let elementProperty
        /**
         * Actual owned viewport coordinate handle.
         * @type {import('../src/native.js').NativeJSHandle | undefined}
         */
        let coordinates
        try {
          elementProperty = await input.getProperty('element')
          coordinates = await input.getProperty('coords')
          const point = z
            .object({ x: z.number(), y: z.number() })
            .parse(await coordinates.jsonValue())
          const element = elementProperty.asElement()
          assert(element, 'WPT provided an actual native DOM element')
          const box = await element.boundingBox()
          assert(box, 'WPT provided a visible native DOM target')
          await element.click({ position: { x: point.x - box.x, y: point.y - box.y } })
          return undefined
        } finally {
          await Promise.allSettled([
            input.dispose(),
            elementProperty?.dispose(),
            coordinates?.dispose(),
          ])
        }
      },
      { handle: true },
    )
    await page.exposeBinding(
      'suitecutWptSendKeys',
      async (_info, argument) => {
        const input = /** @type {unknown} */ (argument)
        assert(input instanceof native.NativeJSHandle)
        /** @type {import('../src/native.js').NativeJSHandle | undefined} */
        let elementProperty
        /** @type {import('../src/native.js').NativeJSHandle | undefined} */
        let keysProperty
        try {
          elementProperty = await input.getProperty('element')
          keysProperty = await input.getProperty('keys')
          const element = elementProperty.asElement()
          assert(element, 'WPT provided a genuine native DOM element')
          const keys = z.string().parse(await keysProperty.jsonValue())
          await element.sendKeys(keys)
          return undefined
        } finally {
          await Promise.allSettled([
            input.dispose(),
            elementProperty?.dispose(),
            keysProperty?.dispose(),
          ])
        }
      },
      { handle: true },
    )
    await page.addInitScript(() => {
      let registered = false
      /** @type {Window & {add_completion_callback?: (callback: (tests: Array<{name:string,status:number,message?:string,stack?:string}>, status:{status:number,message?:string}) => void) => void, suitecutWptComplete: (report: unknown) => Promise<void>}} */
      const wptWindow = window
      document.addEventListener(
        'load',
        () => {
          if (registered || typeof wptWindow.add_completion_callback !== 'function') return
          registered = true
          wptWindow.add_completion_callback((tests, status) => {
            void wptWindow.suitecutWptComplete({
              harness: { status: status.status, message: status.message ?? null },
              tests: tests.map((test) => ({
                name: test.name,
                status: test.status,
                message: test.message ?? null,
                stack: test.stack ?? null,
              })),
            })
          })
        },
        true,
      )
    })
    try {
      await page.goto(`http://localhost:${address.port}/${testPath}`)
      const report = await Promise.race([
        completed,
        setTimeout(30000, undefined, { ref: false }).then(() => {
          throw new Error('Original WPT completion callback did not finish within 30 seconds')
        }),
      ])
      results.push({ testPath, ...resultSchema.parse(report) })
    } catch (error) {
      results.push({
        testPath,
        harness: { status: -1, message: error instanceof Error ? error.message : String(error) },
        tests: [],
      })
    } finally {
      await context.close()
    }
  }
} finally {
  await source.close()
  await new Promise((accept) => server.close(accept))
  await writeFile(`${runDirectory}/native-stderr.log`, diagnostics)
}
const changedInputs = []
for (const input of inputs)
  if ((await hash(input.path)) !== input.sha256) changedInputs.push(input.path)
assert.equal(changedInputs.length, 0, 'An immutable WPT input or native runtime changed')
const tests = results.flatMap((result) => result.tests)
const passed = tests.filter((test) => test.status === 0).length
const failed = tests.length - passed
const harnessFailures = results.filter((result) => result.harness.status !== 0).length
const summary = {
  revision,
  oracleScope,
  cefVersion,
  runDirectory,
  passed,
  failed,
  harnessFailures,
  inputsUnchanged: true,
  changedInputs,
  results,
}
await writeFile(`${runDirectory}/summary.json`, JSON.stringify(summary, null, 2) + '\n')
await writeFile(`${root}/latest-run.json`, JSON.stringify({ runDirectory }) + '\n')
console.log(JSON.stringify(summary))
process.exitCode = failed || harnessFailures ? 1 : 0
