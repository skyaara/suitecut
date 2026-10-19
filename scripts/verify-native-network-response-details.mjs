// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import { createHash, X509Certificate } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as createSecureServer } from 'node:https'
import { resolve } from 'node:path'
import process from 'node:process'
import { TLSSocket } from 'node:tls'
import { promisify } from 'node:util'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_RESPONSE_DETAILS_DIRECTORY ??
    '.suitecut/native-network-response-details',
)
await mkdir(directory, { recursive: true })
const keyPath = resolve(directory, 'key.pem'),
  certificatePath = resolve(directory, 'certificate.pem')
await promisify(execFile)('openssl', [
  'req',
  '-x509',
  '-newkey',
  'ec',
  '-pkeyopt',
  'ec_paramgen_curve:P-256',
  '-nodes',
  '-subj',
  '/CN=native-response.test',
  '-days',
  '2',
  '-keyout',
  keyPath,
  '-out',
  certificatePath,
])
const certificateBytes = await readFile(certificatePath),
  certificate = new X509Certificate(certificateBytes)
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([])
const observed = z.record(z.string(), z.unknown()).parse({})
const wire = z.array(z.object({ url: z.string(), tlsProtocol: z.string().nullable() })).parse([])
const http = createServer((request, response) => {
  wire.push({ url: request.url ?? '', tlsProtocol: null })
  response.end('Actual HTTP response')
})
const https = createSecureServer(
  { key: await readFile(keyPath), cert: certificateBytes },
  (request, response) => {
    const socket = request.socket
    const protocol = socket instanceof TLSSocket ? String(socket.getProtocol()) : null
    wire.push({ url: request.url ?? '', tlsProtocol: protocol })
    response.end('Actual TLS response')
  },
)
await Promise.all(
  [http, https].map(
    (server) => new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined))),
  ),
)
const httpAddress = http.address(),
  httpsAddress = https.address()
assert.ok(
  httpAddress &&
    typeof httpAddress !== 'string' &&
    httpsAddress &&
    typeof httpsAddress !== 'string',
)
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 96,
  height: 64,
  onDiagnostic: (message) => diagnostics.push(message),
})
try {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Native contexts unavailable')
  const contexts = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
  })
  const context = await contexts.newContext({
      ignoreHTTPSErrors: true,
      recordHar: {
        path: resolve(directory, 'connection-details.har'),
        content: 'embed',
      },
    }),
    page = await context.newPage()
  const plain = await page.goto(`http://127.0.0.1:${httpAddress.port}/plain`)
  assert.ok(plain)
  observed.httpAddress = await plain.serverAddr()
  assert.deepEqual(observed.httpAddress, {
    ipAddress: '127.0.0.1',
    port: httpAddress.port,
  })
  checks.push('HTTP server address equals the actual listening endpoint')
  assert.equal(await plain.securityDetails(), null)
  checks.push('HTTP provides no fabricated certificate metadata')
  const secure = await page.goto(`https://127.0.0.1:${httpsAddress.port}/secure`)
  assert.ok(secure)
  observed.tlsAddress = await secure.serverAddr()
  assert.deepEqual(observed.tlsAddress, {
    ipAddress: '127.0.0.1',
    port: httpsAddress.port,
  })
  checks.push('TLS server address equals the actual listening endpoint')
  const details = await secure.securityDetails()
  assert.ok(details)
  observed.securityDetails = details
  const actualProtocol = wire.find((row) => row.url === '/secure')?.tlsProtocol
  assert.ok(actualProtocol)
  assert.equal(details.protocol, actualProtocol.replace('TLSv', 'TLS '))
  assert.equal(details.subjectName, 'native-response.test')
  assert.equal(details.issuer, 'native-response.test')
  assert.equal(details.validFrom, Date.parse(certificate.validFrom) / 1000)
  assert.equal(details.validTo, Date.parse(certificate.validTo) / 1000)
  checks.push('Chromium TLS details match the actual socket protocol and generated certificate')
  await context.close()
  const har = z
    .object({
      log: z.object({
        entries: z.array(
          z.object({
            request: z.object({ url: z.string() }),
            serverIPAddress: z.string().optional(),
            _serverPort: z.number().optional(),
            _securityDetails: z.record(z.string(), z.unknown()).optional(),
          }),
        ),
      }),
    })
    .parse(JSON.parse(await readFile(resolve(directory, 'connection-details.har'), 'utf8')))
  const plainEntry = har.log.entries.find(
      (entry) => entry.request.url === `http://127.0.0.1:${httpAddress.port}/plain`,
    ),
    secureEntry = har.log.entries.find(
      (entry) => entry.request.url === `https://127.0.0.1:${httpsAddress.port}/secure`,
    )
  assert.ok(plainEntry && secureEntry)
  assert.equal(plainEntry.serverIPAddress, '127.0.0.1')
  assert.equal(plainEntry._serverPort, httpAddress.port)
  assert.deepEqual(plainEntry._securityDetails, {})
  assert.equal(secureEntry.serverIPAddress, '127.0.0.1')
  assert.equal(secureEntry._serverPort, httpsAddress.port)
  assert.deepEqual(secureEntry._securityDetails, details)
  checks.push('Native HAR serializes the same observed endpoints and TLS details')
  const state = z
    .object({
      nativePrivateBodyInnerReceipts: z.number(),
      nativePrivateBodyQueuedReads: z.number(),
      nativePrivateBodyRecords: z.number(),
      nativePrivateBodySessionReservations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics', {}))
  assert.deepEqual(state, {
    nativePrivateBodyInnerReceipts: 0,
    nativePrivateBodyQueuedReads: 0,
    nativePrivateBodyRecords: 0,
    nativePrivateBodySessionReservations: 1,
  })
  checks.push('Actual context closure releases its native network ownership')
} catch (error) {
  failures.push(String(error))
} finally {
  await source.close()
  for (const server of [http, https]) server.closeAllConnections()
  await Promise.all(
    [http, https].map((server) => new Promise((done) => server.close(() => done(undefined)))),
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        binarySHA256: createHash('sha256')
          .update(await readFile(executable))
          .digest('hex'),
        certificateSHA256: createHash('sha256').update(certificateBytes).digest('hex'),
        checks,
        failures,
        diagnostics,
        observed,
        wire,
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(diagnostics, [])
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
