// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-aria-ax')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  width: 640,
  height: 480,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
const State = z.record(z.string(), z.union([z.boolean(), z.number(), z.literal('mixed')]))
const NodeBase = z.object({
  role: z.string(),
  name: z.string().optional(),
  text: z.string().optional(),
  states: State.optional(),
  properties: z.record(z.string(), z.string()).optional(),
  children: z.array(z.unknown()).optional(),
})
/** Validate every actual native node with bounded iterative traversal. */
function forest(value) {
  const roots = z.array(NodeBase).parse(value),
    pending = [...roots]
  let count = 0
  while (pending.length) {
    const node = pending.pop()
    assert(node)
    assert(++count <= 4096)
    pending.push(...z.array(NodeBase).parse(node.children ?? []))
  }
  return roots
}
const checks = []
try {
  const page = createNativePage(source)
  /**
   * Capture the actual Chromium AX forest rooted at a native strict DOM query.
   * @param {string} selector
   * @param {import('../src/native-query.js').NativeQueryStep[][]} [framePath]
   */
  const capture = async (selector, framePath = []) => {
    const response = z.object({ result: z.object({ value: z.unknown() }) }).parse(
      await source.runAutomation('queryEvaluate', {
        query: [{ engine: 'css', selector }],
        framePath,
        function: 'function(nodes,metadata,forest){return forest}',
        all: true,
        dom: true,
        accessibilitySnapshot: true,
        timeoutMs: 3000,
      }),
    )
    return forest(response.result.value)
  }
  await page.setContent(`
    <ul id=notifications><li>Database <span>initialised.</span></li></ul>
    <h2 id=heading>Native <span>tree</span></h2>
    <label for=name>User name</label><input id=name value="current value">
    <input id=check type=checkbox disabled><button id=toggle aria-pressed=mixed>Toggle</button>
    <a id=link href="../raw?x=1#spot">Native link</a>
    <div id=hidden aria-hidden=true><button>Invisible</button></div>
    <div id=shadow><span slot=caption>Slotted once</span></div>
  `)
  await page.evaluate(() => {
    const checkbox = document.querySelector('#check')
    if (!(checkbox instanceof Element)) throw new Error('Expected a checkbox')
    Reflect.set(checkbox, 'indeterminate', true)
    document
      .querySelector('#shadow')
      ?.attachShadow({ mode: 'open' })
      .append(document.createElement('slot'))
    const slot = document.querySelector('#shadow')?.shadowRoot?.querySelector('slot')
    slot?.setAttribute('name', 'caption')
  })
  const list = await capture('#notifications')
  assert.equal(list.length, 1)
  assert.equal(list[0]?.role, 'list')
  const item = NodeBase.parse(list[0]?.children?.[0])
  assert.equal(item.role, 'listitem')
  assert.equal(NodeBase.parse(item.children?.[0]).text, 'Database initialised.')
  checks.push(
    'actual AX list and listitem roles preserve adjacent real browser text through ignored containers',
  )
  const heading = await capture('#heading')
  assert.equal(heading[0]?.role, 'heading')
  assert.equal(heading[0]?.name, 'Native tree')
  assert.equal(heading[0]?.states?.level, 2)
  checks.push('the actual selected heading is included with Chromium name and heading level')
  const textbox = await capture('#name')
  assert.equal(textbox[0]?.role, 'textbox')
  assert.equal(textbox[0]?.name, 'User name')
  assert.equal(textbox[0]?.text, 'current value')
  const checkbox = await capture('#check')
  assert.equal(checkbox[0]?.states?.checked, 'mixed')
  assert.equal(checkbox[0]?.states?.disabled, true)
  assert.equal((await capture('#toggle'))[0]?.states?.pressed, 'mixed')
  checks.push(
    'actual AX form names values and mixed or disabled states are projected without HTML role inference',
  )
  assert.equal((await capture('#link'))[0]?.properties?.url, '../raw?x=1#spot')
  checks.push('link URL is the actual raw href read through its native backend node identity')
  assert.deepEqual(await capture('#hidden'), [])
  const shadow = await capture('#shadow')
  assert.equal(shadow.length, 1)
  assert.equal(shadow[0]?.text, 'Slotted once')
  checks.push('actual hidden AX scopes stay empty and composed slotted text appears once')
  await assert.rejects(capture('input'), /strict mode violation/u)
  const canceled = new AbortController()
  const waiting = source.runAutomation(
    'queryEvaluate',
    {
      query: [{ engine: 'css', selector: '#missing' }],
      function: 'function(node,metadata,forest){return forest}',
      dom: true,
      accessibilitySnapshot: true,
      timeoutMs: 3000,
    },
    canceled.signal,
  )
  canceled.abort(new Error('Authored AX cancellation'))
  await assert.rejects(waiting, /Authored AX cancellation/u)
  assert.equal((await capture('#heading'))[0]?.name, 'Native tree')
  checks.push('native scope strictness and cancellation preserve subsequent authentic captures')
  await page.setContent('<iframe srcdoc="<h1 id=child>Child accessibility</h1>"></iframe>')
  const child = await capture('#child', [[{ engine: 'css', selector: 'iframe' }]])
  assert.equal(child[0]?.name, 'Child accessibility')
  assert.equal(child[0]?.states?.level, 1)
  checks.push('native frame-path scope captures the real child-frame Chromium AX tree')
  await page.goto('data:text/html,<h1 id=fresh>Fresh accessibility</h1>')
  assert.equal((await capture('#fresh'))[0]?.name, 'Fresh accessibility')
  checks.push('navigation uses a fresh native frame epoch and Chromium AX identity')
  await page.setContent(`<ul id=large>${'<li>x</li>'.repeat(4200)}</ul>`)
  await assert.rejects(capture('#large'), /snapshot node limit/u)
  await page.setContent('<p id=recovered>Recovered</p>')
  assert((await capture('#recovered')).length > 0)
  checks.push(
    'bounded native projection rejects oversized scopes and recovers with no retained capture',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      pendingAXCaptures: z.number(),
      pendingAXPhysicalCommands: z.number(),
      pendingAXCaptureBytes: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const value of Object.values(state)) assert.equal(value, 0)
  const report = {
    cefVersion: source.cefVersion,
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
