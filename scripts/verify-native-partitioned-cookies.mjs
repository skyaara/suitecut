// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:https'
import { isAbsolute, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable && isAbsolute(executable), 'Set the absolute native executable')
const directory = resolve(
  process.env.SUITECUT_NATIVE_COOKIE_PROOF ?? '.suitecut/native-partitioned-cookies',
)
await mkdir(directory, { recursive: true })
const keyPath = resolve(directory, 'fixture-key.pem')
const certPath = resolve(directory, 'fixture-cert.pem')
execFileSync(
  'openssl',
  [
    'req',
    '-x509',
    '-newkey',
    'rsa:2048',
    '-nodes',
    '-keyout',
    keyPath,
    '-out',
    certPath,
    '-days',
    '1',
    '-subj',
    '/CN=localhost',
    '-addext',
    'subjectAltName=DNS:localhost,IP:127.0.0.1',
  ],
  { stdio: 'pipe' },
)
const Wire = z.object({ url: z.string(), host: z.string(), cookie: z.string() })
const requests = []
let port = 0
const server = createServer(
  { key: await readFile(keyPath), cert: await readFile(certPath) },
  (request, response) => {
    requests.push({
      url: request.url ?? '',
      host: request.headers.host ?? '',
      cookie: request.headers.cookie ?? '',
    })
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.end(
      request.url?.startsWith('/top')
        ? `<title>Actual partition owner</title><iframe src="https://localhost:${port}/frame"></iframe>`
        : '<title>Actual cookie wire</title>',
    )
  },
)
server.listen(0, '0.0.0.0')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
port = address.port
const checks = []
const failures = []
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
  const a = await browser.newContext({ ignoreHTTPSErrors: true })
  const b = await browser.newContext({ ignoreHTTPSErrors: true })
  const base = {
    name: 'native-chip',
    domain: 'localhost',
    path: '/',
    secure: true,
    sameSite: 'None',
  }
  await a.addCookies([
    {
      ...base,
      value: 'cross-site-real',
      partitionKey: 'https://127.0.0.1',
      partitionKeyHasCrossSiteAncestor: true,
    },
    {
      ...base,
      value: 'same-site-real',
      partitionKey: 'https://localhost',
      partitionKeyHasCrossSiteAncestor: false,
    },
    { ...base, name: 'native-ordinary', value: 'ordinary-real' },
  ])
  assert.deepEqual(await b.cookies(), [])
  const cookies = await a.cookies()
  assert.equal(cookies.length, 3)
  assert.deepEqual(
    cookies
      .filter((cookie) => cookie.partitionKey)
      .map((cookie) => [cookie.value, cookie.partitionKey, cookie.partitionKeyHasCrossSiteAncestor])
      .sort(),
    [
      ['cross-site-real', 'https://127.0.0.1', true],
      ['same-site-real', 'https://localhost', false],
    ].sort(),
  )
  assert(cookies.every((cookie) => cookie.expires === -1))
  checks.push(
    'genuine two-context isolation and exact partition/ancestor/session metadata before any public page',
  )
  const page = await a.newPage()
  page.on('requestfailed', (request) => {
    failures.push({ url: request.url(), failure: request.failure() })
  })
  await page.goto(`https://127.0.0.1:${port}/top`)
  await writeFile(
    resolve(directory, 'actual-document.json'),
    JSON.stringify(
      await page.evaluate(
        '({href:location.href,ready:document.readyState,html:document.documentElement.outerHTML})',
      ),
      null,
      2,
    ) + '\n',
  )
  await writeFile(
    resolve(directory, 'actual-frames.json'),
    JSON.stringify(await page.source.sendDevToolsCommand('Page.getFrameTree'), null, 2) + '\n',
  )
  const cross = z
    .array(Wire)
    .parse(requests)
    .find((request) => request.url === '/frame')
  assert(cross)
  assert.match(cross.cookie, /native-chip=cross-site-real/u)
  assert.doesNotMatch(cross.cookie, /same-site-real/u)
  checks.push('real cross-site HTTPS iframe sends only its matching partitioned cookie')
  const strict = await browser.newContext()
  const strictPage = await strict.newPage()
  await assert.rejects(
    strictPage.goto(`https://127.0.0.1:${port}/strict-context`),
    /CERT|certificate/iu,
  )
  assert(
    !z
      .array(Wire)
      .parse(requests)
      .some((request) => request.url === '/strict-context'),
  )
  await strict.close()
  checks.push(
    'native iframe certificate override leaves a separate strict context rejecting the same certificate',
  )
  await page.goto(`https://localhost:${port}/direct`)
  const direct = z
    .array(Wire)
    .parse(requests)
    .find((request) => request.url === '/direct')
  assert(direct)
  assert.match(direct.cookie, /native-chip=same-site-real/u)
  assert.doesNotMatch(direct.cookie, /cross-site-real/u)
  checks.push('real first-party HTTPS request preserves the false ancestor partition')
  const state = await a.storageState()
  assert.deepEqual(state.cookies, await a.cookies())
  const restored = await browser.newContext({ ignoreHTTPSErrors: true, storageState: state })
  assert.deepEqual(await restored.cookies(), state.cookies)
  checks.push('native storageState captures and restores both real partitions into a fresh context')
  await a.clearCookies({ name: 'native-chip' })
  assert.deepEqual(
    (await a.cookies()).map((cookie) => cookie.name),
    ['native-ordinary'],
  )
  assert.equal((await restored.cookies()).length, 3)
  await a.clearCookies()
  assert.deepEqual(await a.cookies(), [])
  assert.deepEqual(await b.cookies(), [])
  checks.push(
    'filtered deletion removes every matching partition and global clear remains context scoped',
  )
  await assert.rejects(
    a.addCookies([{ ...base, value: 'invalid', secure: false, partitionKey: 'https://localhost' }]),
    /require secure/u,
  )
  await assert.rejects(
    a.addCookies([{ ...base, value: 'invalid', partitionKeyHasCrossSiteAncestor: false }]),
    /requires a partition key/u,
  )
  checks.push('native validation rejects insecure partitions and orphan ancestor metadata')
  await restored.close()
  await b.close()
  await a.close()
  const settled = z
    .object({ nativeCookiePhysical: z.literal(0) })
    .parse(await source.runContext('list'))
  assert.equal(settled.nativeCookiePhysical, 0)
  checks.push(
    'all native cookie physical owners retire after real acknowledgements and context closure',
  )
} finally {
  await writeFile(
    resolve(directory, 'observations.json'),
    JSON.stringify({ checks, requests, failures, stderr }, null, 2) + '\n',
  )
  await source.close()
  await new Promise((resolveClose, rejectClose) =>
    server.close((error) => (error ? rejectClose(error) : resolveClose())),
  )
}
assert.equal(stderr, '')
const report = {
  checks,
  requests,
  stderr,
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
process.stdout.write(JSON.stringify(report) + '\n')
