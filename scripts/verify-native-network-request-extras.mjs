// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts, NativeRequest } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_NETWORK_REQUEST_EXTRAS_DIRECTORY ??
    '.suitecut/native-network-request-extras',
)
await mkdir(directory, { recursive: true })
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const cleanupFailures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const wire = z
  .array(
    z.object({
      url: z.string(),
      headers: z.record(z.string(), z.union([z.string(), z.array(z.string())]).optional()),
    }),
  )
  .parse([])
const observations = z
  .array(
    z.object({
      phase: z.string(),
      event: z.string(),
      sequence: z.number(),
      id: z.string(),
      url: z.string(),
      headersReady: z.boolean().optional(),
    }),
  )
  .parse([])
const server = createServer((request, response) => {
  const url = request.url ?? ''
  wire.push({ url, headers: request.headers })
  response.setHeader('content-type', 'text/html')
  const match = /^\/(direct|intercepted)\/(one|two|final)$/u.exec(url)
  if (!match) {
    response.writeHead(404)
    response.end('Actual fixture resource unavailable')
    return
  }
  const phase = match[1],
    hop = match[2]
  if (hop === 'one' || hop === 'two') {
    response.writeHead(302, {
      location: `/${phase}/${hop === 'one' ? 'two' : 'final'}`,
      'set-cookie': `actualHop=${hop}; Path=/${phase}/`,
    })
    response.end()
  } else response.end('<body>actual native redirect headers</body>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let source
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (line) => diagnostics.push(line),
  })
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Actual native context transport is unavailable')
  const context = new NativeBrowserContexts({
    ...source,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  }).defaultContext()
  for (const phase of ['direct', 'intercepted']) {
    const page = await context.newPage()
    page.setDefaultTimeout(5000)
    const requests = z.array(z.instanceof(NativeRequest)).parse([])
    page.on('request', (request) => requests.push(request))
    const detach = page.source.onNetworkMessage?.((event) => {
      if (!('request' in event)) {
        failures.push(`Actual native route failure: ${JSON.stringify(event)}`)
        return
      }
      const { id, url, headersReady } = z
        .object({ id: z.string(), url: z.string(), headersReady: z.boolean().optional() })
        .parse(event.request)
      observations.push({
        phase,
        event: event.event,
        sequence: event.sequence,
        id,
        url,
        ...(headersReady === undefined ? {} : { headersReady }),
      })
    })
    let handled = 0
    if (phase === 'intercepted') {
      await page.route(`**/${phase}/one`, async (route) => {
        ++handled
        const native = await route.request().allHeaders()
        assert.match(native['user-agent'] ?? '', /Chrome/u)
        await route.continue()
      })
    }
    const response = await page.goto(`${origin}/${phase}/one`)
    assert.ok(response)
    assert.equal(response.status(), 200)
    assert.equal(await response.text(), '<body>actual native redirect headers</body>')
    if (phase === 'intercepted') assert.equal(handled, 1)
    checks.push(
      `${phase}: actual three-hop navigation completes; intercepted headers do not await their own continuation`,
    )
    const final = response.request(),
      second = final.redirectedFrom(),
      first = second?.redirectedFrom()
    assert.ok(first && second)
    assert.equal(first.redirectedFrom(), null)
    assert.equal(first.redirectedTo(), second)
    assert.equal(second.redirectedTo(), final)
    const chain = [first, second, final]
    assert.equal(new Set(chain.map((request) => request.info.id)).size, 3)
    for (const [index, request] of chain.entries()) {
      const path = `/${phase}/${['one', 'two', 'final'][index]}`
      assert.equal(request.url(), origin + path)
      assert.ok(request.hasFinished())
      assert.ok(requests.includes(request))
      const actual = wire.filter((record) => record.url === path)
      assert.equal(actual.length, 1)
      assert.deepEqual(await request.allHeaders(), actual[0]?.headers)
    }
    checks.push(
      `${phase}: every canonical logical hop's completed raw headers equal its exact server-observed request`,
    )
    const cookies = await Promise.all(
      chain.map(async (request) => (await request.allHeaders()).cookie ?? null),
    )
    assert.deepEqual(cookies, [null, 'actualHop=one', 'actualHop=two'])
    checks.push(
      `${phase}: real redirect Set-Cookie changes distinguish successive raw-header receipts`,
    )
    detach?.()
    await page.close()
  }
  await context.close()
  await source.close()
  assert.ok(source.processClosed)
  await source.processClosed
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  if (source) {
    await source.close().catch((error) => cleanupFailures.push(String(error)))
    await source.processClosed?.catch((error) => cleanupFailures.push(String(error)))
  }
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  const report = {
    binarySHA256: createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    scope:
      'Genuine server wire and native canonical request IDs; no request filtering or protocol event reordering',
    lateOrderObserved: observations.some(
      (event) =>
        event.event === 'requestupdated' &&
        observations.some(
          (prior) =>
            prior.phase === event.phase &&
            prior.id === event.id &&
            prior.event === 'requestfinished' &&
            prior.sequence < event.sequence &&
            prior.headersReady === false &&
            event.headersReady === true,
        ),
    ),
    checks,
    failures,
    cleanupFailures,
    wire,
    observations,
    processExit: source?.processExit?.(),
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  console.log(JSON.stringify(report, null, 2))
  if (checks.length !== 6 || failures.length || cleanupFailures.length) process.exitCode = 1
}
