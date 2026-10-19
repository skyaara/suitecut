// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'
import { gzipSync } from 'node:zlib'

import * as z from 'zod'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE,
  sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
assert.ok(executable && sdk)
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects a locally hashed immutable SDK matching these public source types.
const { launchNativeBrowser, NativeAPIRequestContext, NativeBrowserContexts } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
/** @type {{phases:string[],wire:{path:string,cookie:string,authorized:boolean}[],stderr:string,error?:string,stack?:string|undefined,cleanup?:string,shutdown?:string,exit?:{code:number|null,signal:NodeJS.Signals|null}|undefined,diagnostics?:Record<string,number>}} */
const report = { phases: [], wire: [], stderr: '' }
const authorization = `Basic ${Buffer.from('native:secret').toString('base64')}`
const server = createServer((request, response) => {
  const path = request.url ?? '/',
    cookie = request.headers.cookie ?? '',
    authorized = request.headers.authorization === authorization
  report.wire.push({ path, cookie, authorized })
  if (path === '/set-domain') {
    response.setHeader('Set-Cookie', 'scoped=wire; Domain=b.one.com; Path=/')
  }
  if (path === '/set') {
    response.setHeader('Set-Cookie', [
      'future=wire; Expires=Fri, 31 Dec 2100 08:00:00 GMT; Path=/',
      'wire_path=real; Path=/input; HttpOnly; SameSite=Strict',
    ])
  }
  if (path === '/auth' && !authorized) {
    response.writeHead(401, {
      'WWW-Authenticate': 'Basic realm="native-api-cookie-jar"',
      'Set-Cookie': 'challenge=renewed; Path=/',
      'Content-Encoding': 'gzip',
      'Content-Length': '0',
    })
    response.end()
    return
  }
  if (path === '/headers-held') {
    response.writeHead(200, {
      'Set-Cookie': 'accepted_before_timeout=real; Path=/',
      'Content-Length': '1024',
    })
    response.flushHeaders()
    return
  }
  response.setHeader('Content-Encoding', 'gzip')
  response.end(gzipSync(Buffer.from(cookie)))
})
await new Promise((resolveListen) => server.listen(0, () => resolveListen(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const base = `http://localhost:${address.port}`,
  output = resolve(
    process.env.SUITECUT_NATIVE_API_COOKIE_JAR_REPORT ?? '/tmp/suitecut-api-cookie-jar-report.json',
  )
const save = async () => {
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
}
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 64,
  height: 64,
  onDiagnostic: (text) => {
    report.stderr += text
  },
}).catch(async (error) => {
  const failure = error instanceof Error ? error : new Error('Native browser startup failed')
  report.error = failure.message
  report.stack = failure.stack
  await new Promise((resolveClose) => server.close(() => resolveClose(undefined)))
  await save()
  throw failure
})
const contexts = new NativeBrowserContexts(source)
/** @type {import('../src/native-context.js').NativeBrowserContext | undefined} */
let browserContext
/** @type {import('../src/native-http.js').NativeAPIRequestContext[]} */
const requests = []
/** @type {Error | undefined} */
let failure
try {
  const request = await NativeAPIRequestContext.create(source)
  requests.push(request)
  await request.addCookies([
    { name: 'imported', value: 'exact-host', domain: 'one.com', path: '/' },
  ])
  const subdomain = await request.get(`http://sub.one.com:${address.port}/imported`, {
    lookup: () => [{ address: '127.0.0.1', family: 4 }],
  })
  assert.equal(await subdomain.text(), '')
  await subdomain.dispose()
  assert.equal(
    (await request.cookies('http://sub.one.com/')).find((cookie) => cookie.name === 'imported')
      ?.value,
    'exact-host',
  )
  await request.addCookies([
    { name: 'dotted_import', value: 'subdomain', domain: '.one.com', path: '/' },
  ])
  const dotted = await request.get(`http://sub.one.com:${address.port}/dotted-import`, {
    lookup: () => [{ address: '127.0.0.1', family: 4 }],
  })
  assert.equal(await dotted.text(), 'dotted_import=subdomain')
  await dotted.dispose()
  const scoped = await request.get(`http://a.b.one.com:${address.port}/set-domain`, {
    lookup: () => [{ address: '127.0.0.1', family: 4 }],
  })
  await scoped.dispose()
  assert.equal(
    (await request.cookies()).find((cookie) => cookie.name === 'scoped')?.domain,
    '.b.one.com',
  )
  report.phases.push(
    'Imported host-only wire excludes subdomains; public URL selection and dotted-domain wire remain supported',
  )

  const seeded = await request.get(base + '/set')
  await seeded.dispose()
  const snapshot = await request.storageState()
  assert.equal(snapshot.cookies.find((cookie) => cookie.name === 'future')?.expires, 4133923200)
  const paths = await request.get(base + '/input/echo')
  assert.equal(await paths.text(), 'future=wire; wire_path=real')
  await paths.dispose()
  browserContext = await contexts.newContext()
  await browserContext.addCookies([
    { name: 'browser', value: 'scoped', domain: 'localhost', path: '/' },
  ])
  const coupled = await browserContext.request.get(base + '/coupled')
  assert.equal(await coupled.text(), 'browser=scoped')
  await coupled.dispose()
  assert.equal(
    (await request.cookies()).some((cookie) => cookie.name === 'browser'),
    false,
  )
  report.phases.push(
    'Real response expiry remains year2100; associated request retains separate actual browser jar',
  )

  const restored = await NativeAPIRequestContext.create(source, { storageState: snapshot })
  requests.push(restored)
  assert.deepEqual(await restored.storageState(), snapshot)
  const restoreWire = await restored.get(base + '/restored')
  assert.equal(await restoreWire.text(), 'future=wire')
  await restoreWire.dispose()
  const selected = /^future$/gy
  selected.lastIndex = 5
  await restored.clearCookies({ name: selected, domain: /LOCALHOST/i, path: /^\/$/ })
  assert.equal(selected.lastIndex, 5)
  assert.equal(
    (await restored.cookies()).some((cookie) => cookie.name === 'future'),
    false,
  )
  assert.equal(
    (await request.cookies()).some((cookie) => cookie.name === 'future'),
    true,
  )
  report.phases.push(
    'Native storage restore and genuine private-V8 regex select exact independent store records',
  )

  const authenticated = await NativeAPIRequestContext.create(source, {
    httpCredentials: { username: 'native', password: 'secret' },
  })
  requests.push(authenticated)
  const auth = await authenticated.get(base + '/auth')
  assert.equal(await auth.text(), 'challenge=renewed')
  await auth.dispose()
  assert.deepEqual(
    report.wire.filter((entry) => entry.path === '/auth'),
    [
      { path: '/auth', cookie: '', authorized: false },
      { path: '/auth', cookie: 'challenge=renewed', authorized: true },
    ],
  )
  report.phases.push(
    'Actual401 cookie receipt refreshes Cookie before genuine authorized retry and final gzip decode',
  )

  await assert.rejects(request.get(base + '/headers-held', { timeout: 100 }), {
    name: 'TimeoutError',
  })
  assert.equal(
    (await request.cookies()).find((cookie) => cookie.name === 'accepted_before_timeout')?.value,
    'real',
  )
  report.phases.push(
    'Accepted actual header cookie survives subsequent original native body timeout',
  )

  for (let index = 0; index < 20; index++) {
    const cycle = await NativeAPIRequestContext.create(source, {
      storageState: {
        cookies: [
          {
            name: 'cycle',
            value: String(index),
            domain: 'localhost',
            path: '/',
            expires: -1,
            httpOnly: false,
            secure: false,
            sameSite: 'Lax',
          },
        ],
        origins: [],
      },
    })
    requests.push(cycle)
    const echo = await cycle.get(base + '/cycle')
    assert.equal(await echo.text(), `cycle=${index}`)
    await echo.dispose()
    await cycle.dispose()
  }
  for (const request of requests) await request.dispose()
  await browserContext.request.dispose()
  await browserContext.close()
  browserContext = undefined
  const diagnostics = z
    .object({
      httpAPICookieAllocatedBytes: z.number(),
      httpAPICookieEntries: z.number(),
      httpTransfers: z.number(),
      httpContexts: z.number(),
      httpActive: z.number(),
      httpResponses: z.number(),
      httpScopeCommands: z.number(),
      httpProfilePhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  report.diagnostics = diagnostics
  for (const [name, value] of Object.entries(diagnostics)) assert.equal(value, 0, name)
  report.phases.push(
    'Twenty real profile/store/wire/dispose lifecycles release native allocations and ownership',
  )
} catch (error) {
  failure = error instanceof Error ? error : new Error('Native API cookie jar verification failed')
  report.error = failure.message
  report.stack = failure.stack
} finally {
  try {
    for (const request of requests) await request.dispose()
    await browserContext?.request.dispose()
    await browserContext?.close()
  } catch (error) {
    report.cleanup = error instanceof Error ? error.message : 'Native API cookie cleanup failed'
    failure ??= error instanceof Error ? error : new Error('Native API cookie cleanup failed')
  }
  try {
    await source.close()
  } catch (error) {
    report.shutdown = error instanceof Error ? error.message : 'Native helper shutdown failed'
    failure ??= error instanceof Error ? error : new Error('Native helper shutdown failed')
  }
  report.exit = source.processExit?.()
  server.closeAllConnections()
  await new Promise((resolveClose) => server.close(() => resolveClose(undefined)))
  await save()
}
if (failure) throw failure
assert.equal(report.stderr, '')
assert.ok(report.exit)
assert.equal(report.exit.code, 0)
assert.equal(report.exit.signal, null)
assert.equal(report.phases.length, 6)
console.log(JSON.stringify(report, null, 2))
