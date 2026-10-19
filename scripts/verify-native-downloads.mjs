// SPDX-License-Identifier: MIT
/* global Blob, document, URL */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { access, mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { Socket } from 'node:net'
import { dirname, join, relative, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearInterval, clearTimeout, setInterval, setTimeout } from 'node:timers'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = resolve('.suitecut/native-downloads')
const downloadsRoot = join(directory, 'download-root')
const artifactsRoot = join(directory, 'artifact-root')
await mkdir(directory, { recursive: true })
await rm(join(directory, 'report.json'), { force: true })
await rm(join(directory, 'failure.json'), { force: true })
const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
/**
 * Hash the exact bytes used by this verifier.
 * @param {string} path
 */
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(path))
    .digest('hex')
const binarySHA256 = await hash(executable)
const cefSHA256 = await hash(join(dirname(executable), 'libcef.so'))
const checks = []
const requestCounts = new Map([['', 0]])
requestCounts.clear()
const sockets = new Set(z.array(z.instanceof(Socket)).parse([]))
const simpleBytes = Buffer.from([0, 255, 128, 10, 65, 0, 66])
const duplicateBodies = [
  Buffer.from('A'.repeat(8192) + 'first-body'),
  Buffer.from('B'.repeat(8192) + 'second-body'),
]
const unknownBody = Buffer.alloc(8 * 65536, 117)
let duplicateCount = 0
let diagnostics = ''
let source
let phase = 'launch'
const server = createServer((request, response) => {
  const path = request.url ?? '/'
  requestCounts.set(path, (requestCounts.get(path) ?? 0) + 1)
  response.setHeader('Content-Type', 'application/octet-stream')
  response.setHeader(
    'Content-Disposition',
    "attachment; filename*=UTF-8''%E6%B8%AC%E8%A9%A6-%C3%A9.bin",
  )
  if (path === '/redirect') {
    response.writeHead(302, { Location: '/bytes' })
    response.end()
  } else if (path === '/bytes') {
    response.setHeader('Content-Length', simpleBytes.length)
    response.end(simpleBytes)
  } else if (path === '/unsupported') {
    response.removeHeader('Content-Disposition')
    response.setHeader('Content-Length', simpleBytes.length)
    response.end(simpleBytes)
  } else if (path === '/duplicate') {
    const index = duplicateCount++
    const bytes = duplicateBodies[index % duplicateBodies.length]
    response.setHeader('Content-Length', bytes.length)
    response.write(bytes.subarray(0, 4096))
    const timer = setTimeout(() => response.end(bytes.subarray(4096)), index === 0 ? 350 : 5)
    response.on('close', () => clearTimeout(timer))
  } else if (path === '/unknown') {
    let offset = 0
    const timer = setInterval(() => {
      response.write(unknownBody.subarray(offset, offset + 65536))
      offset += 65536
      if (offset === unknownBody.length) {
        clearInterval(timer)
        response.end()
      }
    }, 15)
    response.on('close', () => clearInterval(timer))
  } else if (path.startsWith('/stalled')) {
    response.write(Buffer.alloc(8192, 115))
    const timer = setInterval(() => response.write(Buffer.alloc(8192, 115)), 50)
    response.on('close', () => clearInterval(timer))
  } else if (path === '/truncated') {
    response.setHeader('Content-Length', 65536)
    response.write(Buffer.alloc(8192, 116))
    const timer = setTimeout(() => response.destroy(), 100)
    response.on('close', () => clearTimeout(timer))
  } else if (path === '/declared-over-numeric-range') {
    response.setHeader('Content-Length', '18014398509481984')
    response.end(Buffer.alloc(8192, 120))
  } else if (path === '/large') {
    const bytes = Buffer.alloc(64 * 1048576, 108)
    response.setHeader('Content-Length', bytes.length)
    response.end(bytes)
  } else {
    response.setHeader('Content-Type', 'text/html')
    response.removeHeader('Content-Disposition')
    response.end('<title>native download fixture</title>')
  }
})
server.on('connection', (socket) => {
  sockets.add(socket)
  socket.on('close', () => sockets.delete(socket))
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = z.object({ port: z.number().int().positive() }).parse(server.address())
const origin = `http://127.0.0.1:${address.port}`
/**
 * Check that a native-owned path has been removed.
 * @param {string} path
 */
const missing = async (path) => assert.rejects(access(path), /ENOENT/u)
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    downloadsPath: relative(process.cwd(), downloadsRoot),
    headless: true,
    width: 640,
    height: 360,
    onDiagnostic(message) {
      if (diagnostics.length < 1048576)
        diagnostics += message.slice(0, 1048576 - diagnostics.length)
    },
  })
  const browser = new NativeBrowserContexts(source)
  const context = await browser.newContext()
  const page = await context.newPage()
  /**
   * Use a trusted click and the actual native event.
   * @param {import('../dist/native-page.js').NativePage} target
   * @param {string} path
   */
  const trigger = async (target, path) => {
    await target.setContent(`<a id="download" href="${origin}${path}">Download</a>`)
    const [download] = await Promise.all([
      target.waitForEvent('download'),
      target.click('#download'),
    ])
    return download
  }
  phase = 'navigation becomes native download'
  console.log(`Native downloads phase: ${phase}`)
  const [navigationDownload, navigationError] = await Promise.all([
    page.waitForEvent('download'),
    page
      .goto(`${origin}/bytes`)
      .catch((error) => (error instanceof Error ? error : new Error(String(error)))),
  ])
  assert(navigationError instanceof Error)
  assert.match(navigationError.message, /Download is starting/u)
  assert.equal(navigationDownload.page(), page)
  assert.equal(navigationDownload.url(), `${origin}/bytes`)
  assert.equal(navigationDownload.suggestedFilename(), '測試-é.bin')
  const nativePath = await navigationDownload.path()
  assert(nativePath.startsWith(downloadsRoot + '/'))
  assert.deepEqual(await readFile(nativePath), simpleBytes)
  assert.equal(await navigationDownload.failure(), null)
  checks.push('CEF-navigation-download-real-binary-Unicode')

  phase = 'native saveAs streams overwrite delete and retained page close'
  console.log(`Native downloads phase: ${phase}`)
  const outputA = join(directory, 'copies', '測試', 'a.bin')
  const outputB = join(directory, 'copies', 'b.bin')
  await writeFile(outputB, Buffer.from('previous')).catch(async () => {
    await mkdir(dirname(outputB), { recursive: true })
    await writeFile(outputB, Buffer.from('previous'))
  })
  await navigationDownload.saveAs(outputA)
  await navigationDownload.saveAs(outputB)
  await navigationDownload.saveAs(outputA)
  assert.deepEqual(await readFile(outputA), simpleBytes)
  assert.deepEqual(await readFile(outputB), simpleBytes)
  const chunks = []
  for await (const chunk of await navigationDownload.createReadStream()) {
    assert(Buffer.isBuffer(chunk))
    chunks.push(chunk)
  }
  assert.deepEqual(Buffer.concat(chunks), simpleBytes)
  await page.close()
  assert.deepEqual(await readFile(nativePath), simpleBytes)
  await navigationDownload.saveAs(outputB)
  checks.push('native-copy-nested-overwrite-repeat-real-stream-context-page-retention')
  await navigationDownload.delete()
  await navigationDownload.delete()
  await missing(nativePath)
  await assert.rejects(
    navigationDownload.saveAs(outputA),
    /Target page, context or browser has been closed/u,
  )
  checks.push('native-delete-real-file-idempotent')

  phase = 'redirect and genuine Blob download'
  console.log(`Native downloads phase: ${phase}`)
  const redirectPage = await context.newPage()
  const redirected = await trigger(redirectPage, '/redirect')
  assert.equal(redirected.url(), `${origin}/bytes`)
  assert.equal(redirected.originalURL(), `${origin}/redirect`)
  assert.deepEqual(await readFile(await redirected.path()), simpleBytes)
  let observed
  /**
   * Observe identity shared between the native listener and waiter.
   * @param {import('../dist/native-download.js').NativeDownload} download
   */
  const listener = (download) => {
    observed = download
  }
  redirectPage.on('download', listener)
  const blobEvent = redirectPage.waitForEvent('download')
  await redirectPage.evaluate(() => {
    const anchor = document.createElement('a')
    anchor.id = 'blob'
    anchor.href = URL.createObjectURL(
      new Blob(['real native blob bytes'], { type: 'application/octet-stream' }),
    )
    anchor.download = 'blob-測試.txt'
    anchor.textContent = 'Blob'
    document.body.append(anchor)
  })
  await redirectPage.click('#blob')
  const blob = await blobEvent
  assert.equal(observed, blob)
  redirectPage.off('download', listener)
  assert.match(blob.url(), /^blob:/u)
  assert.equal(blob.suggestedFilename(), 'blob-測試.txt')
  assert.equal(await readFile(await blob.path(), 'utf8'), 'real native blob bytes')
  checks.push('real-redirect-and-Blob-native-callback-event-identity')

  phase = 'concurrent identical URLs with reversed completion'
  console.log(`Native downloads phase: ${phase}`)
  const duplicatePages = await Promise.all([context.newPage(), context.newPage()])
  const duplicates = await Promise.all(
    duplicatePages.map((target) => trigger(target, '/duplicate')),
  )
  const duplicatePaths = await Promise.all(duplicates.map((download) => download.path()))
  assert.notEqual(duplicatePaths[0], duplicatePaths[1])
  const duplicateHashes = await Promise.all(duplicatePaths.map(hash))
  assert.deepEqual(
    duplicateHashes.sort(),
    duplicateBodies.map((bytes) => createHash('sha256').update(bytes).digest('hex')).sort(),
  )
  assert.equal(requestCounts.get('/duplicate'), 2)
  checks.push('native-identical-URL-downloads-independent-files-by-native-ID')

  phase = 'unknown-length EOF and truncated response'
  console.log(`Native downloads phase: ${phase}`)
  const unknown = await trigger(redirectPage, '/unknown')
  assert.deepEqual(await readFile(await unknown.path()), unknownBody)
  const unknownInfo = await unknown.status()
  assert.equal(unknownInfo.receivedBytes, unknownBody.length)
  assert.equal(unknownInfo.state, 'complete')
  const truncated = await trigger(redirectPage, '/truncated')
  assert.equal(typeof (await truncated.failure()), 'string')
  await assert.rejects(truncated.path(), /interrupted|network|canceled/iu)
  checks.push('real-chunked-EOF-and-truncated-transfer-failure')

  phase = 'declined download policy still emits real event'
  console.log(`Native downloads phase: ${phase}`)
  const declinedPage = await browser.newPage({ acceptDownloads: false })
  const declined = await trigger(declinedPage, '/bytes')
  assert.match(z.string().parse(await declined.failure()), /acceptDownloads/u)
  await assert.rejects(declined.path(), /acceptDownloads: true/u)
  await assert.rejects(
    declined.saveAs(outputA),
    /Pass \{ acceptDownloads: true \} when you are creating your browser context/u,
  )
  assert.equal((await declined.status()).accepted, false)
  await declinedPage.close()
  checks.push('native-acceptDownloads-false-real-event-no-file')

  phase = 'original path deadline then real native cancellation'
  console.log(`Native downloads phase: ${phase}`)
  const stalled = await trigger(redirectPage, '/stalled-deadline')
  redirectPage.setDefaultTimeout(90)
  const started = performance.now()
  await assert.rejects(
    stalled.path(),
    (error) => error instanceof Error && error.name === 'TimeoutError',
  )
  assert(performance.now() - started < 1500)
  redirectPage.setDefaultTimeout(30000)
  await stalled.cancel()
  assert.equal(await stalled.failure(), 'canceled')
  await assert.rejects(stalled.path(), /canceled/u)
  await stalled.delete()
  checks.push('native-original-deadline-cancel-physical-file-cleanup')

  phase = 'pending page close and context-owned retention'
  console.log(`Native downloads phase: ${phase}`)
  const pendingPage = await context.newPage()
  const pending = await trigger(pendingPage, '/stalled-close')
  const pendingPath = pending
    .path()
    .catch((error) => (error instanceof Error ? error : new Error(String(error))))
  await pendingPage.close()
  const pendingError = await pendingPath
  assert(pendingError instanceof Error)
  assert.match(pendingError.message, /closed|canceled/u)
  const ownedPage = await browser.newPage()
  const owned = await trigger(ownedPage, '/bytes')
  const ownedPath = await owned.path()
  await ownedPage.close()
  await missing(ownedPath)
  checks.push('native-pending-page-close-and-owned-context-delete')

  phase = 'context closes during actual native saveAs workers'
  console.log(`Native downloads phase: ${phase}`)
  const copyContext = await browser.newContext()
  const copyPage = await copyContext.newPage()
  const large = await trigger(copyPage, '/large')
  const largePath = await large.path()
  const copyResults = [
    large.saveAs(join(directory, 'large-a.bin')),
    large.saveAs(join(directory, 'large-b.bin')),
  ].map((promise) =>
    promise.catch((error) => (error instanceof Error ? error : new Error(String(error)))),
  )
  await copyContext.close()
  const results = await Promise.all(copyResults)
  assert(
    results.some((result) => result instanceof Error && /canceled|closed/u.test(result.message)),
  )
  await missing(largePath)
  checks.push('native-copy-workers-canceled-and-joined-before-context-close')

  phase = 'data URL unsupported MIME and real response interception'
  console.log(`Native downloads phase: ${phase}`)
  await redirectPage.setContent(
    '<a id="data" download="data-測試.txt" href="data:text/plain;charset=utf8;,native%20data%20bytes">Data</a>',
  )
  const [dataDownload] = await Promise.all([
    redirectPage.waitForEvent('download'),
    redirectPage.click('#data'),
  ])
  assert.equal(dataDownload.suggestedFilename(), 'data-測試.txt')
  assert.equal(await readFile(await dataDownload.path(), 'utf8'), 'native data bytes')
  const unsupported = await trigger(redirectPage, '/unsupported')
  assert.deepEqual(await readFile(await unsupported.path()), simpleBytes)
  await redirectPage.route('**/intercepted-download', async (route) => {
    await route.fulfill({
      body: 'genuine native interception bytes',
      contentType: 'application/octet-stream',
      headers: { 'Content-Disposition': 'attachment; filename=route.txt' },
    })
  })
  const intercepted = await trigger(redirectPage, '/intercepted-download')
  assert.equal(intercepted.suggestedFilename(), 'route.txt')
  assert.equal(
    await readFile(await intercepted.path(), 'utf8'),
    'genuine native interception bytes',
  )
  await redirectPage.unroute('**/intercepted-download')
  checks.push('real-data-URL-unsupported-MIME-native-Fetch-response-download')

  phase = 'download from target blank routes to the owning opener'
  console.log(`Native downloads phase: ${phase}`)
  await redirectPage.setContent(
    `<a id="popup-download" target="_blank" href="${origin}/unsupported">Popup download</a>`,
  )
  const [popupDownload] = await Promise.all([
    redirectPage.waitForEvent('download', { timeout: 3000 }),
    redirectPage.click('#popup-download'),
  ])
  assert.equal(popupDownload.page(), redirectPage)
  assert.deepEqual(await readFile(await popupDownload.path()), simpleBytes)
  checks.push('real-new-window-download-opener-event-and-file')

  phase = 'bounded native metadata stays unavailable after terminal CEF callbacks'
  console.log(`Native downloads phase: ${phase}`)
  await redirectPage.setContent(
    `<a id="metadata" download="metadata.txt" href="data:text/plain,${'x'.repeat(9000)}">Metadata</a>`,
  )
  const [oversizedMetadata] = await Promise.all([
    redirectPage.waitForEvent('download'),
    redirectPage.click('#metadata'),
  ])
  assert.match(z.string().parse(await oversizedMetadata.failure()), /metadata capacity/u)
  const bounded = await oversizedMetadata.status()
  assert.equal(bounded.url, null)
  assert.equal(bounded.originalURL, null)
  assert.throws(() => oversizedMetadata.url(), /unavailable/u)
  assert.throws(() => oversizedMetadata.originalURL(), /unavailable/u)
  await assert.rejects(oversizedMetadata.path(), /metadata capacity/u)
  const impreciseLength = await trigger(redirectPage, '/declared-over-numeric-range')
  assert.match(z.string().parse(await impreciseLength.failure()), /limit|metadata/u)
  assert.equal((await impreciseLength.status()).totalBytes, null)
  await assert.rejects(impreciseLength.path(), /limit|metadata/u)
  assert.equal(source.signal.aborted, false)
  checks.push('native-metadata-unavailable-projection-terminal-sticky-no-transport-failure')

  phase = 'retention cleanup and final native ownership counters'
  console.log(`Native downloads phase: ${phase}`)
  const retainedPaths = [
    await redirected.path(),
    await blob.path(),
    ...duplicatePaths,
    await unknown.path(),
  ]
  await context.close()
  await Promise.all(retainedPaths.map(missing))
  const final = z
    .object({
      activeDownloads: z.literal(0),
      retainedDownloadFiles: z.literal(0),
      downloadDiskBytes: z.literal(0),
      downloadReservedBytes: z.literal(0),
      downloadWaits: z.literal(0),
      downloadCopies: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(
    await hash(executable),
    binarySHA256,
    'Native executable changed during verification',
  )
  assert.equal(
    requestCounts.get('/bytes'),
    4,
    'Download APIs must use real existing native files without HTTP refetch',
  )
  checks.push('native-retention-zero-physical-files-bytes-waits-copies-no-SDK-refetch')
  await source.close()
  assert.deepEqual(await readdir(downloadsRoot), [])
  phase = 'artifactsDir fallback retains only the user parent directory'
  source = await launchNativeBrowser({
    executablePath: executable,
    artifactsDir: artifactsRoot,
    headless: true,
    width: 640,
    height: 360,
  })
  const artifactBrowser = new NativeBrowserContexts(source)
  const artifactPage = await artifactBrowser.newPage()
  const artifactDownload = await trigger(artifactPage, '/bytes')
  const artifactPath = await artifactDownload.path()
  assert(artifactPath.startsWith(artifactsRoot + '/'))
  assert.deepEqual(await readFile(artifactPath), simpleBytes)
  await artifactPage.close()
  await missing(artifactPath)
  await source.close()
  assert.deepEqual(await readdir(artifactsRoot), [])
  checks.push('real-relative-downloadsPath-artifactsDir-parent-preserved')
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefSHA256,
        checks,
        requestCounts: Object.fromEntries(requestCounts),
        final,
      },
      null,
      2,
    ),
  )
} catch (error) {
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        phase,
        binarySHA256,
        cefSHA256,
        checks,
        error:
          error instanceof Error
            ? { name: error.name, message: error.message, stack: error.stack }
            : String(error),
        requestCounts: Object.fromEntries(requestCounts),
      },
      null,
      2,
    ),
  )
  throw error
} finally {
  await writeFile(join(directory, 'native.log'), diagnostics)
  if (source) await source.close()
  for (const socket of sockets) socket.destroy()
  await new Promise((done) => server.close(done))
}
