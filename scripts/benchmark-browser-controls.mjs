#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument, @typescript-eslint/no-misused-promises -- Benchmarks dynamically selected emitted SDKs against a local application fixture. */
/* global document, window, innerWidth, innerHeight, devicePixelRatio */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { dirname, resolve } from 'node:path'
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
const output = resolve(options.get('--output') ?? '.suitecut/external-native-perf/controls')
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const rounds = Number(options.get('--rounds') ?? 6)
const passes = Number(options.get('--passes') ?? 4)
const singlePass = options.has('--pass') ? Number(options.get('--pass')) : undefined
assert.ok(Number.isInteger(rounds) && rounds >= 1 && rounds <= 100)
assert.ok(Number.isInteger(passes) && passes >= 1 && passes <= 20)
assert.ok(singlePass === undefined || (Number.isInteger(singlePass) && singlePass >= 0))
assert.ok(!options.has('--backend') || ['cef', 'cdp'].includes(options.get('--backend')))
await mkdir(output, { recursive: true })
const sdkManifest = async () => {
  const manifest = {}
  for (const file of (await readdir(dirname(sdkPath), { recursive: true }))
    .filter((file) => file.endsWith('.js'))
    .sort())
    manifest[file] = createHash('sha256')
      .update(await readFile(resolve(dirname(sdkPath), file)))
      .digest('hex')
  return manifest
}
const sdkBefore = options.get('--hash-sdk') === 'true' ? await sdkManifest() : null
const sdk = await import(pathToFileURL(sdkPath).href)
const chromeArgs = [
  ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []),
  ...(options.get('--chrome-software') === 'true'
    ? ['--use-gl=angle', '--use-angle=swiftshader']
    : []),
]
const serverEvents = []
let port
const fixture = (
  mode,
) => `<!doctype html><html><meta charset="utf-8"><title>Matched controls ${mode}</title><style>body{font:16px sans-serif;margin:20px}button,input,select{margin:5px;padding:5px}#board{display:flex;gap:160px;margin:20px}#card,#done{width:120px;height:60px;border:2px solid #768;touch-action:none;user-select:none}iframe{width:650px;height:220px}section{display:contents}</style>
<label>Display name<input id="input-name" placeholder="Your display name"></label><label><input id="agree" type="checkbox">Accept terms</label><label>Plan<select id="plan"><option value="basic">Basic</option><option value="team">Team</option></select></label><button id="inc" data-testid="visible-action">Visible action</button><output id="status">0</output><div id="shadow-host"></div>
<div id="board"><div id="card">Drag this card</div><div id="done">Done lane</div></div><output id="drop-status">Pending</output><div id="dense"></div><div style="height:1300px"></div><button id="bottom">Bottom action</button>
${mode === 'frames' ? `<iframe id="outer" src="http://127.0.0.1:${port}/frame-outer"></iframe>` : ''}
<script>window.clicks=[];window.enterEvents=[];document.querySelector('#inc').onclick=event=>{clicks.push({trusted:event.isTrusted,value:document.querySelector('#input-name').value});document.querySelector('#status').textContent=String(clicks.length)};document.querySelector('#input-name').onkeydown=event=>{if(event.key==='Enter')enterEvents.push({trusted:event.isTrusted,value:event.target.value})};document.querySelector('#shadow-host').attachShadow({mode:'open'}).innerHTML='<button id="shadow-target">Shadow Save</button>';
let held=false,moves=0,trusted=true;window.drops=[];const card=document.querySelector('#card');card.onpointerdown=e=>{held=true;moves=0;trusted=e.isTrusted;card.setPointerCapture(e.pointerId)};card.onpointermove=e=>{if(held){moves++;trusted&&=e.isTrusted}};card.onpointerup=e=>{if(!held)return;held=false;const r=document.querySelector('#done').getBoundingClientRect();if(e.clientX>=r.left&&e.clientX<=r.right&&e.clientY>=r.top&&e.clientY<=r.bottom){window.lastDrop={moves,trusted:trusted&&e.isTrusted};window.drops.push(window.lastDrop);document.querySelector('#drop-status').textContent='Completed'}};
if(${JSON.stringify(mode)}==='dense'){const fragment=document.createDocumentFragment();for(let i=0;i<2500;i++){const section=document.createElement('section');section.innerHTML='<div><span>Row '+i+'</span><div><span>Description '+i+'</span><button>Process order '+i+'</button><label for="row-'+i+'">Account '+i+'</label><input id="row-'+i+'"><div><span>Payload '+i+'</span><span>Metadata '+i+'</span></div></div></div>';fragment.append(section)}document.querySelector('#dense').append(fragment);for(let i=0;i<120;i++){const host=document.createElement('div');document.querySelector('#dense').append(host);host.attachShadow({mode:'open'}).innerHTML='<div><label>Shadow account '+i+'<input></label><button>Shadow action '+i+'</button></div>'}}
</script></html>`
const server = createServer(async (request, response) => {
  const url = new URL(request.url ?? '/', 'http://127.0.0.1')
  response.setHeader('cache-control', 'no-store')
  if (url.pathname === '/frame-submit') {
    const chunks = []
    for await (const chunk of request) chunks.push(chunk.toString())
    serverEvents.push({ event: 'frame-submit', value: chunks.join('') })
    response.end('Recorded')
    return
  }
  response.setHeader('content-type', 'text/html; charset=utf-8')
  if (url.pathname === '/frame-outer') {
    response.end(
      `<!doctype html><iframe id="inner" src="http://localhost:${port}/frame-inner" style="width:600px;height:160px"></iframe>`,
    )
    return
  }
  if (url.pathname === '/frame-inner') {
    response.end(
      `<!doctype html><label>Frame memo<input id="memo"></label><button id="send">Submit frame</button><output id="receipt">Pending</output><script>document.querySelector('#send').onclick=async event=>{const value=document.querySelector('#memo').value;await fetch('/frame-submit',{method:'POST',body:JSON.stringify({value,trusted:event.isTrusted})});document.querySelector('#receipt').textContent=value}</script>`,
    )
    return
  }
  response.end(fixture(url.searchParams.get('mode') ?? 'small'))
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', resolve))
port = server.address().port
const origin = `http://127.0.0.1:${port}`
await writeFile(resolve(output, 'fixture-small.html'), fixture('small'))
await writeFile(resolve(output, 'fixture-dense.html'), fixture('dense'))
const records = []
const summary = (samples) => {
  const sorted = [...samples].sort((left, right) => left - right)
  return {
    count: sorted.length,
    p50Ms: sorted[Math.floor(sorted.length * 0.5)] ?? null,
    p95Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))] ?? null,
    meanMs: sorted.length ? sorted.reduce((sum, value) => sum + value, 0) / sorted.length : null,
    maxMs: sorted.at(-1) ?? null,
  }
}
const waitFor = async (read, expected) => {
  const end = performance.now() + 5000
  let actual
  do {
    actual = await read()
    if (actual === expected) return
    await delay(10)
  } while (performance.now() < end)
  assert.equal(actual, expected)
}
const runStarted = performance.now()
const nodeCPUStarted = process.cpuUsage()
const launches = []
let browser
try {
  for (const pass of singlePass === undefined
    ? Array.from({ length: passes }, (_, index) => index)
    : [singlePass]) {
    const order = pass % 2 === 0 ? ['cef', 'cdp'] : ['cdp', 'cef']
    for (const backend of order.filter(
      (value) => !options.has('--backend') || value === options.get('--backend'),
    )) {
      const launchStart = performance.now()
      browser = await sdk.launchBrowser(
        backend === 'cef'
          ? {
              backend,
              executablePath: resolve(
                options.get('--cef-executable') ?? '.suitecut/memory-deep/runtime/suitecut-browser',
              ),
              headless: true,
              width: 1280,
              height: 720,
              framesPerSecond: 60,
              startupTimeoutMs: 20000,
            }
          : {
              backend,
              executablePath: resolve(
                options.get('--cdp-executable') ??
                  '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
              ),
              args: chromeArgs,
              viewport: { width: 1280, height: 720 },
              timeoutMs: 12000,
            },
      )
      launches.push({ backend, pass, startupMs: performance.now() - launchStart })
      const context = await browser.newContext()
      const page = await context.newPage()
      page.setDefaultTimeout(4000)
      const measure = async (
        name,
        action,
        verify,
        { warmup = false, completedUnits = 1, kind = 'action' } = {},
      ) => {
        const begin = performance.now()
        const cpuBefore = process.cpuUsage()
        let value, actionMs, verificationMs
        try {
          value = await action()
          actionMs = performance.now() - begin
          const verifyStart = performance.now()
          await verify?.(value)
          verificationMs = performance.now() - verifyStart
          const cpu = process.cpuUsage(cpuBefore)
          records.push({
            backend,
            pass,
            name,
            kind,
            warmup,
            status: 'passed',
            actionMs,
            verificationMs,
            elapsedThroughVerifiedCompletionMs: performance.now() - begin,
            completedUnits,
            nodeCPU: { userMs: cpu.user / 1000, systemMs: cpu.system / 1000 },
          })
          return true
        } catch (error) {
          records.push({
            backend,
            pass,
            name,
            kind,
            warmup,
            status: 'failed',
            actionMs: actionMs ?? performance.now() - begin,
            verificationMs: verificationMs ?? null,
            elapsedThroughVerifiedCompletionMs: performance.now() - begin,
            completedUnits: 0,
            error: String(error?.stack ?? error),
          })
          return false
        }
      }
      const repeat = async (name, action, verify, config = {}) => {
        await config.prepare?.(-1)
        await measure(
          name,
          () => action(-1),
          (value) => verify?.(value, -1),
          { ...config, warmup: true },
        )
        for (let index = 0; index < rounds; index++) {
          await config.prepare?.(index)
          await measure(
            name,
            () => action(index),
            (value) => verify?.(value, index),
            config,
          )
        }
      }
      try {
        for (const mode of ['small', 'dense']) {
          await page.goto(`${origin}/?mode=${mode}`)
          const dom = await page.evaluate(() => ({
            lightNodes: document.querySelectorAll('*').length,
            width: innerWidth,
            height: innerHeight,
            devicePixelRatio,
          }))
          assert.deepEqual({ width: dom.width, height: dom.height }, { width: 1280, height: 720 })
          assert.equal(dom.devicePixelRatio, 1)
          const queries = {
            css: () => page.locator('#inc'),
            roleName: () => page.getByRole('button', { name: 'Visible action', exact: true }),
            label: () => page.getByLabel('Display name', { exact: true }),
            text: () => page.getByText('Visible action', { exact: true }),
            testId: () => page.getByTestId('visible-action'),
            placeholder: () => page.getByPlaceholder('Your display name', { exact: true }),
            shadowRole: () => page.getByRole('button', { name: 'Shadow Save', exact: true }),
            shadowCSS: () => page.locator('#shadow-target'),
          }
          for (const [engine, locator] of Object.entries(queries))
            await repeat(
              `locator.${mode}.${engine}`,
              () => locator().count(),
              (value) => assert.equal(value, 1),
            )
          launches.at(-1)[`${mode}DOM`] = dom
        }
        await page.goto(`${origin}/?mode=small`)
        await repeat(
          'input.fillUnicode',
          (index) =>
            page
              .getByLabel('Display name', { exact: true })
              .fill(`Account ${pass}/${index} café 東京 🚀`),
          async (_, index) =>
            assert.equal(
              await page.evaluate(() => document.querySelector('#input-name').value),
              `Account ${pass}/${index} café 東京 🚀`,
            ),
        )
        await repeat(
          'input.check',
          () => page.locator('#agree').check(),
          async () => assert.equal(await page.locator('#agree').isChecked(), true),
          { prepare: () => page.locator('#agree').uncheck() },
        )
        await repeat(
          'input.uncheck',
          () => page.locator('#agree').uncheck(),
          async () => assert.equal(await page.locator('#agree').isChecked(), false),
          { prepare: () => page.locator('#agree').check() },
        )
        await repeat(
          'input.select',
          (index) =>
            page.getByLabel('Plan', { exact: true }).selectOption(index % 2 ? 'basic' : 'team'),
          async (_, index) =>
            assert.equal(
              await page.evaluate(() => document.querySelector('#plan').value),
              index % 2 ? 'basic' : 'team',
            ),
        )
        let expectedEnters = 0
        await repeat(
          'input.keyboardEnter',
          () => page.locator('#input-name').press('Enter'),
          async () => {
            expectedEnters++
            assert.deepEqual(
              await page.evaluate(() => ({
                count: window.enterEvents.length,
                trusted: window.enterEvents.at(-1)?.trusted,
              })),
              { count: expectedEnters, trusted: true },
            )
          },
        )
        let expectedClicks = 0
        await repeat(
          'input.locatorTrustedClick',
          () => page.locator('#inc').click(),
          async () => {
            expectedClicks++
            assert.deepEqual(
              await page.evaluate(() => ({
                count: window.clicks.length,
                trusted: window.clicks.at(-1)?.trusted,
              })),
              { count: expectedClicks, trusted: true },
            )
          },
        )
        const buttonBox = await page.locator('#inc').boundingBox()
        assert.ok(buttonBox)
        await repeat(
          'input.mouseTrustedClick',
          () =>
            page.mouse.click(buttonBox.x + buttonBox.width / 2, buttonBox.y + buttonBox.height / 2),
          async () => {
            expectedClicks++
            assert.deepEqual(
              await page.evaluate(() => ({
                count: window.clicks.length,
                trusted: window.clicks.at(-1)?.trusted,
              })),
              { count: expectedClicks, trusted: true },
            )
          },
        )
        await repeat(
          'evaluate.serializedValue',
          () =>
            page.evaluate(() => ({
              text: 'café 東京 🚀',
              count: 42,
              items: Array.from({ length: 100 }, (_, index) => index * 2),
            })),
          (value) =>
            assert.deepEqual(value, {
              text: 'café 東京 🚀',
              count: 42,
              items: Array.from({ length: 100 }, (_, index) => index * 2),
            }),
        )
        let expectedDrops = 0
        await repeat(
          'pointer.dragCard',
          () => page.locator('#card').dragTo(page.locator('#done'), { steps: 12 }),
          async () => {
            expectedDrops++
            const value = await page.evaluate(() => ({
              ...window.lastDrop,
              count: window.drops.length,
            }))
            assert.equal(value?.trusted, true)
            assert.equal(value.count, expectedDrops)
            assert.ok(value.moves >= 2)
          },
        )
        await repeat(
          'scroll.bottomIntoView',
          () => page.locator('#bottom').scrollIntoViewIfNeeded(),
          async () =>
            assert.equal(
              await page.evaluate(() => {
                const rect = document.querySelector('#bottom').getBoundingClientRect()
                return rect.top >= 0 && rect.bottom <= innerHeight
              }),
              true,
            ),
          { prepare: () => page.locator('#inc').scrollIntoViewIfNeeded() },
        )
        await repeat(
          'screenshot.viewportPNG',
          () => page.screenshot({ type: 'png' }),
          (bytes) => {
            assert.ok(bytes.length > 100)
            assert.equal(bytes.subarray(0, 8).toString('hex'), '89504e470d0a1a0a')
          },
        )
        await repeat(
          'navigation.localDocument',
          (index) => page.goto(`${origin}/?mode=small&round=${index}`),
          async () => assert.equal(await page.locator('#inc').count(), 1),
          { kind: 'workflow' },
        )
        await page.goto(`${origin}/?mode=frames`)
        const frame = page.frameLocator('#outer').frameLocator('#inner')
        await repeat(
          'frame.nestedCrossOriginSubmit',
          async (index) => {
            const text = `Frame memo ${backend}/${pass}/${index} 東京`
            await frame.getByLabel('Frame memo', { exact: true }).fill(text)
            await frame.getByRole('button', { name: 'Submit frame', exact: true }).click()
            await waitFor(() => frame.locator('#receipt').textContent(), text)
            return text
          },
          (text) =>
            assert.ok(
              serverEvents.some((event) => {
                const value = JSON.parse(event.value)
                return value.value === text && value.trusted === true
              }),
            ),
          { kind: 'workflow' },
        )
        await page.goto(`${origin}/?mode=small`)
        await repeat(
          'workflow.oneTabFillSubmit',
          async (index) => {
            const text = `Single tab ${pass}/${index}`
            await page.getByLabel('Display name', { exact: true }).fill(text)
            await page.locator('#inc').click()
            assert.equal(await page.evaluate(() => window.clicks.at(-1)?.value), text)
          },
          undefined,
          { kind: 'workflow' },
        )
        const siblings = await Promise.all(Array.from({ length: 3 }, () => context.newPage()))
        await Promise.all(siblings.map((candidate) => candidate.goto(`${origin}/?mode=small`)))
        await repeat(
          'workflow.threeTabParallelFillSubmit',
          async (index) => {
            await Promise.all(
              siblings.map(async (candidate, tab) => {
                const text = `Concurrent ${pass}/${index}/${tab}`
                await candidate.getByLabel('Display name', { exact: true }).fill(text)
                await candidate.locator('#inc').click()
                assert.equal(await candidate.evaluate(() => window.clicks.at(-1)?.value), text)
                assert.equal(await candidate.evaluate(() => window.clicks.at(-1)?.trusted), true)
              }),
            )
          },
          undefined,
          { kind: 'workflow', completedUnits: 3 },
        )
        await Promise.all(siblings.map((candidate) => candidate.close()))
        await repeat(
          'workflow.contextCookieIsolation',
          async (index) => {
            const first = await browser.newContext(),
              second = await browser.newContext()
            try {
              const name = `isolated_${pass}_${index + 1}`
              await first.addCookies([{ name, value: 'private', domain: '127.0.0.1', path: '/' }])
              assert.ok(
                (await first.cookies()).some(
                  (cookie) => cookie.name === name && cookie.value === 'private',
                ),
              )
              assert.equal(
                (await second.cookies()).some((cookie) => cookie.name === name),
                false,
              )
            } finally {
              await first.close()
              await second.close()
            }
          },
          undefined,
          { kind: 'workflow' },
        )
      } finally {
        const closing = performance.now()
        await browser.close()
        launches.at(-1).closeMs = performance.now() - closing
        browser = undefined
      }
      console.log(
        JSON.stringify({
          backend,
          pass,
          records: records.filter((record) => record.backend === backend && record.pass === pass)
            .length,
          failures: records.filter(
            (record) =>
              record.backend === backend && record.pass === pass && record.status !== 'passed',
          ).length,
        }),
      )
      await writeFile(
        resolve(output, 'progress.json'),
        JSON.stringify({ records, launches }, null, 2) + '\n',
      )
    }
  }
} finally {
  await browser?.close()
  await new Promise((resolve) => server.close(resolve))
  const sdkAfter = sdkBefore ? await sdkManifest() : null
  const groups = []
  for (const backend of ['cef', 'cdp'])
    for (const name of new Set(
      records.filter((record) => record.backend === backend).map((record) => record.name),
    )) {
      const measured = records.filter(
        (record) => record.backend === backend && record.name === name && !record.warmup,
      )
      const passed = measured.filter((record) => record.status === 'passed')
      const wallMs = measured.reduce(
        (sum, record) => sum + record.elapsedThroughVerifiedCompletionMs,
        0,
      )
      groups.push({
        backend,
        name,
        kind: measured[0]?.kind,
        ...summary(passed.map((record) => record.actionMs)),
        verifiedCompletion: summary(
          passed.map((record) => record.elapsedThroughVerifiedCompletionMs),
        ),
        attempted: measured.length,
        errors: measured.length - passed.length,
        completedUnits: passed.reduce((sum, record) => sum + record.completedUnits, 0),
        trueCompletedUnitsPerSecond: wallMs
          ? (passed.reduce((sum, record) => sum + record.completedUnits, 0) * 1000) / wallMs
          : 0,
      })
    }
  const nodeCPU = process.cpuUsage(nodeCPUStarted)
  const report = {
    started: new Date().toISOString(),
    elapsedMs: performance.now() - runStarted,
    sdkPath,
    sdkBefore,
    sdkAfter,
    sdkUnchanged: sdkBefore ? JSON.stringify(sdkBefore) === JSON.stringify(sdkAfter) : null,
    settings: {
      viewport: { width: 1280, height: 720 },
      headless: true,
      cefFramesPerSecond: 60,
      chromeArgs,
      video: false,
      rounds,
      passes,
      singlePass: singlePass ?? null,
      warmupsPerCase: 1,
      balancedOrder:
        'Even passes CEF then CDP; odd passes CDP then CEF. Four passes means four runs per backend (eight browser launches).',
    },
    measurement: {
      action:
        'Public SDK operation duration, excludes subsequent verification; backend completion guarantees differ.',
      primary:
        'Elapsed through independently verified application completion, retaining native stable geometry and navigation guarantees.',
      workflow: 'Includes constituent controls and verified application completion.',
      throughput:
        'Successfully completed units divided by elapsed duration including verification and failed attempts; multi-tab batch completes three units.',
      preparation:
        'Checkbox state and scroll position are reset via public controls outside measured intervals, preventing repeated no-op measurements.',
      nodeCPU:
        'Current Node process CPU only, excludes browser descendants; user/system counters are not browser CPU profiling.',
      warmups: 'Retained in raw records, explicitly discarded from aggregates.',
      sdkIntegrity:
        'Use --hash-sdk true for local before/after hashes; default defers integrity to the parent run manifest.',
      scope:
        'Matched controls and local composition, not an official browser benchmark or complete product feature inventory.',
    },
    nodeCPU: { userMs: nodeCPU.user / 1000, systemMs: nodeCPU.system / 1000 },
    launches,
    groups,
    records,
    serverEvents,
  }
  await writeFile(resolve(output, 'results.json'), JSON.stringify(report, null, 2) + '\n')
  if (report.sdkUnchanged === false || records.some((record) => record.status !== 'passed'))
    process.exitCode = 1
}
