import assert from 'node:assert/strict'
import { getEventListeners } from 'node:events'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout } from 'node:timers'

import { record } from '../dist/playwright.js'
import { renderSuiteCut } from '../dist/render.js'

const directory = await mkdtemp(join(tmpdir(), 'suitecut-page-lifecycle-'))
const manifestPath = join(directory, 'manifest.json')
const outputPath = join(directory, 'popup.mp4')
const quickManifestPath = join(directory, 'quick-popups.json')
const quickOutputPath = join(directory, 'quick-popups.mp4')
const abortedManifestPath = join(directory, 'aborted.json')

try {
  const result = await record(
    'popup lifecycle',
    async ({ page, suitecut }) => {
      await page.setContent('<a target="_blank" href="about:blank">Open popup</a>')
      const [popup] = await Promise.all([
        page.waitForEvent('popup'),
        page.getByRole('link', { name: 'Open popup' }).click(),
      ])
      await popup.setContent('<h1>Popup page</h1>')
      suitecut.selectPage(popup)
      await suitecut.hold(250)
      await popup.close()
      await suitecut.hold(250)
    },
    {
      capture: {
        viewport: { width: 640, height: 360 },
        framesPerSecond: 30,
        quality: 80,
      },
      output: { directory, manifestPath, pathKind: 'absolute' },
    },
  )

  assert.equal(result.manifest.schemaVersion, 1)
  const attempt = result.manifest.tests[0]?.attempts[0]
  assert(attempt !== undefined, 'Popup lifecycle recording has no attempt')
  const mainPage = attempt.pages.find((page) => page.kind === 'main')
  const popupPage = attempt.pages.find((page) => page.kind === 'popup')
  assert(mainPage !== undefined, 'Popup lifecycle recording has no main page')
  assert(popupPage !== undefined, 'Popup lifecycle recording has no popup page')
  const selections = attempt.events.filter((event) => event.type === 'page-selected')
  assert(
    selections.some((event) => event.pageId === popupPage.id && event.reason === 'opened'),
    'Popup opening did not persist a page-selection event',
  )
  assert(
    selections.some((event) => event.pageId === mainPage.id && event.reason === 'closed'),
    'Popup closing did not persist the opener selection',
  )

  const report = await renderSuiteCut({
    manifestPath,
    outputPath,
    config: {
      narrationEnabled: false,
      resultHoldMs: 250,
      output: { width: 320, height: 180, framesPerSecond: 30, quality: 'standard' },
    },
  })
  assert.equal(report.status, 'rendered')
  assert.deepEqual(
    report.edits.map((edit) => edit.pageId),
    [mainPage.id, popupPage.id, mainPage.id, mainPage.id],
  )
  const persistedReport = await readFile(`${outputPath}.suitecut.json`, 'utf8')
  assert(!persistedReport.includes(directory), 'Render report leaked its temporary project path')
  assert(
    persistedReport.includes('<input-1>'),
    'Render report omitted redacted FFmpeg input evidence',
  )

  const quickResult = await record(
    'quick popup lifecycle',
    async ({ page }) => {
      await page.setContent('<a target="_blank" href="about:blank">Open quick popup</a>')
      for (const delayMs of [0, 1, 10, 50]) {
        const [popup] = await Promise.all([
          page.waitForEvent('popup'),
          page.getByRole('link', { name: 'Open quick popup' }).click(),
        ])
        if (delayMs > 0) await popup.waitForTimeout(delayMs)
        await popup.close()
        assert.equal(
          getEventListeners(popup, 'domcontentloaded').length,
          0,
          `Closed popup retained its DOM listener after a ${String(delayMs)}ms lifetime`,
        )
        assert.equal(
          getEventListeners(popup, 'load').length,
          0,
          `Closed popup retained its load listener after a ${String(delayMs)}ms lifetime`,
        )
      }
    },
    {
      capture: {
        viewport: { width: 640, height: 360 },
        framesPerSecond: 30,
        quality: 80,
      },
      output: { directory, manifestPath: quickManifestPath, pathKind: 'absolute' },
    },
  )
  const quickAttempt = quickResult.manifest.tests[0]?.attempts[0]
  assert(quickAttempt !== undefined, 'Quick popup lifecycle recording has no attempt')
  const recordedPageIds = new Set(quickAttempt.pages.map((page) => page.id))
  assert(
    quickAttempt.events.every((event) => recordedPageIds.has(event.pageId)),
    'Quick popup lifecycle retained an event for an uncaptured page',
  )
  const quickReport = await renderSuiteCut({
    manifestPath: quickManifestPath,
    outputPath: quickOutputPath,
    config: {
      narrationEnabled: false,
      resultHoldMs: 100,
      output: { width: 320, height: 180, framesPerSecond: 30, quality: 'standard' },
    },
  })
  assert.equal(quickReport.status, 'rendered')

  const controller = new AbortController()
  const abortStartedAt = performance.now()
  await assert.rejects(
    record(
      'aborted recording',
      async ({ page }) => {
        setTimeout(() => controller.abort(), 50)
        await page.waitForTimeout(30_000)
      },
      {
        signal: controller.signal,
        capture: { viewport: { width: 640, height: 360 }, framesPerSecond: 30, quality: 80 },
        output: { directory, manifestPath: abortedManifestPath, pathKind: 'absolute' },
      },
    ),
    (error) => error instanceof Error && error.name === 'AbortError',
  )
  assert(
    performance.now() - abortStartedAt < 10_000,
    'Aborted recording did not finish cleanup within the ten-second shutdown bound',
  )

  process.stdout.write(
    'Verified settled, quick, and aborted lifecycles, listener disposal, rendering, and report redaction.\n',
  )
} finally {
  await rm(directory, { recursive: true, force: true })
}
