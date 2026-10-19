#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises a selectable emitted SDK against genuine browser pointer events. */
/* global window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2)
  options.set(process.argv[index], process.argv[index + 1])
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const sdk = await import(pathToFileURL(sdkPath).href)
const backend = options.get('--backend') ?? 'cdp'
assert.ok(['cef', 'cdp'].includes(backend))
const executablePath = resolve(
  options.get('--executable') ??
    (backend === 'cef'
      ? '.suitecut/memory-deep/runtime/suitecut-browser'
      : '.suitecut/agent-comparison/competitor/chrome-linux64/chrome'),
)
const output = resolve(options.get('--output') ?? `.suitecut/input-race-review/${backend}`)
await mkdir(output, { recursive: true })
const report = {
  backend,
  executablePath,
  sdkPath,
  passed: false,
  checks: [],
  scope:
    'Deterministic native pointer regression, no application-specific waits, force clicks, completed-click retries or videos.',
}
async function observe(page) {
  const state = await page.evaluate(() => window.state)
  report.lastObservedState = state
  return state
}
const html = `<!doctype html><meta charset=utf-8><title>Input race fixture</title>
<style>button{font:18px sans-serif;width:170px;height:60px}#moving,#trap{position:absolute;left:100px;top:150px}#trap{z-index:1}#moving{z-index:2}#overlay{position:fixed;inset:0;z-index:100;background:#ddd}#overlay button{margin:80px}</style>
<button id=trap>Wrong target</button><button id=moving>Moving target</button>
<script>
const mode=new URL(location.href).searchParams.get('mode');window.state={moves:0,downs:0,targetClicks:0,trapClicks:0,overlayClicks:0,overlayShows:0,trusted:false};
const target=document.querySelector('#moving');document.querySelector('#trap').onclick=()=>state.trapClicks++;
target.onmousedown=event=>{state.downs++;state.trusted=event.isTrusted;if(mode==='remove-on-down')target.remove()};
target.onclick=event=>{state.targetClicks++;state.trusted=event.isTrusted};
target.onmousemove=()=>{state.moves++;if(mode==='shift'&&state.moves===1){target.style.left='550px';target.style.top='380px'}
if(mode==='continuous'){const alternate=state.moves%2;target.style.left=alternate?'550px':'100px';target.style.top=alternate?'380px':'150px'}
if((mode==='overlay'||mode==='overlay-handler'&&state.overlayShows===0)&&!document.querySelector('#overlay')){state.overlayShows++;const overlay=document.createElement('div');overlay.id='overlay';overlay.innerHTML='<button>Dismiss overlay</button>';overlay.onmousedown=()=>state.overlayClicks++;overlay.querySelector('button').onclick=()=>overlay.remove();document.body.append(overlay)}};
</script>`
const server = createServer((_request, response) => {
  response.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
  })
  response.end(html)
})
await new Promise((resolveListen) => server.listen(0, '127.0.0.1', resolveListen))
const origin = `http://127.0.0.1:${server.address().port}`
let browser
const started = performance.now()
try {
  browser = await sdk.launchBrowser(
    backend === 'cef'
      ? {
          backend,
          executablePath,
          headless: true,
          width: 1000,
          height: 700,
          framesPerSecond: 60,
          pixelFormat: 'bgra',
        }
      : {
          backend,
          executablePath,
          headless: true,
          viewport: { width: 1000, height: 700 },
          args: process.getuid?.() === 0 ? ['--no-sandbox', '--disable-dev-shm-usage'] : [],
        },
  )
  report.version = browser.version()
  const context = await browser.newContext()
  const check = async (name, work) => {
    const start = performance.now()
    const evidence = await work()
    report.checks.push({ name, passed: true, elapsedMs: performance.now() - start, evidence })
  }
  await check('hover-triggered target movement uses fresh geometry before down', async () => {
    const page = await context.newPage()
    try {
      await page.goto(`${origin}/?mode=shift`)
      page.setDefaultTimeout(1500)
      await page.getByRole('button', { name: 'Moving target', exact: true }).click()
      const state = await observe(page)
      assert.equal(state.targetClicks, 1)
      assert.equal(state.trapClicks, 0)
      assert.equal(state.downs, 1)
      assert.equal(state.trusted, true)
      assert.ok(state.moves >= 2)
      return state
    } finally {
      await page.close()
    }
  })
  await check('hover overlay blocks down without forcing a covered target', async () => {
    const page = await context.newPage()
    try {
      await page.goto(`${origin}/?mode=overlay`)
      page.setDefaultTimeout(350)
      await assert.rejects(
        page.getByRole('button', { name: 'Moving target', exact: true }).click(),
        /timed out|Timeout|covered/iu,
      )
      const state = await observe(page)
      assert.equal(state.downs, 0)
      assert.equal(state.trapClicks, 0)
      assert.equal(state.overlayClicks, 0)
      page.setDefaultTimeout(1500)
      await page.getByRole('button', { name: 'Dismiss overlay', exact: true }).click()
      return state
    } finally {
      await page.close()
    }
  })
  if (backend === 'cef')
    await check(
      'hover overlay yields input to a registered locator handler before one trusted press',
      async () => {
        const page = await context.newPage()
        const overlay = page.locator('#overlay')
        let handlerCalls = 0
        try {
          await page.goto(`${origin}/?mode=overlay-handler`)
          page.setDefaultTimeout(2000)
          await page.addLocatorHandler(
            overlay,
            async () => {
              handlerCalls++
              await page.getByRole('button', { name: 'Dismiss overlay', exact: true }).click()
            },
            { times: 1 },
          )
          await page.getByRole('button', { name: 'Moving target', exact: true }).click()
          const state = await observe(page)
          assert.equal(handlerCalls, 1)
          assert.equal(state.overlayShows, 1)
          assert.equal(state.overlayClicks, 1)
          assert.equal(state.targetClicks, 1)
          assert.equal(state.downs, 1)
          assert.equal(state.trapClicks, 0)
          assert.equal(state.trusted, true)
          return { handlerCalls, ...state }
        } finally {
          await page.removeLocatorHandler(overlay)
          await page.close()
        }
      },
    )
  await check('continuously moving targets time out before a button is pressed', async () => {
    const page = await context.newPage()
    try {
      await page.goto(`${origin}/?mode=continuous`)
      page.setDefaultTimeout(350)
      await assert.rejects(
        page.getByRole('button', { name: 'Moving target', exact: true }).click(),
        /timed out|Timeout|moved/iu,
      )
      const state = await observe(page)
      assert.equal(state.downs, 0)
      assert.equal(state.trapClicks, 0)
      return state
    } finally {
      await page.close()
    }
  })
  await check('target removal after down never causes a second press', async () => {
    const page = await context.newPage()
    try {
      await page.goto(`${origin}/?mode=remove-on-down`)
      page.setDefaultTimeout(1000)
      // A removed pressed target need not receive click. Its side effect must run once.
      await page
        .getByRole('button', { name: 'Moving target', exact: true })
        .click()
        .catch(() => undefined)
      const state = await observe(page)
      assert.equal(state.downs, 1)
      assert.equal(state.trapClicks, 0)
      return state
    } finally {
      await page.close()
    }
  })
  await check('absent target wait permits another page input and cancels on close', async () => {
    const page = await context.newPage()
    const other = await context.newPage()
    try {
      await page.goto(origin)
      await other.goto(origin)
      page.setDefaultTimeout(5000)
      other.setDefaultTimeout(1000)
      const rejected = assert.rejects(
        page.getByRole('button', { name: 'Never exists', exact: true }).click(),
        /closed|cancel|abort/iu,
      )
      await other.getByRole('button', { name: 'Moving target', exact: true }).click()
      assert.equal((await other.evaluate(() => window.state)).targetClicks, 1)
      await page.close()
      await rejected
      return { otherPageClicked: true, closedWaitRejected: true }
    } finally {
      await page.close()
      await other.close()
    }
  })
  await context.close()
  report.passed = true
} catch (error) {
  report.error = { message: error.message, stack: error.stack }
  process.exitCode = 1
} finally {
  try {
    await browser?.close()
  } catch (error) {
    report.closeError = error.message
    report.passed = false
    process.exitCode = 1
  }
  report.afterClose = browser?.connectionStats?.()
  report.nativeExit = browser?.source?.processExit?.()
  report.durationMs = performance.now() - started
  server.closeAllConnections()
  await new Promise((resolveClose) => server.close(resolveClose))
  await writeFile(resolve(output, 'results.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(
    JSON.stringify({
      passed: report.passed,
      checks: report.checks.length,
      backend,
      error: report.error?.message,
      output,
    }),
  )
}
