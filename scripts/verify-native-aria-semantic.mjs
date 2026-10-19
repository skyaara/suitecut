// SPDX-License-Identifier: MIT
/* global document, HTMLIFrameElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { formatNativeAriaTree } from '../dist/native-aria.js'
import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-aria-semantic')
await mkdir(directory, { recursive: true })
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
const Node = z.object({
  role: z.string(),
  name: z.string().optional(),
  text: z.string().optional(),
  box: z.tuple([z.number(), z.number(), z.number(), z.number()]).optional(),
  states: z
    .record(
      z.string(),
      z.union([
        z.boolean(),
        z.number(),
        z.literal('mixed'),
        z.literal('grammar'),
        z.literal('spelling'),
      ]),
    )
    .optional(),
  properties: z.record(z.string(), z.string()).optional(),
  children: z.array(z.unknown()).optional(),
})
const checks = []
try {
  const page = createNativePage(source)
  const capture = async (selector = 'body', semantic = true, boxes = false) => {
    const response = z.object({ result: z.object({ value: z.string() }) }).parse(
      await source.runAutomation('queryEvaluate', {
        query: [{ engine: 'css', selector }],
        all: true,
        dom: true,
        accessibilitySnapshot: true,
        accessibilitySemantics: semantic,
        accessibilityBoxes: boxes,
        function: 'function(nodes,metadata,forest){return JSON.stringify(forest)}',
        timeoutMs: 3000,
      }),
    )
    const forest = z.array(Node).parse(JSON.parse(response.result.value))
    const pending = [...forest]
    let count = 0
    while (pending.length) {
      const node = pending.pop()
      assert(node)
      assert(++count <= 4096)
      pending.push(...z.array(Node).parse(node.children ?? []))
    }
    return forest
  }
  /**
   * Verify an actual composed document snapshot.
   * @param {string} html
   * @param {string} expected
   */
  const check = async (html, expected) => {
    await page.setContent(html)
    const forest = await capture()
    assert.equal(formatNativeAriaTree(forest), expected)
    return forest
  }
  await check(
    '<style>span:before{content:"world";display:block}div:after{content:"bye";display:block}</style><a href=about:blank><span>hello</span><div>hello</div></a>',
    '- link "world hello hello bye":\n  - /url: about:blank',
  )
  checks.push('genuine CSS pseudo block spacing agrees with composed accessible name')
  await check(
    '<div role=listitem><div><span title=a.test.ts>a.test.ts</span></div><div><button title=Run></button><button title="Show source"></button></div></div><div role=listitem><div><span>snapshot</span></div><div>30ms</div></div>',
    '- listitem:\n  - text: a.test.ts\n  - button "Run"\n  - button "Show source"\n- listitem: snapshot 30ms',
  )
  checks.push('real DOM listitem roles and inline/block text retained')
  await check(
    '<a href=about:blank aria-owns="input p"><div role=region>Link 1</div></a><a href=about:blank aria-owns="input p"><div role=region>Link 2</div></a><input id=input value=Value><p id=p>Paragraph</p>',
    '- link "Link 1 Value Paragraph":\n  - /url: about:blank\n  - region: Link 1\n  - textbox: Value\n  - paragraph: Paragraph\n- link "Link 2 Value Paragraph":\n  - /url: about:blank\n  - region: Link 2',
  )
  checks.push('first legal aria-owns tree ownership preserves actual nonowned names')
  await check(
    '<a href=about:blank id=parent><div role=region aria-owns=parent>Hello</div></a>',
    '- link "Hello":\n  - /url: about:blank\n  - region: Hello',
  )
  checks.push('ancestor ownership cycles excluded from actual composed forest')
  const table = await check(
    '<table><tr aria-selected=true><td>Row</td></tr><tr><td>Row 2</td></tr></table>',
    '- table:\n  - rowgroup:\n    - row "Row" [selected]:\n      - cell "Row"\n    - row "Row 2":\n      - cell "Row 2"',
  )
  assert.equal(Node.parse(Node.parse(table[0]?.children?.[0]).children?.[0]).states?.selected, true)
  checks.push('HTML table semantic roles and actual selected row state')
  const long = 'a'.repeat(10000)
  await page.setContent(`<a href=about:blank><div role=region>${long}</div></a>`)
  const lengthy = await capture()
  assert.equal(lengthy[0]?.name, long)
  assert.equal(Node.parse(lengthy[0]?.children?.[0]).role, 'region')
  assert.equal(Node.parse(Node.parse(lengthy[0]?.children?.[0]).children?.[0]).text, long)
  checks.push('complete real long name and region text survive bounded native transport')
  await page.setContent(
    '<input id=invalid aria-label=Label placeholder=Placeholder aria-invalid=grammar>',
  )
  assert.equal((await capture('#invalid'))[0]?.states?.invalid, 'grammar')
  assert.equal((await capture('#invalid', false))[0]?.states?.invalid, true)
  assert.equal((await capture('#invalid'))[0]?.properties?.placeholder, 'Placeholder')
  checks.push('separate raw Chromium invalid boolean and authored genuine DOM invalid subtype')
  await page.setContent('<h1 id=name></h1><p id=text></p>')
  await page.evaluate(() => {
    const name = document.querySelector('#name'),
      text = document.querySelector('#text')
    if (!name || !text) throw new Error('Missing Unicode fixture')
    name.textContent = '\ud800'
    text.textContent = '\udfff'
  })
  const unicode = await capture()
  assert.equal(unicode[0]?.name, '\ud800')
  assert.equal(Node.parse(unicode[1]?.children?.[0]).text, '\udfff')
  checks.push('isolated UTF16 name and text preserve exact actual code units')
  await page.setContent(
    '<div style="visibility:hidden"><div style="visibility:visible"><button>Hidden</button></div></div><button id=box style="position:absolute;left:100px;top:50px;width:80px;height:40px">click</button>',
  )
  assert.deepEqual((await capture('#box', true, true))[0]?.box, [100, 50, 80, 40])
  assert.equal((await capture()).length, 1)
  checks.push('genuine CSS ancestor visibility and native utility geometry')
  await page.setContent(
    '<div id=target style="position:absolute;left:100px;top:50px"><span>Hello</span>\n</div>',
  )
  const genuineTextBox = await page.evaluate(() => {
    const target = document.querySelector('#target span')
    if (!target) throw new Error('Missing actual text geometry fixture')
    const range = document.createRange()
    range.selectNodeContents(target)
    const rect = range.getBoundingClientRect()
    return [rect.x, rect.y, rect.width, rect.height]
  })
  assert.deepEqual((await capture('#target', true, true))[0]?.box, genuineTextBox)
  checks.push('zero-layout whitespace cannot enlarge genuine visible text geometry')
  await page.setContent(
    '<input id=value value="  hello   &nbsp; world "><input id=file type=file><input id=multiple type=file multiple><input id=disabled type=file disabled><input id=hidden type=file hidden>',
  )
  assert.equal((await capture('#value'))[0]?.text, 'hello world')
  assert.equal(await page.locator('#value').inputValue(), '  hello   \u00a0 world ')
  for (const selector of ['#file', '#multiple', '#disabled']) {
    const raw = await capture(selector, false)
    const semantic = await capture(selector)
    assert.equal(semantic[0]?.role, raw[0]?.role)
    assert.equal(semantic[0]?.name, raw[0]?.name)
    assert.equal(semantic[0]?.states?.disabled, raw[0]?.states?.disabled)
  }
  assert.deepEqual(await capture('#hidden'), [])
  checks.push(
    'actual browser UA single/multiple/disabled file metadata and normalized snapshot value preserve native inputValue',
  )
  await page.setContent(
    '<a id=owner href=# aria-owns=outside>Upload</a><input id=outside type=file>',
  )
  const rawOwned = await capture('#outside', false)
  const owner = await capture('#owner')
  const ownedButton = z
    .array(Node)
    .parse(owner[0]?.children)
    .find((node) => node.role === 'button')
  assert.equal(ownedButton?.name, rawOwned[0]?.name)
  assert.equal(owner[0]?.name, `Upload ${z.string().parse(rawOwned[0]?.name)}`)
  checks.push(
    'native UA collector follows exact scoped aria-owns graph to genuine outside-subtree file control',
  )
  await page.setContent('<iframe id=child></iframe>')
  await page.evaluate(() => {
    const frame = document.querySelector('#child')
    if (!(frame instanceof HTMLIFrameElement) || !frame.contentDocument)
      throw new Error('Missing genuine child document')
    frame.contentDocument.body.innerHTML = '<h2>Child snapshot</h2>'
  })
  assert.equal(
    await page.frameLocator('#child').locator('h2').ariaSnapshot(),
    '- heading "Child snapshot" [level=2]',
  )
  checks.push('public semantic snapshot resolves genuine child frame node and native frame epoch')
  await page.setContent('<button>A</button><button>B</button>')
  await assert.rejects(capture('button'), /strict mode violation/u)
  assert.deepEqual(await capture('#missing'), [])
  await assert.rejects(
    source.runAutomation('queryEvaluate', {
      query: [{ engine: 'css', selector: 'body' }],
      all: true,
      dom: true,
      function: 'function(){return true}',
      nativeSemanticSnapshotJson: '[]',
    }),
    /Invalid|native|private/iu,
  )
  checks.push('genuine strict query, empty query, and private forest injection protection')
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      pendingAXCaptures: z.number(),
      pendingAXPhysicalCommands: z.number(),
      pendingAXCaptureBytes: z.number(),
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
