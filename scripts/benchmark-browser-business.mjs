#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Replays recorded public SDK workflows against selectable emitted backends and fixture modules. */
/* global document, location, requestAnimationFrame, setTimeout, clearTimeout, structuredClone, innerWidth, innerHeight, devicePixelRatio */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { basename, dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2) {
  assert.ok(
    process.argv[index]?.startsWith('--') && process.argv[index + 1],
    'Expected --option value',
  )
  options.set(process.argv[index], process.argv[index + 1])
}
const output = resolve(options.get('--output') ?? '.suitecut/external-native-perf/business')
const reference = resolve(options.get('--reference') ?? '.suitecut/backend-agent-bench-fixed')
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const nativePath = resolve(
  options.get('--cef-executable') ?? '.suitecut/memory-deep/runtime/suitecut-browser',
)
const chromePath = resolve(
  options.get('--cdp-executable') ?? '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
)
const repetitions = Number(options.get('--repetitions') ?? 3)
const repetitionOffset = Number(options.get('--repetition-offset') ?? 0)
const deadlineMs = Number(options.get('--deadline-ms') ?? 240_000)
assert.ok(Number.isInteger(repetitions) && repetitions > 0 && repetitions <= 10)
assert.ok(Number.isInteger(repetitionOffset) && repetitionOffset >= 0 && repetitionOffset <= 10)
await mkdir(output, { recursive: true })
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex')
async function manifest() {
  const files = [
    resolve(process.argv[1]),
    sdkPath,
    nativePath,
    chromePath,
    resolve(reference, 'results/actions.jsonl'),
    ...['a', 'b', 'c'].map((name) => resolve(reference, `fixtures/${name}.mjs`)),
    ...(await readdir(dirname(sdkPath), { recursive: true }).then((files) =>
      files.filter((file) => file.endsWith('.js')).map((file) => resolve(dirname(sdkPath), file)),
    )),
  ]
  const hashes = {}
  for (const file of [...new Set(files)].sort()) {
    const digest = createHash('sha256')
    for await (const chunk of createReadStream(file)) digest.update(chunk)
    hashes[file] = digest.digest('hex')
  }
  return hashes
}
const before = await manifest()
const sdk = await import(pathToFileURL(sdkPath).href)
const factories = await Promise.all(
  ['a', 'b', 'c'].map(async (name) => ({
    name,
    create: (await import(pathToFileURL(resolve(reference, `fixtures/${name}.mjs`)).href))
      .createTasks,
  })),
)
const referenceRows = (await readFile(resolve(reference, 'results/actions.jsonl'), 'utf8'))
  .trim()
  .split('\n')
  .map((line) => JSON.parse(line))
const referenceReport = JSON.parse(
  await readFile(resolve(reference, 'results/report.json'), 'utf8'),
)
const plans = referenceReport.episodes
  .filter((episode) => episode.backend === 'cef')
  .map((episode) => {
    assert.equal(episode.grade.passed, true, 'Reference plan must have verified completion')
    const rows = referenceRows.filter((row) => row.episode === episode.id)
    assert.ok(
      rows.every((row) => !row.error),
      'Use successful, error-free semantic reference actions',
    )
    const actions = rows
      .filter((row) => ['act', 'switch', 'navigate'].includes(row.input.op))
      .map((row) => structuredClone(row.input))
    assert.ok(
      actions.every((input) => !input.locator?.ref),
      'Replay must not retain stale opaque refs',
    )
    return {
      family: episode.family,
      seed: episode.seed,
      referenceEpisode: episode.id,
      actions,
      omittedObservations: rows.filter((row) => row.input.op === 'observe').length,
      omittedOperatorWaits: rows.filter((row) => row.input.op === 'wait').length,
    }
  })
assert.equal(plans.length, 24)
const selectedPlans = options.has('--family')
  ? plans.filter((plan) => plan.family === options.get('--family'))
  : plans
assert.ok(selectedPlans.length)
const report = {
  scope:
    'Deterministic replay of successful semantic browser actions from 24 previous business cases; not an AI-agent benchmark.',
  sdkPath,
  reference,
  repetitions,
  repetitionOffset,
  recording: false,
  viewport: { width: 1280, height: 720 },
  dpr: 1,
  cefFramesPerSecond: 60,
  launchFlags: {
    cef: [],
    cdp: process.getuid?.() === 0 ? ['--no-sandbox', '--disable-dev-shm-usage'] : [],
  },
  readiness:
    'Public locator auto-waits; clicked busy buttons must become enabled, disappear or navigate. CRM pagination instead waits for visible pager text to change, since Next becomes permanently disabled on the last page. One public read-only requestAnimationFrame checks document readiness. Application-imposed140ms delays remain; AI thinking, observations and explicit operator waits are omitted. Fresh finance approval code is read from the visible popup and substituted into the recorded fill action.',
  guarantees:
    'Normal backend APIs, no forced clicks. Native CEF includes its existing two-frame action stability check; CDP uses its existing immediate hit test. These public guarantees differ and are included in timings.',
  before,
  plans,
  episodes: [],
  started: new Date().toISOString(),
  passed: false,
}
const active = new Map()
let origin,
  browser,
  stopping = false,
  deadline,
  workPromise
function cancel(signal) {
  stopping = true
  report.cancelled = signal
  void browser?.close().catch(() => undefined)
}
const onTerm = () => cancel('SIGTERM')
const onInterrupt = () => cancel('SIGINT')
process.once('SIGTERM', onTerm)
process.once('SIGINT', onInterrupt)
const started = performance.now()
async function handleRequest(request, response) {
  try {
    const url = new URL(request.url ?? '/', origin ?? 'http://127.0.0.1')
    const episode = active.get(url.pathname.split('/')[2])
    if (!episode) {
      response.writeHead(404)
      response.end()
      return
    }
    const chunks = []
    const originalWrite = response.write.bind(response),
      originalEnd = response.end.bind(response),
      originalWriteHead = response.writeHead.bind(response)
    let attachmentDisposition
    response.writeHead = (statusCode, ...args) => {
      const headers = typeof args[0] === 'string' ? args[1] : args[0]
      const entries = Array.isArray(headers)
        ? Array.from({ length: headers.length / 2 }, (_, index) => [
            headers[index * 2],
            headers[index * 2 + 1],
          ])
        : Object.entries(headers ?? {})
      for (const [key, value] of entries)
        if (String(key).toLowerCase() === 'content-disposition') attachmentDisposition = value
      return originalWriteHead(statusCode, ...args)
    }
    response.write = (chunk, ...args) => {
      if (chunk) chunks.push(Buffer.from(chunk))
      return originalWrite(chunk, ...args)
    }
    response.end = (chunk, ...args) => {
      if (chunk) chunks.push(Buffer.from(chunk))
      const disposition = attachmentDisposition ?? response.getHeader('content-disposition')
      if (disposition) {
        const bytes = Buffer.concat(chunks)
        episode.serverDownloads.push({ bytes: bytes.length, sha256: hash(bytes), disposition })
      }
      return originalEnd(chunk, ...args)
    }
    if (!(await episode.task.handle(request, response, url))) {
      response.writeHead(404)
      response.end()
    }
  } catch (error) {
    response.writeHead(500, { 'content-type': 'text/plain' })
    response.end(error.message)
  }
}
const server = createServer((request, response) => {
  void handleRequest(request, response)
})
function locate(owner, spec) {
  assert.ok(spec)
  let locator
  if (spec.label !== undefined)
    locator = owner.getByLabel(spec.label, { exact: spec.exact ?? true })
  else if (spec.role)
    locator = owner.getByRole(spec.role, { name: spec.name, exact: spec.exact ?? true })
  else if (spec.text !== undefined)
    locator = owner.getByText(spec.text, { exact: spec.exact ?? true })
  else if (spec.placeholder !== undefined)
    locator = owner.getByPlaceholder(spec.placeholder, { exact: spec.exact ?? true })
  else throw new Error('Replay requires semantic public locators')
  return spec.index === undefined ? locator : locator.nth(spec.index)
}
async function ready(page, target, documentEpoch, pagerBefore) {
  const until = performance.now() + 5000
  while (performance.now() < until) {
    try {
      if ((await page.evaluate(() => performance.timeOrigin)) !== documentEpoch) break
      if (pagerBefore !== undefined && (await page.locator('#pager').textContent()) !== pagerBefore)
        break
      if (!(await target.count()) || (await target.isEnabled())) break
    } catch {
      /* Reload can destroy the previous document. */
    }
    await delay(5)
  }
  while (performance.now() < until) {
    try {
      if (
        await page.evaluate(
          () =>
            new Promise((resolveFrame) =>
              requestAnimationFrame(() => resolveFrame(document.readyState === 'complete')),
            ),
        )
      )
        return
    } catch {
      /* Wait for the navigation document to become available. */
    }
    await delay(5)
  }
  throw new Error('Application readiness exceeded 5 seconds')
}
async function episodeRun(plan, backend, repetition, caseIndex) {
  const id = `r${repetition}-${String(caseIndex).padStart(2, '0')}-${backend}`
  const directory = resolve(output, id)
  await mkdir(directory, { recursive: true })
  let task
  for (const factory of factories) {
    const descriptors = await factory.create({
      baseURL: `${origin}/run/${id}`,
      assetRoot: resolve(directory, `assets-${factory.name}`),
      seed: plan.seed,
    })
    const found = descriptors.find((value) => value.family === plan.family)
    if (found) {
      task = found
      break
    }
  }
  assert.ok(task)
  const episode = {
    id,
    family: plan.family,
    seed: plan.seed,
    backend,
    repetition,
    referenceEpisode: plan.referenceEpisode,
    actions: [],
    downloads: [],
    serverDownloads: [],
    passed: false,
  }
  report.episodes.push(episode)
  active.set(id, { task, serverDownloads: episode.serverDownloads })
  const pages = new Map()
  let nextTab = 0,
    selected,
    dialogChoice,
    financeCode
  let taskStartedAt, activeAction
  function register(page) {
    const tab = ++nextTab
    pages.set(tab, page)
    page.setDefaultTimeout(5000)
    page.setDefaultNavigationTimeout?.(5000)
    page.on('dialog', (dialog) => {
      const choice = dialogChoice
      dialogChoice = undefined
      void (choice?.action === 'accept' ? dialog.accept(choice.text) : dialog.dismiss()).catch(
        () => undefined,
      )
    })
    return tab
  }
  try {
    assert.equal((await task.grade()).passed, false)
    const startup = performance.now()
    browser = await sdk.launchBrowser(
      backend === 'cef'
        ? {
            backend: 'cef',
            executablePath: nativePath,
            headless: true,
            width: 1280,
            height: 720,
            framesPerSecond: 60,
            pixelFormat: 'bgra',
            userDataDir: resolve(directory, 'profile'),
          }
        : {
            backend: 'cdp',
            executablePath: chromePath,
            headless: true,
            viewport: report.viewport,
            timeoutMs: 15_000,
            args: report.launchFlags.cdp,
          },
    )
    episode.version = browser.version()
    const context = await browser.newContext({ viewport: report.viewport, acceptDownloads: true })
    const page = await context.newPage()
    selected = register(page)
    await page.goto(origin + task.startPath)
    episode.startupMs = performance.now() - startup
    episode.observedViewport = await page.evaluate(() => ({
      width: innerWidth,
      height: innerHeight,
      dpr: devicePixelRatio,
    }))
    assert.deepEqual(episode.observedViewport, { width: 1280, height: 720, dpr: 1 })
    episode.taskStatsStart = browser.connectionStats?.()
    const taskStart = performance.now()
    taskStartedAt = taskStart
    for (const raw of plan.actions) {
      if (stopping) throw new Error('Benchmark deadline reached')
      const input = structuredClone(raw)
      activeAction = { input, startedAtMs: performance.now() }
      const page = pages.get(input.tab ?? selected)
      const frameStart = performance.now()
      const owner = input.frame === undefined ? page : (await page.frames())[input.frame]
      assert.ok(owner, `Missing frame ${input.frame}`)
      const target = input.locator ? locate(owner, input.locator) : undefined
      const documentEpoch =
        input.action === 'click' ? await page.evaluate(() => performance.timeOrigin) : undefined
      const pagerBefore =
        plan.family === 'crm-qualification' && input.locator?.name === 'Next page'
          ? await page.locator('#pager').textContent()
          : undefined
      if (
        plan.family === 'account-onboarding' &&
        input.action === 'fill' &&
        input.locator?.label === 'Finance approval code'
      ) {
        assert.match(
          financeCode ?? '',
          /^[A-F0-9]{6}$/u,
          'Read current approval code from the visible popup',
        )
        input.recordedValue = input.value
        input.value = financeCode
        input.dynamicValueSource = 'Current finance popup #approval-code visible text'
      }
      const frameResolveMs = performance.now() - frameStart
      if (input.dialog) dialogChoice = input.dialog
      if (input.action === 'upload')
        input.files = input.files.map((file) => {
          const allowed = task.allowedUploadFiles.find((path) => basename(path) === basename(file))
          assert.ok(allowed, `Replay upload has no matching allowed file: ${file}`)
          return allowed
        })
      const beforeStats = browser.connectionStats?.()
      const actionStart = performance.now()
      if (input.op === 'switch') {
        await pages.get(input.tab).bringToFront()
        selected = input.tab
      } else if (input.op === 'navigate') throw new Error('Unexpected reference navigation')
      else
        switch (input.action) {
          case 'click':
            await target.click()
            break
          case 'fill':
            await target.fill(String(input.value))
            break
          case 'select':
            await target.selectOption(input.value)
            break
          case 'check':
            await target.check()
            break
          case 'uncheck':
            await target.uncheck()
            break
          case 'upload':
            await target.setInputFiles(input.files)
            break
          case 'popup': {
            const pending = page.waitForEvent('popup', { timeout: 5000 })
            void pending.catch(() => undefined)
            await target.click()
            const popup = await pending
            selected = register(popup)
            await popup.bringToFront()
            if (plan.family === 'account-onboarding')
              financeCode = (await popup.locator('#approval-code').textContent()).trim()
            break
          }
          case 'download': {
            const pending = page.waitForEvent('download', { timeout: 5000 })
            void pending.catch(() => undefined)
            await target.click()
            const download = await pending
            const path = resolve(directory, 'download-' + basename(download.suggestedFilename()))
            await download.saveAs(path)
            const bytes = await readFile(path)
            episode.downloads.push({ path, bytes: bytes.length, sha256: hash(bytes) })
            break
          }
          default:
            throw new Error(`Unhandled replay action: ${input.action}`)
        }
      const controlMs = performance.now() - actionStart
      const readyStart = performance.now()
      if (input.action === 'click') await ready(page, target, documentEpoch, pagerBefore)
      const readinessMs = performance.now() - readyStart
      const afterStats = browser.connectionStats?.()
      episode.actions.push({
        input,
        frameResolveMs,
        controlMs,
        readinessMs,
        elapsedMs: frameResolveMs + controlMs + readinessMs,
        protocol: beforeStats
          ? Object.fromEntries(
              ['sent', 'received', 'sentBytes', 'receivedBytes'].map((key) => [
                key,
                afterStats[key] - beforeStats[key],
              ]),
            )
          : undefined,
        pendingPeak: afterStats?.pendingPeak,
      })
    }
    episode.taskMs = performance.now() - taskStart
    episode.taskStatsEnd = browser.connectionStats?.()
    episode.grade = await task.grade()
    episode.state = await task.state()
    assert.equal(episode.grade.passed, true, JSON.stringify(episode.grade.criteria))
    assert.equal(episode.downloads.length, 1)
    assert.equal(episode.serverDownloads.length, 1)
    assert.equal(episode.downloads[0].bytes, episode.serverDownloads[0].bytes)
    assert.equal(episode.downloads[0].sha256, episode.serverDownloads[0].sha256)
    episode.actualDownloadMatchesServer = true
    episode.beforeClose = browser.connectionStats?.()
    episode.passed = true
  } catch (error) {
    episode.error = { message: error.message, stack: error.stack }
    episode.failure = {
      input: activeAction?.input,
      elapsedTaskMs: taskStartedAt === undefined ? undefined : performance.now() - taskStartedAt,
      elapsedActionMs: activeAction ? performance.now() - activeAction.startedAtMs : undefined,
    }
    // Failure evidence is collected after the timing failure. It never changes actions,
    // retries a click, or contributes successful-task latency samples.
    const failedPage = pages.get(selected)
    if (failedPage && !stopping) {
      try {
        episode.failureDOM = await failedPage.evaluate(() => ({
          url: location.href,
          readyState: document.readyState,
          preview: document.querySelector('#preview')?.textContent ?? null,
          itemCount: document.querySelectorAll('#items fieldset').length,
          itemSelectCount: document.querySelectorAll('#items select').length,
          selects: Array.from(document.querySelectorAll('select')).map((element) => ({
            id: element.id,
            label:
              element.getAttribute('aria-label') ??
              Array.from(element.labels ?? [])
                .map((label) => label.textContent)
                .join(' '),
            value: element.value,
            options: element.options.length,
          })),
          buttons: Array.from(document.querySelectorAll('button')).map((element) => {
            const rect = element.getBoundingClientRect()
            return {
              id: element.id,
              text: element.textContent,
              disabled: element.disabled,
              x: rect.x,
              y: rect.y,
              width: rect.width,
              height: rect.height,
            }
          }),
          bodyText: document.body.innerText.slice(0, 12000),
        }))
        const screenshotPath = resolve(directory, 'failure.png')
        await writeFile(screenshotPath, await failedPage.screenshot())
        episode.failureScreenshot = screenshotPath
      } catch (diagnosticError) {
        episode.failureDiagnosticError = diagnosticError.message
      }
    }
  } finally {
    const closeStart = performance.now()
    try {
      await browser?.close()
      if (backend === 'cef')
        await rm(resolve(directory, 'profile'), { recursive: true, force: true })
      episode.afterClose = browser?.connectionStats?.()
      episode.nativeExit = browser?.source?.processExit?.()
      if (episode.afterClose) {
        assert.equal(episode.afterClose.pending, 0)
        assert.equal(episode.afterClose.eventListeners, 0)
      }
    } catch (error) {
      episode.closeError = error.message
      episode.passed = false
    }
    episode.teardownMs = performance.now() - closeStart
    browser = undefined
    active.delete(id)
    await writeFile(resolve(directory, 'result.json'), JSON.stringify(episode, null, 2) + '\n')
    console.log(
      JSON.stringify({
        episode: id,
        family: plan.family,
        seed: plan.seed,
        passed: episode.passed,
        startupMs: episode.startupMs,
        taskMs: episode.taskMs,
        teardownMs: episode.teardownMs,
        error: episode.error?.message,
      }),
    )
  }
}
try {
  await new Promise((resolveListen) => server.listen(0, '127.0.0.1', resolveListen))
  origin = `http://127.0.0.1:${server.address().port}`
  const run = async () => {
    for (
      let repetition = repetitionOffset;
      repetition < repetitionOffset + repetitions;
      repetition++
    ) {
      const ordered = repetition % 2 ? [...selectedPlans].reverse() : selectedPlans
      for (let index = 0; index < ordered.length; index++) {
        const plan = ordered[index]
        const stableIndex = selectedPlans.indexOf(plan)
        const backends = (stableIndex + repetition) % 2 ? ['cdp', 'cef'] : ['cef', 'cdp']
        for (const backend of backends) {
          if (stopping) return
          await episodeRun(plan, backend, repetition, index)
        }
      }
    }
  }
  workPromise = run()
  await Promise.race([
    workPromise,
    new Promise((_, reject) => {
      deadline = setTimeout(() => {
        stopping = true
        void browser?.close().catch(() => undefined)
        reject(new Error(`Business replay exceeded ${deadlineMs}ms`))
      }, deadlineMs)
    }),
  ])
  report.passed =
    report.episodes.length === selectedPlans.length * repetitions * 2 &&
    report.episodes.every((episode) => episode.passed)
} catch (error) {
  report.error = { message: error.message, stack: error.stack }
} finally {
  clearTimeout(deadline)
  await browser?.close().catch(() => undefined)
  await workPromise?.catch(() => undefined)
  server.closeAllConnections()
  await new Promise((resolveClose) => server.close(resolveClose))
  report.after = await manifest()
  report.inputsUnchanged = JSON.stringify(report.before) === JSON.stringify(report.after)
  if (!report.inputsUnchanged) report.passed = false
  report.durationMs = performance.now() - started
  process.off('SIGTERM', onTerm)
  process.off('SIGINT', onInterrupt)
  if (report.cancelled)
    await writeFile(
      resolve(output, 'CANCELLED.json'),
      JSON.stringify(
        {
          signal: report.cancelled,
          episodes: report.episodes.length,
          status: 'Cancelled; partial evidence only.',
        },
        null,
        2,
      ) + '\n',
    )
  await writeFile(resolve(output, 'results.json'), JSON.stringify(report, null, 2) + '\n')
  if (!report.passed) process.exitCode = 1
  console.log(
    JSON.stringify({
      passed: report.passed,
      episodes: report.episodes.length,
      durationMs: report.durationMs,
      results: resolve(output, 'results.json'),
    }),
  )
}
