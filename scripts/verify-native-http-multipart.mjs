// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer, File } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE,
  sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
assert.ok(executable && sdk, 'Provide the immutable native multipart executable and SDK')
/** @type {typeof import('../src/native.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Caller selects a locally hashed immutable SDK matching the public source types.
const { launchNativeBrowser, NativeAPIRequestContext } = await import(
  pathToFileURL(join(sdk, 'native.js')).href
)
const output = resolve(
  process.env.SUITECUT_NATIVE_MULTIPART_REPORT ??
    '/tmp/suitecut-http-multipart-stage/wire-report.json',
)
/** @type {{phases:string[],wire:{path:string,bytes:number,sha256:string,contentType:string}[],stderr:string,error?:string,stack?:string|undefined,cleanup?:string,shutdown?:string,final?:import('../src/untrusted.js').UntrustedInput,exit?:{code:number|null,signal:NodeJS.Signals|null}}} */
const report = { phases: [], wire: [], stderr: '' }
/** @type {{body:Buffer,contentType:string,contentLength:string}[]} */
const received = []
const server = createServer((request, response) => {
  void (async () => {
    /** @type {Buffer[]} */
    const chunks = []
    for await (const chunk of request) {
      assert.ok(Buffer.isBuffer(chunk))
      chunks.push(chunk)
    }
    const body = Buffer.concat(chunks),
      contentType = request.headers['content-type'] ?? ''
    assert.ok(body.length <= 8 * 1024 * 1024)
    received.push({
      body,
      contentType,
      contentLength: request.headers['content-length'] ?? '',
    })
    report.wire.push({
      path: request.url ?? '',
      bytes: body.length,
      sha256: createHash('sha256').update(body).digest('hex'),
      contentType,
    })
    response.end('actual native multipart')
  })().catch((error) => response.destroy(error instanceof Error ? error : new Error(String(error))))
})
await new Promise((resolve) => server.listen(0, resolve))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = 'http://localhost:' + address.port
/** @type {import('../src/native-browser.js').NativeBrowser|undefined} */
let source
/** @type {import('../src/native-http.js').NativeAPIRequestContext|undefined} */
let api
/**
 * Builds an independent exact wire expectation from the real wire boundary.
 * @param {{name:string,body:Buffer,filename?:string,mime?:string}[]} parts
 */
function checkWire(parts) {
  const wire = received.at(-1)
  assert.ok(wire)
  const match = /^multipart\/form-data; boundary=([a-zA-Z0-9-]{16,70})$/u.exec(wire.contentType)
  assert.ok(match)
  const boundary = match[1]
  assert.ok(boundary)
  /**
   * Escapes the quoted multipart disposition parameter.
   * @param {string} value
   */
  const escape = (value) =>
    value.replace(/\r/gu, '%0D').replace(/\n/gu, '%0A').replace(/"/gu, '%22')
  /** @type {Buffer[]} */
  const expected = []
  for (const part of parts) {
    expected.push(
      Buffer.from(
        '--' +
          boundary +
          '\r\ncontent-disposition: form-data; name="' +
          escape(part.name) +
          '"' +
          (part.filename === undefined ? '' : '; filename="' + escape(part.filename) + '"') +
          '\r\n' +
          (part.filename === undefined
            ? ''
            : 'content-type: ' + (part.mime ?? 'application/octet-stream') + '\r\n') +
          '\r\n',
      ),
      part.body,
      Buffer.from('\r\n'),
    )
  }
  expected.push(Buffer.from('--' + boundary + '--\r\n'))
  assert.deepEqual(wire.body, Buffer.concat(expected))
  assert.equal(wire.contentLength, String(wire.body.length))
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 240,
    height: 180,
    onDiagnostic: (value) => {
      report.stderr += value
    },
  })
  api = await NativeAPIRequestContext.create(source)
  const form = new FormData(),
    binary = Buffer.from([0, 255, 13, 10, 128, 65])
  form.append('same', 'first')
  form.append('same', 'second')
  form.append('quoted"\r\n', new File([binary], 'q"\r\n.bin', { type: 'application/octet-stream' }))
  form.append('empty', '')
  let response = await api.post(origin + '/ordered', {
    multipart: form,
    headers: { 'Content-Type': 'application/invalid' },
  })
  assert.equal(response.status(), 200)
  await response.dispose()
  checkWire([
    { name: 'same', body: Buffer.from('first') },
    { name: 'same', body: Buffer.from('second') },
    {
      name: 'quoted"\r\n',
      body: binary,
      filename: 'q"\r\n.bin',
      mime: 'application/octet-stream',
    },
    { name: 'empty', body: Buffer.alloc(0) },
  ])
  report.phases.push(
    'actual native multipart wire preserves repeated FormData order, binary File bytes, escaped names and filenames, lowercase headers and exact Content-Length',
  )
  const large = Buffer.alloc(160 * 1024)
  for (let index = 0; index < large.length; ++index) large[index] = index % 256
  response = await api.post(origin + '/chunks', {
    multipart: { file: { name: 'actual.bin', buffer: large } },
  })
  assert.equal(await response.text(), 'actual native multipart')
  await response.dispose()
  checkWire([{ name: 'file', body: large, filename: 'actual.bin' }])
  report.phases.push(
    'actual large binary multipart source traverses native upload chunks and is framed in C++ with exact bytes',
  )
  const before = received.length
  await assert.rejects(
    api.post(origin + '/over-wire-limit', {
      multipart: {
        file: { name: 'large.bin', buffer: Buffer.alloc(8 * 1024 * 1024) },
      },
    }),
    /Native multipart wire body exceeds 8 MiB/u,
  )
  assert.equal(received.length, before)
  report.phases.push(
    'native final encoded-wire limit rejects a raw 8MiB source plus framing before any origin request',
  )
  await assert.rejects(
    api.post(origin + '/mime-injection', {
      multipart: {
        file: {
          name: 'x',
          buffer: binary,
          mimeType: 'text/plain\r\nInjected: yes',
        },
      },
    }),
    /Invalid native multipart field metadata/u,
  )
  assert.equal(received.length, before)
  report.phases.push(
    'native MIME header-injection validation rejects the actual request before wire dispatch',
  )
  response = await api.post(origin + '/empty', { multipart: new FormData() })
  assert.equal(response.status(), 200)
  await response.dispose()
  checkWire([])
  report.phases.push('empty multipart uses a real native closing delimiter and exact byte length')
  await api.dispose()
  report.final = await source.runAutomation('diagnostics', {})
  const schema = z.object(
    Object.fromEntries(
      [
        'httpContexts',
        'httpActive',
        'httpTransfers',
        'httpUploads',
        'httpUploadBytes',
        'httpActiveUploadBytes',
        'httpProfilePhysical',
        'httpCookieVisits',
        'httpCookieCallbacks',
        'httpScopeCommands',
      ].map((key) => [key, z.number()]),
    ),
  )
  const diagnostics = schema.parse(report.final)
  for (const [key, value] of Object.entries(diagnostics)) assert.equal(value, 0, key)
  await source.close()
  report.exit = source.processExit?.()
  assert.ok(report.exit)
  assert.equal(report.exit.code, 0)
  assert.equal(report.stderr, '')
} catch (error) {
  report.error = String(error)
  report.stack = error instanceof Error ? error.stack : undefined
  process.exitCode = 1
} finally {
  await api?.dispose().catch((error) => {
    report.cleanup = String(error)
    process.exitCode = 1
  })
  await source?.close().catch((error) => {
    report.shutdown = String(error)
    process.exitCode = 1
  })
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
