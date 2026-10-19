// SPDX-License-Identifier: MIT
/* global document, Element, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  NativeLocator,
  NativeSelectors,
  launchNativeBrowser,
} from '../dist/native.js'

const directory = resolve('.suitecut/native-queries')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const helperSource = await readFile('native/shared/dom_query.h', 'utf8')
const helper = z.string().parse(/R"JS\(([\s\S]*)\)JS"/u.exec(helperSource)?.[1])
const mode = process.env.SUITECUT_QUERY_HELPER_SOURCE === '1' ? 'source-helper' : 'native-query'
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
try {
  const page = createNativePage(source)
  await page.setContent(`
    <button id="save" aria-label="Store document">Save <img alt="document icon"></button>
    <button id="content">Read <img alt="document"></button>
    <button id="hidden" hidden>Invisible</button>
    <button id="reference" aria-labelledby="hidden-label"></button>
    <span id="hidden-label" hidden>Hidden name</span>
    <fieldset disabled><legend><button id="legend">Legend</button></legend><button id="disabled">Disabled</button></fieldset>
    <input id="checkbox" type="checkbox" checked><input id="mixed" type="checkbox">
    <div id="pressed" role="button" aria-pressed="mixed">Toggle</div>
    <h3 id="heading">Heading</h3>
    <label>Account<input id="account" value="not a label"></label>
    <section><div id="first"><span>Alpha</span></div><div id="second"><span>Beta</span></div></section>
    <div id="shadow"></div>
    <div id="unslotted"><button id="shadow-hidden">Hidden by shadow</button></div>
    <span id="outside">outside</span>
    <div id="#">Escaped identifier</div>
  `)
  await page.evaluate(() => {
    document.querySelector('#mixed').indeterminate = true
    document.querySelector('#shadow').innerHTML = '<span slot="caption">Slotted caption</span>'
    document.querySelector('#shadow').attachShadow({ mode: 'open' }).innerHTML =
      '<button id="shadow-button"><slot name="caption"></slot></button>'
    document.querySelector('#unslotted').attachShadow({ mode: 'open' }).innerHTML = 'replacement'
  })
  const results = []
  /**
   * Check the actual native DOM results for one authored query.
   * @param {string} label
   * @param {import('../src/native-query.js').NativeQueryStep[]} steps
   * @param {string[]} expected
   */
  const query = async (label, steps, expected) => {
    const actual =
      mode === 'source-helper'
        ? z
            .array(z.string())
            .parse(
              await page.evaluate(`(${helper})(${JSON.stringify(steps)}).map(element=>element.id)`),
            )
        : z
            .object({ result: z.object({ value: z.object({ actual: z.array(z.string()) }) }) })
            .parse(
              await new NativeLocator(page, label, steps).runExpectation({
                function:
                  'function(nodes){return {matches:true,actual:nodes.map(element=>element.id)}}',
                dom: true,
              }),
            ).result.value.actual
    assert.deepEqual(actual, expected, label)
    results.push(label)
  }
  await query(
    'accessible explicit label',
    [{ engine: 'role', role: 'button', name: { text: 'Store document', exact: true } }],
    ['save'],
  )
  await query(
    'accessible image content',
    [{ engine: 'role', role: 'button', name: { text: 'Read document', exact: true } }],
    ['content'],
  )
  await query(
    'hidden accessible reference',
    [{ engine: 'role', role: 'button', name: { text: 'Hidden name', exact: true } }],
    ['reference'],
  )
  await query(
    'default hidden exclusion',
    [{ engine: 'role', role: 'button', name: { text: 'Invisible' } }],
    [],
  )
  await query(
    'include hidden',
    [{ engine: 'role', role: 'button', includeHidden: true, name: { text: 'Invisible' } }],
    ['hidden'],
  )
  await query(
    'disabled fieldset',
    [{ engine: 'role', role: 'button', disabled: true }],
    ['disabled'],
  )
  await query(
    'raw role whitespace including newline',
    [{ engine: 'css', selector: 'role=button[name=" \n Store document "]' }],
    ['save'],
  )
  await query('checked state', [{ engine: 'role', role: 'checkbox', checked: true }], ['checkbox'])
  await query('mixed state', [{ engine: 'role', role: 'checkbox', checked: 'mixed' }], ['mixed'])
  await query('pressed state', [{ engine: 'role', role: 'button', pressed: 'mixed' }], ['pressed'])
  await query('heading level', [{ engine: 'role', role: 'heading', level: 3 }], ['heading'])
  await query(
    'label excludes embedded control',
    [{ engine: 'label', text: 'Account', exact: true }],
    ['account'],
  )
  await query(
    'slot name included once',
    [{ engine: 'role', role: 'button', name: { text: 'Slotted caption', exact: true } }],
    ['shadow-button'],
  )
  await query(
    'unslotted role excluded',
    [{ engine: 'role', role: 'button', name: { text: 'Hidden by shadow' } }],
    [],
  )
  await query(
    'filter text',
    [
      { engine: 'css', selector: 'section div' },
      { engine: 'filter', hasText: { text: 'Alpha' } },
    ],
    ['first'],
  )
  await query(
    'filter has',
    [
      { engine: 'css', selector: 'section div' },
      { engine: 'filter', has: [{ engine: 'text', text: 'Beta' }] },
    ],
    ['second'],
  )
  await query(
    'filter hasNot',
    [
      { engine: 'css', selector: 'section div' },
      { engine: 'filter', hasNot: [{ engine: 'text', text: 'Beta' }] },
    ],
    ['first'],
  )
  await query(
    'intersection',
    [
      { engine: 'css', selector: 'section div' },
      { engine: 'and', query: [{ engine: 'css', selector: '#first' }] },
    ],
    ['first'],
  )
  await query(
    'union document order',
    [
      { engine: 'css', selector: '#second' },
      { engine: 'or', query: [{ engine: 'css', selector: '#first' }] },
    ],
    ['first', 'second'],
  )
  await query(
    'relative composite scope',
    [
      { engine: 'css', selector: 'section' },
      {
        engine: 'chain',
        query: [
          { engine: 'css', selector: '#first' },
          { engine: 'or', query: [{ engine: 'css', selector: '#outside' }] },
        ],
      },
    ],
    ['first'],
  )
  await query(
    'raw role selector',
    [{ engine: 'css', selector: 'role=checkbox[checked=mixed]' }],
    ['mixed'],
  )
  await query(
    'raw selector chain',
    [{ engine: 'css', selector: 'section >> div >> nth=1' }],
    ['second'],
  )
  await query(
    'relative XPath',
    [
      { engine: 'css', selector: 'section' },
      { engine: 'css', selector: 'xpath=./div' },
    ],
    ['first', 'second'],
  )
  await query(
    'CSS visibility pseudo',
    [{ engine: 'css', selector: 'button:visible' }],
    ['save', 'content', 'reference', 'legend', 'disabled', 'shadow-button'],
  )
  await query(
    'CSS text pseudo',
    [{ engine: 'css', selector: 'section div:has-text("Alpha")' }],
    ['first'],
  )
  await query(
    'CSS exact text pseudo',
    [{ engine: 'css', selector: 'section div:text-is("Alpha")' }],
    [],
  )
  await query(
    'CSS text regular expression',
    [{ engine: 'css', selector: 'section div:text-matches("beta", "i")' }],
    [],
  )
  await query(
    'CSS nested negation',
    [{ engine: 'css', selector: 'section div:not(:has-text("Alpha"))' }],
    ['second'],
  )
  await query(
    'CSS relative has shadow',
    [{ engine: 'css', selector: '#shadow:has(> button)' }],
    ['shadow'],
  )
  await query(
    'CSS relative sibling has',
    [{ engine: 'css', selector: '#first:has(+ #second)' }],
    ['first'],
  )
  await query(
    'CSS nth match',
    [{ engine: 'css', selector: ':nth-match(section div, 2)' }],
    ['second'],
  )
  await query(
    'capture ancestor selector',
    [{ engine: 'css', selector: '*css=section >> span' }],
    [''],
  )
  await query(
    'multiple test id attributes',
    [{ engine: 'testId', attribute: 'id,data-testid', text: 'first' }],
    ['first'],
  )
  await query('escaped CSS identifier remains valid', [{ engine: 'css', selector: '#\\#' }], ['#'])
  if (mode === 'native-query') {
    for (const [selector, diagnostic] of [
      ['row="row"', 'Unknown engine "row" while parsing selector row="row"'],
      ['row]', 'Unexpected token "]" while parsing css selector "row]"'],
      [
        '##',
        'Unexpected token "#" while parsing css selector "##". Did you mean to CSS.escape it?',
      ],
    ]) {
      await assert.rejects(
        page.locator(selector).count(),
        (error) => error instanceof Error && error.message.includes(diagnostic),
      )
      results.push(`native grammar diagnostic: ${selector}`)
    }
    assert.equal(await page.locator('section div').filter({ hasText: 'Alpha' }).count(), 1)
    assert.equal(await page.getByRole('checkbox', { checked: 'mixed' }).count(), 1)
    assert.equal(await page.locator('#second').or(page.locator('#first')).count(), 2)
    results.push('SDK query composition')
  }
  if (mode === 'native-query') {
    const selectors = new NativeSelectors(source)
    await page.evaluate(() => {
      window.nativeSelectorMarker = true
    })
    await selectors.register('nativeSmoke', () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (!window.nativeSelectorMarker)
          throw new Error('Selector did not execute in the main world')
        return Array.from(root.querySelectorAll(selector))
      },
    }))
    assert.equal(await page.locator('nativeSmoke=#save').count(), 1)
    assert.equal(await page.locator('nativeSmoke=section').locator('nativeSmoke=div').count(), 2)
    await assert.rejects(
      selectors.register('nativeSmoke', '({query(){},queryAll(){return []}})'),
      /already registered/u,
    )
    await selectors.register(
      'isolatedSmoke',
      () => ({
        query(root, selector) {
          return root.querySelector(selector)
        },
        queryAll(root, selector) {
          if (window.nativeSelectorMarker !== undefined)
            throw new Error('Isolated selector observed main-world state')
          return root.querySelectorAll(selector)
        },
      }),
      { contentScript: true },
    )
    assert.equal(await page.locator('isolatedSmoke=#save').count(), 1)
    assert.equal(
      await new NativeLocator(page, '<direct custom>', [
        { engine: 'custom', name: 'nativeSmoke', selector: '#save' },
      ]).count(),
      1,
    )
    await selectors.register('__proto__', () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (!window.nativeSelectorMarker)
          throw new Error('Prototype-named engine used the wrong world')
        return root.querySelectorAll(selector)
      },
    }))
    assert.equal(await page.locator('__proto__=#save').count(), 1)
    results.push('direct custom stage selects the main world')
    results.push('prototype-named engine is an own registry entry')
    results.push('native registered custom selector')
    results.push('registered selector main world')
    results.push('duplicate selector controlled error')
    results.push('registered isolated selector excludes page JavaScript')
  }
  if (mode === 'native-query') {
    await page.setContent('<iframe srcdoc="<button id=child>Child</button>"></iframe>')
    const locator = page.frameLocator('nativeSmoke=iframe').locator('button')
    const child = await locator.elementHandle()
    assert(child)
    try {
      assert.equal(
        await child.evaluate((element) => {
          if (!element || typeof element !== 'object' || !(element instanceof Element))
            throw new Error('Expected an element')
          return element.id
        }),
        'child',
      )
      const property = await child.getProperty('id')
      try {
        assert.equal(await property.jsonValue(), 'child')
      } finally {
        await property.dispose()
      }
    } finally {
      await child.dispose()
    }
    const children = await locator.elementHandles()
    try {
      assert.deepEqual(
        await Promise.all(
          children.map((element) =>
            element.evaluate((node) => {
              if (!node || typeof node !== 'object' || !(node instanceof Element))
                throw new Error('Expected an element')
              return node.id
            }),
          ),
        ),
        ['child'],
      )
    } finally {
      await Promise.all(children.map((element) => element.dispose()))
    }
    results.push('child frame handle ownership')
    results.push('child frame handle array ownership')
  }
  if (mode === 'native-query') {
    assert.equal(await page.locator('missing').count(), 0)
    assert.equal(await page.frameLocator('missing').locator('span').count(), 0)
    await assert.rejects(page.locator('css=[').count(), /selector|CSS|SyntaxError/iu)
    await new NativeSelectors(source).register('throwingCount', () => ({
      query() {
        throw new Error('user selector failure')
      },
      queryAll() {
        throw new Error('user selector failure')
      },
    }))
    await assert.rejects(page.locator('throwingCount=x').count(), /user selector failure/u)
    await new NativeSelectors(source).register('navigatingCount', () => ({
      query(root, selector) {
        window.location.href = 'about:blank'
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        window.location.href = 'about:blank'
        return root.querySelectorAll(selector)
      },
    }))
    await page.setContent('<iframe srcdoc="<div>child</div>"></iframe>')
    await page.waitForTimeout(100)
    assert.equal(
      await page.frameLocator('navigatingCount=iframe').locator('navigatingCount=div').count(),
      0,
    )
    results.push('count snapshots empty nodes and absent frames')
    results.push('count preserves authored selector exceptions')
    results.push('count finishes when custom query navigates its scope')
  }
  // Deep text uses an explicit bounded stack instead of the JavaScript call stack.
  await page.evaluate(() => {
    const root = document.createElement('div')
    root.id = 'deep'
    let node = root
    for (let index = 0; index < 4000; index++) {
      const child = document.createElement('span')
      node.append(child)
      node = child
    }
    node.append('deep content')
    document.body.replaceChildren(root)
  })
  await query(
    'deep text avoids recursion overflow',
    [
      { engine: 'css', selector: '#deep' },
      { engine: 'filter', hasText: { text: 'deep content' } },
    ],
    ['deep'],
  )
  await page.evaluate(() => {
    document.body.style.display = 'none'
    document.body.replaceChildren(document.createTextNode('x'.repeat(20 * 1024 * 1024)))
  })
  const limited =
    mode === 'source-helper'
      ? page.evaluate(
          `(${helper})([{engine:'css',selector:'body'},{engine:'filter',hasText:{text:'missing'}}])`,
        )
      : new NativeLocator(page, '<text budget>', [
          { engine: 'css', selector: 'body' },
          { engine: 'filter', hasText: { text: 'missing' } },
        ]).count()
  await assert.rejects(limited, /Native DOM text byte limit exceeded/u)
  await page.setContent('<p>recovered</p>')
  await query(
    'query recovers after text byte budget error',
    [{ engine: 'css', selector: 'p' }],
    [''],
  )
  results.push('text byte budget controlled error')
  await page.setContent('<main><div id="branch-root"></div></main>')
  await page.evaluate(() => {
    let current = [document.querySelector('#branch-root')]
    let nextId = 0
    for (let depth = 0; depth <= 12; depth++) {
      const next = []
      for (const node of current) {
        for (let index = 0; index < 2; index++) {
          const span = document.createElement('span')
          span.id = `branch-span-${++nextId}`
          node.append(span)
        }
        if (depth < 12)
          for (let index = 0; index < 2; index++) {
            const div = document.createElement('div')
            node.append(div)
            next.push(div)
          }
      }
      current = next
    }
  })
  for (const selector of [
    'div div div div div div div div div div span',
    'div + div div div span + span',
    'div ~ div div ~ div div ~ div div ~ div div ~ div span',
  ]) {
    const expected = await page.evaluate(
      (selector) => Array.from(document.querySelectorAll(selector), (node) => node.id),
      selector,
    )
    const actual = z
      .array(z.string())
      .parse(
        mode === 'source-helper'
          ? await page.evaluate(
              `(${helper})([{engine:'css',selector:${JSON.stringify(selector)}}],document).map(node=>node.id)`,
            )
          : await new NativeLocator(page, selector, [{ engine: 'css', selector }]).evaluateAll(
              (nodes) => nodes.map((node) => node.id),
            ),
      )
    assert.deepEqual(actual, expected, selector)
  }
  results.push(
    'deep branching CSS prefix matching preserves every actual native node and order without exponential backtracking',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(state, {
    otherOperations: 0,
    evaluationGroups: 0,
    activeHandles: 0,
    pendingHandles: 0,
  })
  const report = {
    mode,
    cefVersion: source.cefVersion,
    helperSha256: createHash('sha256').update(helperSource).digest('hex'),
    checks: results,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
