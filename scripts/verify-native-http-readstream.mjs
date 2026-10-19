// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { close, createReadStream, open, read } from 'node:fs'
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'
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
/** @type {typeof import('../src/native-http-stream.js')} */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The exact private host helper is compiled and hashed alongside the selected SDK.
const { NativeMultipartStreamJob } = await import(
  pathToFileURL(join(sdk, 'native-http-stream.js')).href
)
const output = resolve(
  process.env.SUITECUT_NATIVE_READSTREAM_REPORT ??
    '/tmp/suitecut-http-stream-stage/wire-report.json',
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
    response.end('actual native ReadStream multipart')
  })().catch((error) => response.destroy(error instanceof Error ? error : new Error(String(error))))
})
await new Promise((resolve) => server.listen(0, () => resolve(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = 'http://localhost:' + address.port
/** @type {import('../src/native-browser.js').NativeBrowser|undefined} */
let source
/** @type {import('../src/native-http.js').NativeAPIRequestContext|undefined} */
let api
const directory = await mkdtemp(join(tmpdir(), 'suitecut-readstream-wire-'))
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
/**
 * Retains a genuine completed kernel read callback so logical native cancellation
 * can be observed independently of physical host I/O settlement.
 * @param {string} path
 */
function heldRead(path) {
  /**
   * Signals that the actual kernel read has completed.
   * @type {() => void}
   */
  let notifyRead = () => undefined
  const actualRead = new Promise((resolve) => {
    notifyRead = () => resolve(undefined)
  })
  /** @type {(() => void)|undefined} */
  let retained
  let releaseRequested = false
  const stream = createReadStream(path, {
    fs: {
      open,
      close,
      /**
       * Performs the actual OS read and retains only its original callback.
       * @param {number} fd
       * @param {Buffer} buffer
       * @param {number} offset
       * @param {number} length
       * @param {number|null} position
       * @param {(error:NodeJS.ErrnoException|null, bytes:number, data:Buffer)=>void} callback
       */
      read(fd, buffer, offset, length, position, callback) {
        read(fd, buffer, offset, length, position, (error, bytes, data) => {
          if (releaseRequested) callback(error, bytes, data)
          else retained = () => callback(error, bytes, data)
          notifyRead()
        })
      },
    },
  })
  const closed = new Promise((resolve) => stream.once('close', () => resolve(undefined)))
  return {
    stream,
    actualRead,
    closed,
    release() {
      releaseRequested = true
      const callback = retained
      retained = undefined
      assert.ok(callback, 'The genuine kernel read callback must exist')
      callback()
    },
  }
}
const diagnosticsSchema = z.object({
  httpUploadPreparations: z.number(),
  httpRetainedUploadCapacity: z.number(),
  httpActive: z.number(),
  httpTransfers: z.number(),
  httpActiveUploadBytes: z.number(),
  httpCookieCallbacks: z.number(),
  httpContexts: z.number(),
})
const diagnostics = async () => {
  assert.ok(source)
  return diagnosticsSchema.parse(await source.runAutomation('diagnostics', {}))
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
  const path = join(directory, 'actual.json')
  const json = Buffer.from('{' + '"actual":true,' + '"bytes":"' + 'a'.repeat(5085) + '"}')
  await writeFile(path, json)
  let response = await api.post(origin + '/json-stream', {
    multipart: {
      firstName: 'John',
      lastName: 'Doe',
      readStream: createReadStream(path, { emitClose: false }),
    },
  })
  assert.equal(response.status(), 200)
  await response.dispose()
  checkWire([
    { name: 'firstName', body: Buffer.from('John') },
    { name: 'lastName', body: Buffer.from('Doe') },
    {
      name: 'readStream',
      body: await readFile(path),
      filename: 'actual.json',
      mime: 'application/json',
    },
  ])
  report.phases.push(
    'genuine emitClose:false ReadStream bytes, basename and native MIME inference reach exact native multipart wire with matching Content-Length',
  )
  const binary = Buffer.alloc(160 * 1024)
  for (let index = 0; index < binary.length; ++index) binary[index] = index % 256
  const binaryPath = join(directory, 'range.bin')
  await writeFile(binaryPath, binary)
  response = await api.post(origin + '/range-stream', {
    multipart: {
      before: 'actual',
      range: createReadStream(binaryPath, { start: 5, end: binary.length - 8 }),
      after: false,
    },
  })
  assert.equal(response.status(), 200)
  await response.dispose()
  checkWire([
    { name: 'before', body: Buffer.from('actual') },
    {
      name: 'range',
      body: binary.subarray(5, -7),
      filename: 'range.bin',
      mime: 'application/octet-stream',
    },
    { name: 'after', body: Buffer.from('false') },
  ])
  report.phases.push(
    'actual ranged binary file traverses multiple bounded native append commands and retains ordered fields and exact bytes',
  )
  const beforeReject = received.length
  await assert.rejects(
    api.post(origin + '/encoded-limit', {
      multipart: {
        huge: createReadStream(binaryPath),
        raw: { name: 'large.bin', buffer: Buffer.alloc(8 * 1024 * 1024 - binary.length) },
      },
    }),
    /8 MiB|upload|multipart/iu,
  )
  assert.equal(received.length, beforeReject)
  report.phases.push(
    'native final multipart wire limit rejects streamed raw bytes plus framing before any origin request',
  )
  for (const mode of ['deadline', 'cancel']) {
    const held = heldRead(binaryPath),
      controller = new AbortController()
    const before = received.length
    /** @type {Promise<{response:import('../src/native-http.js').NativeAPIResponse}|{error:Error}>} */
    const pending = api
      .post(origin + '/held-' + mode, {
        multipart: { held: held.stream },
        timeout: mode === 'deadline' ? 500 : 5000,
        signal: controller.signal,
      })
      .then(
        (value) => ({ response: value }),
        (error) => ({ error: error instanceof Error ? error : new Error(String(error)) }),
      )
    try {
      const readFirst = await Promise.race([
        held.actualRead.then(() => true),
        pending.then(() => false),
      ])
      assert.ok(readFirst, 'The genuine kernel read must happen before the native request outcome')
      const admitted = await diagnostics()
      assert.equal(admitted.httpUploadPreparations, 1)
      assert.equal(admitted.httpActive, 1)
      assert.deepEqual(NativeMultipartStreamJob.physical(), { jobs: 1, streams: 1 })
      if (mode === 'cancel') controller.abort(new Error('actual held host upload cancellation'))
      const result = await pending
      assert.ok('error' in result)
      assert.ok(result.error instanceof Error)
      if (mode === 'deadline') assert.equal(result.error.name, 'TimeoutError')
      else assert.match(result.error.message, /actual held host upload cancellation/u)
      const canceled = await diagnostics()
      assert.equal(canceled.httpUploadPreparations, 0)
      assert.equal(canceled.httpActive, 0)
      assert.equal(canceled.httpActiveUploadBytes, 0)
      assert.equal(canceled.httpRetainedUploadCapacity, 0)
      assert.equal(received.length, before)
      assert.equal(held.stream.closed, false)
      assert.deepEqual(NativeMultipartStreamJob.physical(), { jobs: 1, streams: 1 })
      held.release()
      await held.closed
      await setImmediate()
      assert.deepEqual(NativeMultipartStreamJob.physical(), { jobs: 0, streams: 0 })
      report.phases.push(
        'original native ' +
          mode +
          ' retires the held preparation before wire dispatch while physical host I/O remains retained until its genuine callback and close',
      )
    } finally {
      if (!held.stream.closed) {
        held.release()
        held.stream.destroy()
        await held.closed
      }
    }
  }
  await api.dispose()
  report.final = await diagnostics()
  assert.equal(diagnosticsSchema.parse(report.final).httpContexts, 0)
  assert.deepEqual(NativeMultipartStreamJob.physical(), { jobs: 0, streams: 0 })
  await source.close()
  const exit = source.processExit?.()
  assert.ok(exit)
  report.exit = exit
  assert.equal(exit.code, 0)
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
  await rm(directory, { recursive: true, force: true })
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
