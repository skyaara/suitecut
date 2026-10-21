// SPDX-License-Identifier: MIT
/* global document, getComputedStyle */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-coverage-oopif',
)
await mkdir(directory, { recursive: true })
const counters = z.object({
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageQueuedSources: z.number().int().nonnegative(),
  nativeCoverageSourceBytes: z.number().nonnegative(),
  nativeCoverageSharedPayloadBytes: z.number().nonnegative(),
  nativeCoverageBackendPhysical: z.number().int().nonnegative(),
  sessions: z.number().int().nonnegative(),
})
const rootJS = 'function rootExecuted(){return 4;}rootExecuted();'
const childJS = 'function childExecuted(){return 7;}childExecuted();'
const rootCSS = '.used { color: rgb(3,4,5); } .unused { color: red; }'
const childCSS = '.used { color: rgb(6,7,8); } .unused { color: blue; }'
const server = createServer((request, response) => {
  const address = server.address()
  assert(address && typeof address !== 'string')
  response.setHeader('Content-Type', 'text/html')
  if (request.url?.startsWith('/child')) {
    response.end(
      `<style>${childCSS}</style><div class="used">Child</div><script>${childJS}</script>`,
    )
  } else if (request.url === '/root') {
    response.end(
      `<style>${rootCSS}</style><div class="used">Root</div><script>${rootJS}</script><iframe src="http://localhost:${address.port}/child"></iframe>`,
    )
  } else response.end('<body>Actual empty root</body>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const childOrigin = `http://localhost:${address.port}`
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
let stderr = '',
  phase = 'launch'
/**
 * @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,preexistingJS?:import('../dist/native.js').NativeJSCoverageEntry[],futureJS?:import('../dist/native.js').NativeJSCoverageEntry[],preexistingCSS?:import('../dist/native.js').NativeCSSCoverageEntry[],futureCSS?:import('../dist/native.js').NativeCSSCoverageEntry[],diagnostics?:z.infer<typeof counters>,timeout?:{elapsed:number,physical:number}}}
 */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 360,
    height: 240,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
    },
  })
  const activeSource = source
  const page = createNativePage(source)
  const diagnostics = async () => counters.parse(await activeSource.runAutomation('diagnostics'))
  const settled = async () => {
    const until = performance.now() + 4000
    while (performance.now() < until) {
      const actual = await diagnostics()
      if (actual.nativeCoveragePhysical === 0 && actual.nativeCoverageRecordings === 0) {
        assert.equal(actual.nativeCoverageQueuedSources, 0)
        assert.equal(actual.nativeCoverageSourceBytes, 0)
        assert.equal(actual.nativeCoverageSharedPayloadBytes, 0)
        assert.equal(actual.nativeCoverageBackendPhysical, 0)
        return actual
      }
    }
    throw Error('Genuine OOPIF coverage domains/source payload failed to retire')
  }
  phase = 'preexisting actual OOPIF JS and CSS'
  await page.goto(origin + '/root')
  assert(
    z
      .object({ sessions: z.number().int().positive() })
      .parse(await activeSource.runAutomation('diagnostics')).sessions > 0,
  )
  const child = page.frames().find((frame) => frame.url() === childOrigin + '/child')
  assert(child)
  await page.coverage.startJSCoverage()
  await page.coverage.startCSSCoverage()
  assert.equal((await diagnostics()).nativeCoverageRecordings, 4)
  await page.evaluate('rootExecuted();rootExecuted()')
  await child.evaluate('childExecuted();childExecuted();childExecuted()')
  for (const frame of [page, child]) {
    await frame.evaluate(() => {
      const target = document.querySelector('div')
      if (!target) throw Error('Actual styled target absent')
      target.className = ''
      void getComputedStyle(target).color
      target.className = 'used'
      return getComputedStyle(target).color
    })
  }
  report.preexistingCSS = await page.coverage.stopCSSCoverage()
  report.preexistingJS = await page.coverage.stopJSCoverage()
  const rootRecord = report.preexistingJS.find((entry) => entry.source === rootJS)
  const childRecord = report.preexistingJS.find((entry) => entry.source === childJS)
  assert(rootRecord && childRecord)
  assert.equal(rootRecord.url, origin + '/root')
  assert.equal(childRecord.url, childOrigin + '/child')
  assert.equal(
    rootRecord.functions.find((entry) => entry.functionName === 'rootExecuted')?.ranges[0]?.count,
    2,
  )
  assert.equal(
    childRecord.functions.find((entry) => entry.functionName === 'childExecuted')?.ranges[0]?.count,
    3,
  )
  report.groups.push(
    'preexisting genuine root/OOPIF independent function counts2/3 and exact sources',
  )
  for (const text of [rootCSS, childCSS]) {
    const actual = report.preexistingCSS.find((entry) => entry.text === text)
    assert(actual)
    assert(actual.ranges.some((range) => text.slice(range.start, range.end).includes('.used')))
    assert(!actual.ranges.some((range) => text.slice(range.start, range.end).includes('.unused')))
  }
  report.groups.push('preexisting actual target CSS sources and used/unused UTF16 ranges')
  await settled()
  phase = 'future OOPIF before author resume'
  await page.goto(origin + '/empty')
  await page.coverage.startJSCoverage()
  await page.coverage.startCSSCoverage()
  await page.evaluate(async (url) => {
    const frame = document.createElement('iframe')
    frame.src = url
    const loaded = new Promise((resolve) => {
      frame.onload = () => resolve(undefined)
    })
    document.body.appendChild(frame)
    await loaded
  }, childOrigin + '/child?future')
  assert((await diagnostics()).sessions > 0)
  report.futureCSS = await page.coverage.stopCSSCoverage()
  report.futureJS = await page.coverage.stopJSCoverage()
  const future = report.futureJS.find((entry) => entry.source === childJS)
  assert(future)
  assert.equal(future.url, childOrigin + '/child?future')
  assert.equal(
    future.functions.find((entry) => entry.functionName === 'childExecuted')?.ranges[0]?.count,
    1,
  )
  assert(report.futureCSS.some((entry) => entry.text === childCSS && entry.ranges.length > 0))
  report.groups.push(
    'future actual target startup ACK barrier captures first author execution count1 and initial CSS',
  )
  await settled()
  phase = 'main document reset with old OOPIF actual detach'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/empty')
  assert.deepEqual(await page.coverage.stopJSCoverage(), [])
  report.groups.push(
    'genuine main navigation resets old OOPIF document and tolerates its actual detach',
  )
  await settled()
  phase = 'genuine OOPIF detach reports lost unsampled engine counters'
  await page.goto(origin + '/root')
  await page.coverage.startJSCoverage()
  const detached = page.waitForEvent('framedetached')
  await page.evaluate(() => document.querySelector('iframe')?.remove())
  await detached
  const until = performance.now() + 4000
  while ((await diagnostics()).sessions > 0 && performance.now() < until) {
    void 0
  }
  assert.equal((await diagnostics()).sessions, 0)
  await assert.rejects(page.coverage.stopJSCoverage(), /detached before.*counters/i)
  await settled()
  report.groups.push('actual target detach fails explicitly instead of inventing lost counts')
  phase = 'held actual OOPIF original deadline and physical cleanup'
  await page.goto(origin + '/root')
  const heldChild = page.frames().find((frame) => frame.url() === childOrigin + '/child')
  assert(heldChild)
  const ready = page.waitForEvent('console')
  const held = heldChild.evaluate(() => {
    console.log('actual-held-OOPIF-coverage')
    const until = performance.now() + 1600
    while (performance.now() < until) {
      void 0
    }
    return 71
  })
  await ready
  page.setDefaultTimeout(120)
  const started = performance.now()
  await assert.rejects(page.coverage.startJSCoverage(), /timeout|timed out|deadline/i)
  const elapsed = performance.now() - started
  assert(elapsed >= 90 && elapsed < 1200, String(elapsed))
  const retained = await diagnostics()
  assert(retained.nativeCoveragePhysical > 0)
  report.timeout = { elapsed, physical: retained.nativeCoveragePhysical }
  assert.equal(await held, 71)
  page.setDefaultTimeout(30000)
  await settled()
  await page.coverage.startJSCoverage()
  await page.coverage.stopJSCoverage()
  report.groups.push(
    'real child renderer hold retains physical ownership past original120ms timeout and permits genuine restart',
  )
  phase = 'unsupported actual navigation-retention options remain rejected'
  await assert.rejects(
    page.coverage.startJSCoverage({ resetOnNavigation: false }),
    /retention.*(?:not implemented|unavailable)|unsupported/i,
  )
  await settled()
  await assert.rejects(
    page.coverage.startCSSCoverage({ resetOnNavigation: false }),
    /retention.*(?:not implemented|unavailable)|unsupported/i,
  )
  report.diagnostics = await settled()
  report.groups.push(
    'unsupported non-resetting JS/CSS retention stays explicit with zero native ownership',
  )
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
  }
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(directory + '/native-stderr.log', stderr)
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
console.log(JSON.stringify(report))
