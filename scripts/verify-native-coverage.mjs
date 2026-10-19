// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

const binary = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-coverage',
)
await mkdir(directory, { recursive: true })
const counterSchema = z.object({
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageQueuedSources: z.number().int().nonnegative(),
  nativeCoverageSourceBytes: z.number().nonnegative(),
  sessions: z.number().int().nonnegative(),
})
const js = 'function foo(){return 42;}foo();'
const css = '\ndiv { color: green; }\nspan { color: red; }\n'
const pages = z.record(z.string(), z.string()).parse({
  '/simple.html': `<style>${css}</style><div>Actual CSS use</div><script>${js}</script>`,
  '/named.html':
    '<script>function actualNamed(){return 1;}actualNamed();\n//# sourceURL=nicename.js</script>',
  '/native-label.html':
    '<script>function authorNativeLabel(){return 3;}authorNativeLabel();\n//# sourceURL=suitecut:initial-document-identity</script>',
  '/eval.html': '<script>eval(\'console.log("foo")\')</script>',
  '/empty.html': '<body>Empty actual document</body>',
  '/frame.html': '<iframe src="/simple.html"></iframe>',
  '/unused.html':
    '<style>span {color:red;}\n/*# sourceURL=unused.css */</style><div>Actual no span</div>',
})
const server = createServer((request, response) => {
  response.writeHead(200, { 'Content-Type': 'text/html' })
  response.end(pages[request.url ?? '/'] ?? pages['/empty.html'])
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/**
 * Actual helper endpoint, retained for unconditional cleanup.
 * @type {import('../dist/native.js').NativeBrowser | undefined}
 */
let source
let stderr = '',
  phase = 'launch'
/**
 * Exact native measurements and cleanup evidence.
 * @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,scripts?:import("../dist/native.js").NativeJSCoverageEntry[],styles?:import("../dist/native.js").NativeCSSCoverageEntry[],diagnostics?:z.infer<typeof counterSchema>,origin:string,js:string,css:string,anonymous?:import("../dist/native.js").NativeJSCoverageEntry[]}}
 */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(binary))
    .digest('hex'),
  origin,
  js,
  css,
  groups: [],
}
try {
  source = await launchNativeBrowser({
    executablePath: binary,
    width: 320,
    height: 240,
    headless: true,
    onDiagnostic: (m) => (stderr += m),
  })
  const page = createNativePage(source)
  phase = 'actual engine JavaScript function ranges and author source'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/simple.html')
  const scripts = await page.coverage.stopJSCoverage()
  report.scripts = scripts
  assert.equal(scripts.length, 1)
  assert(scripts[0])
  assert.equal(scripts[0].source, js)
  assert.equal(scripts[0].url, origin + '/simple.html')
  assert.equal(scripts[0].functions.find((f) => f.functionName === 'foo')?.ranges[0]?.count, 1)
  report.scripts = scripts
  report.groups.push('native JS exact source and actual executed foo count1')
  phase = 'actual CSS used and unused rule offsets'
  await page.coverage.startCSSCoverage()
  await page.goto(origin + '/simple.html')
  const styles = await page.coverage.stopCSSCoverage()
  assert.equal(styles.length, 1)
  assert(styles[0])
  assert(styles[0].ranges[0])
  assert.equal(styles[0].text, css)
  assert.equal(
    styles[0].text.slice(styles[0].ranges[0].start, styles[0].ranges[0].end),
    'div { color: green; }',
  )
  report.styles = styles
  report.groups.push('native CSS exact source and used UTF16 offsets')
  phase = 'explicit author sourceURL'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/named.html')
  const named = await page.coverage.stopJSCoverage()
  assert.equal(named.length, 1)
  assert(named[0])
  assert.equal(named[0].url, 'nicename.js')
  report.groups.push('actual debugger sourceURL')
  phase = 'author sourceURL cannot impersonate actual native bootstrap ownership'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/native-label.html')
  const authorNative = await page.coverage.stopJSCoverage()
  assert.equal(authorNative.length, 1)
  assert(authorNative[0])
  assert.equal(authorNative[0].url, 'suitecut:initial-document-identity')
  assert(authorNative[0].functions.some((f) => f.functionName === 'authorNativeLabel'))
  report.groups.push('actual author native-looking sourceURL retained')
  phase = 'anonymous eval default and opt-in'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/eval.html')
  const unnamedDefault = await page.coverage.stopJSCoverage()
  assert.equal(unnamedDefault.length, 1)
  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  await page.goto(origin + '/eval.html')
  const unnamed = await page.coverage.stopJSCoverage()
  assert(unnamed.some((s) => s.url === '' && s.source === 'console.log("foo")'))
  report.anonymous = unnamed
  report.groups.push('actual anonymous eval opt-in')
  phase = 'navigation default reset and explicit unsupported retention'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/simple.html')
  await page.goto(origin + '/empty.html')
  assert.deepEqual(await page.coverage.stopJSCoverage(), [])
  await assert.rejects(
    page.coverage.startCSSCoverage({ resetOnNavigation: false }),
    /retention across navigation.*not implemented/,
  )
  await assert.rejects(
    page.coverage.startJSCoverage({ resetOnNavigation: false }),
    /retention across navigation.*not implemented/,
  )
  report.groups.push('native reset and explicit unsupported retention')
  phase = 'debugger statement does not hang'
  await page.coverage.startJSCoverage()
  await page.evaluate('(()=>{debugger;return 1})()')
  await page.coverage.stopJSCoverage()
  report.groups.push('actual debugger statement skip and real domain teardown')
  phase = 'unused stylesheet has actual empty ranges'
  await page.coverage.startCSSCoverage()
  await page.goto(origin + '/unused.html')
  const unused = await page.coverage.stopCSSCoverage()
  assert.equal(unused.length, 1)
  assert(unused[0])
  assert.equal(unused[0].url, 'unused.css')
  assert.deepEqual(unused[0].ranges, [])
  report.groups.push('actual unused stylesheet')
  phase = 'same-process child author realm'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/frame.html')
  const iframe = await page.coverage.stopJSCoverage()
  assert(iframe.some((s) => s.source === js && s.url === origin + '/simple.html'))
  report.groups.push('actual same-process child author context')
  const diagnostics = counterSchema.parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(diagnostics))
    if (key.startsWith('nativeCoverage')) assert.equal(value, 0, key)
  report.diagnostics = diagnostics
  report.result = 'pass'
} catch (error) {
  report.result = 'failed'
  report.phase = phase
  report.error = String(error)
  throw error
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(resolve))
    report.nativeStderrBytes = Buffer.byteLength(stderr)
    await writeFile(directory + '/native-stderr.log', stderr)
    await writeFile(directory + '/report.json', JSON.stringify(report, null, 2))
  }
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(JSON.stringify(report))
