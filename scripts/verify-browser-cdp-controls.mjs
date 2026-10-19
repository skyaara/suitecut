#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises the selectable emitted SDK against a real browser. */
/* global document, window */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { access, mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2) {
  const option = process.argv[index]
  const value = process.argv[index + 1]
  if (!option?.startsWith('--') || value === undefined) throw new Error('Expected --option value')
  options.set(option, value)
}
const sdkPath = resolve(options.get('--sdk') ?? 'dist/remote-browser.js')
const executablePath = resolve(
  options.get('--chrome') ?? '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
)
const output = resolve(options.get('--output') ?? '.suitecut/cdp-completion/advanced-controls.json')
const selected = options.has('--only') ? new Set(options.get('--only').split(',')) : null
const sdk = await import(pathToFileURL(sdkPath).href)
const temporary = await mkdtemp(join(tmpdir(), 'suitecut-cdp-proof-'))
const payload = Buffer.from('SuiteCut actual browser download\nUTF-8: π ✓\n')
const uploadPayload = 'SuiteCut actual upload bytes\nA&B=42\n'
const uploadPath = join(temporary, 'sample upload.txt')
await writeFile(uploadPath, uploadPayload)
const report = {
  sdk: sdkPath,
  executablePath,
  startedAt: new Date().toISOString(),
  checks: [],
  errors: [],
  passed: false,
  videos: false,
}
let origin = '',
  crossOrigin = ''
let browser, context, page, ownedDownloadPath

const commonStyle =
  '<style>body{font:16px sans-serif;margin:10px}label,button,select,a{display:block;margin:8px}iframe{display:block;width:95%;height:210px;margin:8px}input{margin:4px}</style>'
const frame = (
  nested,
) => `<!doctype html><meta charset="utf-8"><title>Frame fixture</title>${commonStyle}
<label>Frame value<input id="frame-value"></label><button id="frame-button">Commit frame</button><output id="frame-result"></output>
<label>Frame upload<input id="frame-upload" type="file"></label>
${nested ? `<iframe id="nested" title="Nested controls" src="${origin}/nested"></iframe>` : ''}
<script>document.querySelector('#frame-button').onclick=event=>{document.querySelector('#frame-result').textContent=document.querySelector('#frame-value').value;window.clickTrusted=event.isTrusted};document.querySelector('#frame-upload').onchange=async event=>{window.upload={count:event.target.files.length,name:event.target.files[0]?.name,text:event.target.files[0]?await event.target.files[0].text():null,trusted:event.isTrusted}}</script>`
const html =
  () => `<!doctype html><html lang="en"><meta charset="utf-8"><title>Advanced native CDP controls</title>${commonStyle}
<label>Calendar date<input id="date" type="date"></label><label>Clock time<input id="time" type="time"></label>
<label>Meeting timestamp<input id="datetime" type="datetime-local"></label><label>Billing month<input id="month" type="month"></label><label>Planning week<input id="week" type="week"></label>
<label>Exact select<select id="choice"><option value="a">Alpha</option><option value="b">Beta</option></select></label>
<input id="placeholder" placeholder="Search inventory"><label>Upload payload<input id="upload" type="file" multiple></label>
<a id="download" href="/download">Download actual export</a><button id="alert">Alert</button><button id="confirm">Confirm</button><button id="prompt">Prompt</button>
<button id="popup">Open popup</button><button id="hover">Hover target</button>
<div id="drag-start" style="display:inline-block;width:100px;height:40px;background:lightblue">Drag source</div><div id="drag-end" style="display:inline-block;margin-left:100px;width:100px;height:40px;background:lightgreen">Drop target</div>
<iframe id="same" title="Same-origin controls" src="/frame"></iframe><iframe id="cross" title="Cross-origin controls" src="${crossOrigin}/cross"></iframe>
<div style="height:1400px"></div><button id="bottom">Bottom action</button>
<script>
window.dialogResults=[];window.formEvents=[];window.mouseEvents=[];
for(const element of document.querySelectorAll('input,select'))for(const type of ['input','change'])element.addEventListener(type,event=>window.formEvents.push({id:element.id,type,value:element.value}));
document.querySelector('#upload').onchange=async event=>{window.upload={count:event.target.files.length,names:[...event.target.files].map(file=>file.name),texts:await Promise.all([...event.target.files].map(file=>file.text())),trusted:event.isTrusted}};
document.querySelector('#alert').onclick=()=>{alert('actual alert');window.dialogResults.push({type:'alert'})};
document.querySelector('#confirm').onclick=()=>window.dialogResults.push({type:'confirm',value:confirm('actual confirm')});
document.querySelector('#prompt').onclick=()=>window.dialogResults.push({type:'prompt',value:prompt('actual prompt','seed')});
document.querySelector('#popup').onclick=()=>window.open('/popup','_blank');
document.querySelector('#hover').onmouseenter=event=>window.hoverTrusted=event.isTrusted;
let dragging=false;document.querySelector('#drag-start').onmousedown=event=>{dragging=true;window.mouseEvents.push({type:'down',trusted:event.isTrusted})};
document.onmousemove=event=>{if(dragging)window.mouseEvents.push({type:'move',trusted:event.isTrusted})};
document.onmouseup=event=>{if(dragging){window.mouseEvents.push({type:'up',trusted:event.isTrusted,target:event.target.id});dragging=false}};
document.querySelector('#bottom').onclick=event=>window.bottomTrusted=event.isTrusted;
</script></html>`
const serve = (request, response) => {
  const path = new URL(request.url, 'http://fixture').pathname
  if (path === '/download') {
    response.writeHead(200, {
      'content-type': 'application/octet-stream',
      'content-disposition': 'attachment; filename="actual-export.txt"',
      'content-length': payload.length,
    })
    response.end(payload)
    return
  }
  if (path === '/favicon.ico') {
    response.writeHead(204)
    response.end()
    return
  }
  response.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
  })
  response.end(
    path === '/frame'
      ? frame(true)
      : path === '/nested' || path === '/cross'
        ? frame(false)
        : path === '/popup'
          ? '<!doctype html><title>Actual popup</title><h1>Popup ready</h1>'
          : html(),
  )
}
const server = createServer(serve),
  crossServer = createServer(serve)
await Promise.all([
  new Promise((resolve) => server.listen(0, '127.0.0.1', resolve)),
  new Promise((resolve) => crossServer.listen(0, '127.0.0.1', resolve)),
])
origin = `http://127.0.0.1:${server.address().port}`
crossOrigin = `http://localhost:${crossServer.address().port}`
report.origins = [origin, crossOrigin]

async function until(operation, description, timeout = 5000) {
  const deadline = performance.now() + timeout
  for (;;) {
    const value = await operation()
    if (value) return value
    if (performance.now() >= deadline) throw new Error(`Timed out waiting for ${description}`)
    await delay(10)
  }
}
async function check(name, operation) {
  if (selected && !selected.has(name)) return
  const started = performance.now()
  try {
    const evidence = await operation()
    report.checks.push({
      name,
      passed: true,
      durationMs: performance.now() - started,
      evidence: evidence ?? null,
    })
    console.log(`PASS ${name}`)
  } catch (error) {
    const detail = {
      name,
      passed: false,
      durationMs: performance.now() - started,
      error: error.stack ?? String(error),
    }
    report.checks.push(detail)
    report.errors.push(detail)
    console.error(`FAIL ${name}: ${error.message}`)
  }
}

try {
  browser = await sdk.launchProtocolBrowser({
    backend: 'cdp',
    executablePath,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--site-per-process'],
    viewport: { width: 1400, height: 1400 },
    timeoutMs: 15_000,
  })
  report.browserVersion = browser.version()
  report.capabilities = browser.capabilities()
  context = await browser.newContext()
  page = await context.newPage()
  page.setDefaultTimeout(2500)
  await page.goto(origin)
  await check('special-form-inputs', async () => {
    const values = {
      date: '2026-10-18',
      time: '13:42',
      datetime: '2026-10-18T13:42',
      month: '2026-10',
      week: '2026-W42',
    }
    for (const [id, value] of Object.entries(values)) await page.locator(`#${id}`).fill(value)
    await page.getByLabel('Exact select', { exact: true }).selectOption({ label: 'Beta' })
    await page.getByPlaceholder('Search inventory', { exact: true }).fill('blue widgets')
    const state = await page.evaluate(() => ({
      values: Object.fromEntries(
        ['date', 'time', 'datetime', 'month', 'week', 'choice', 'placeholder'].map((id) => [
          id,
          document.getElementById(id).value,
        ]),
      ),
      events: window.formEvents,
    }))
    assert.deepEqual(state.values, { ...values, choice: 'b', placeholder: 'blue widgets' })
    for (const id of Object.keys(values))
      for (const type of ['input', 'change'])
        assert.ok(
          state.events.some((event) => event.id === id && event.type === type),
          `${id} emits ${type}`,
        )
    return state
  })
  await check('same-origin-and-nested-frames', async () => {
    const outer = page.frameLocator('#same')
    await outer.getByLabel('Frame value', { exact: true }).fill('same origin value')
    await outer.getByRole('button', { name: 'Commit frame', exact: true }).click()
    assert.equal(await outer.locator('#frame-result').textContent(), 'same origin value')
    const nested = outer.frameLocator('#nested')
    await nested.getByLabel('Frame value', { exact: true }).fill('nested value')
    await nested.locator('#frame-button').click()
    assert.equal(await nested.locator('#frame-result').textContent(), 'nested value')
    const frames = await page.frames()
    assert.ok(frames.length >= 4)
    return {
      frames: await Promise.all(
        frames.map(async (frame) => ({ id: frame.id, url: await frame.url() })),
      ),
      sameValue: 'same origin value',
      nestedValue: 'nested value',
    }
  })
  await check('cross-origin-frame-controls', async () => {
    const cross = page.frameLocator('#cross')
    await cross.getByLabel('Frame value', { exact: true }).fill('cross origin value')
    await cross.locator('#frame-button').click()
    assert.equal(await cross.locator('#frame-result').textContent(), 'cross origin value')
    const frames = await page.frames()
    const frame = (
      await Promise.all(frames.map(async (value) => ({ frame: value, url: await value.url() })))
    ).find((value) => value.url === `${crossOrigin}/cross`)?.frame
    assert.ok(frame, 'Cross-origin frame is discoverable through public frames()')
    assert.equal(await frame.evaluate(() => window.clickTrusted), true)
    return { url: await frame.url(), trustedClick: true }
  })
  await check('actual-file-upload-and-clear', async () => {
    await page.locator('#upload').setInputFiles(uploadPath)
    await until(() => page.evaluate(() => window.upload?.texts?.length === 1), 'uploaded bytes')
    assert.deepEqual(
      await page.evaluate(() => ({
        count: window.upload.count,
        names: window.upload.names,
        texts: window.upload.texts,
      })),
      { count: 1, names: ['sample upload.txt'], texts: [uploadPayload] },
    )
    await page.locator('#upload').setInputFiles([])
    await until(() => page.evaluate(() => window.upload?.count === 0), 'cleared upload')
    await assert.rejects(page.locator('#upload').setInputFiles(join(temporary, 'missing-file.txt')))
    return {
      bytes: Buffer.byteLength(uploadPayload),
      filename: 'sample upload.txt',
      cleared: true,
      missingPathRejected: true,
    }
  })
  await check('cross-origin-frame-upload', async () => {
    const cross = page.frameLocator('#cross')
    await cross.locator('#frame-upload').setInputFiles(uploadPath)
    const frames = await page.frames()
    const frame = (
      await Promise.all(frames.map(async (value) => ({ frame: value, url: await value.url() })))
    ).find((value) => value.url === `${crossOrigin}/cross`)?.frame
    assert.ok(frame)
    await until(
      () => frame.evaluate(() => window.upload?.text !== undefined),
      'cross-origin uploaded bytes',
    )
    const value = await frame.evaluate(() => ({
      name: window.upload.name,
      text: window.upload.text,
      count: window.upload.count,
    }))
    assert.deepEqual(value, { name: 'sample upload.txt', text: uploadPayload, count: 1 })
    await cross.locator('#frame-upload').setInputFiles([])
    assert.equal(
      await frame.evaluate(() => document.querySelector('#frame-upload').files.length),
      0,
    )
    return value
  })
  await check('event-driven-download', async () => {
    const pending = page.waitForEvent('download', { timeout: 5000 })
    await page.locator('#download').click()
    const download = await pending
    assert.equal(download.suggestedFilename(), 'actual-export.txt')
    assert.equal(download.url(), `${origin}/download`)
    ownedDownloadPath = await download.path()
    assert.deepEqual(await readFile(ownedDownloadPath), payload)
    const saved = join(temporary, 'saved-export.txt')
    await download.saveAs(saved)
    assert.deepEqual(await readFile(saved), payload)
    assert.equal(await download.failure(), null)
    return {
      actualBytes: payload.length,
      suggestedFilename: download.suggestedFilename(),
      url: download.url(),
      savedBytesMatch: true,
    }
  })
  await check('dialogs-accept-dismiss-and-default', async () => {
    const evidence = []
    for (const [id, type, accept, promptText] of [
      ['alert', 'alert', true],
      ['confirm', 'confirm', true],
      ['confirm', 'confirm', false],
      ['prompt', 'prompt', true, 'entered value'],
      ['prompt', 'prompt', false],
    ]) {
      const pending = page.waitForEvent('dialog', { timeout: 5000 })
      const click = page.locator(`#${id}`).click()
      const dialog = await pending
      assert.equal(dialog.type(), type)
      assert.equal(dialog.message(), `actual ${type}`)
      if (type === 'prompt') assert.equal(dialog.defaultValue(), 'seed')
      await (accept ? dialog.accept(promptText) : dialog.dismiss())
      await click
      evidence.push({ type, accepted: accept })
    }
    await page.locator('#confirm').click()
    await until(
      () => page.evaluate(() => window.dialogResults.length === 6),
      'default dialog dismissal',
    )
    const results = await page.evaluate(() => window.dialogResults)
    assert.deepEqual(results, [
      { type: 'alert' },
      { type: 'confirm', value: true },
      { type: 'confirm', value: false },
      { type: 'prompt', value: 'entered value' },
      { type: 'prompt', value: null },
      { type: 'confirm', value: false },
    ])
    return { events: evidence, defaultDismissed: true, results }
  })
  await check('popup-cache-and-background-screenshot', async () => {
    const pending = page.waitForEvent('popup', { timeout: 5000 })
    await page.locator('#popup').click()
    const popup = await pending
    await until(async () => (await popup.url()) === `${origin}/popup`, 'popup URL')
    await until(async () => (await popup.title()) === 'Actual popup', 'popup title')
    await popup.bringToFront()
    const samples = []
    for (let index = 0; index < 100; index++) {
      const started = performance.now()
      assert.equal(await page.url(), `${origin}/`)
      assert.equal(await popup.url(), `${origin}/popup`)
      samples.push(performance.now() - started)
    }
    const backgroundActionStarted = performance.now()
    await page.locator('#hover').hover()
    const backgroundActionMs = performance.now() - backgroundActionStarted
    assert.ok(
      backgroundActionMs < 1000,
      'A control action must activate its background page without a five-second stall',
    )
    await popup.bringToFront()
    const screenshotStarted = performance.now()
    const screenshot = await page.screenshot()
    const screenshotMs = performance.now() - screenshotStarted
    assert.deepEqual(screenshot.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
    assert.ok(screenshotMs < 10_000, 'Background screenshot must not hit a protocol timeout')
    assert.ok(
      Math.max(...samples) < 1000,
      'Cached URL reads must not wait for a background renderer',
    )
    await popup.close()
    assert.equal(popup.isClosed(), true)
    assert.ok(!(await browser.pages()).some((value) => value.id === popup.id))
    return {
      cachedUrlPairs: samples.length,
      maxCachedUrlPairMs: Math.max(...samples),
      backgroundActionMs,
      screenshotMs,
      screenshotBytes: screenshot.length,
      popupClosed: true,
    }
  })
  await check('trusted-hover-drag-and-scroll', async () => {
    await page.locator('#hover').hover()
    assert.equal(await page.evaluate(() => window.hoverTrusted), true)
    await page.locator('#drag-start').dragTo(page.locator('#drag-end'), { steps: 8 })
    const events = await page.evaluate(() => window.mouseEvents)
    assert.ok(events.length >= 3)
    assert.ok(events.every((event) => event.trusted))
    assert.equal(events[0].type, 'down')
    assert.equal(events.at(-1).type, 'up')
    assert.equal(events.at(-1).target, 'drag-end')
    await page.locator('#bottom').scrollIntoViewIfNeeded()
    await page.locator('#bottom').click()
    assert.equal(await page.evaluate(() => window.bottomTrusted), true)
    return { trustedHover: true, trustedDragEvents: events.length, trustedScrolledClick: true }
  })
  await check('closed-context-retirement', async () => {
    await context.close()
    assert.equal(page.isClosed(), true)
    await assert.rejects(
      page.evaluate(() => document.title),
      /closed/u,
    )
    assert.ok(!(await browser.pages()).some((value) => value.id === page.id))
    return { closedPageRejected: true, contextPagesRetired: true }
  })
} catch (error) {
  report.errors.push({ name: 'setup-or-fatal', error: error.stack ?? String(error) })
  console.error(error)
} finally {
  const started = performance.now()
  try {
    await browser?.close()
    await browser?.closed
    report.cleanup = {
      browserClosed: !browser?.isConnected(),
      pendingRequests: browser?.connectionStats().pending ?? 0,
      eventListeners: browser?.connectionStats().eventListeners ?? 0,
      durationMs: performance.now() - started,
    }
    assert.equal(report.cleanup.pendingRequests, 0)
    assert.equal(report.cleanup.eventListeners, 0)
    if (ownedDownloadPath) await assert.rejects(access(ownedDownloadPath), { code: 'ENOENT' })
  } catch (error) {
    report.errors.push({ name: 'cleanup', error: error.stack ?? String(error) })
  }
  await Promise.all([
    new Promise((resolve) => server.close(resolve)),
    new Promise((resolve) => crossServer.close(resolve)),
  ])
  await rm(temporary, { recursive: true, force: true })
  report.passed = report.errors.length === 0 && report.checks.length > 0
  report.finishedAt = new Date().toISOString()
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, `${JSON.stringify(report, null, 2)}\n`)
}
console.log(
  `${report.checks.filter((value) => value.passed).length}/${report.checks.length} passed; evidence: ${output}`,
)
if (!report.passed) process.exitCode = 1
