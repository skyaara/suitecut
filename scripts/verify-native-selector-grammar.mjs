// SPDX-License-Identifier: MIT
/* global document, HTMLElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { readFile, mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeLocator,
  NativeSelectors,
} from '../dist/native.js'

const header = await readFile('native/shared/dom_query.h', 'utf8'),
  helper = z.string().parse(/R"JS\(([\s\S]*)\)JS"/u.exec(header)?.[1])
const mode = process.env.SUITECUT_QUERY_HELPER_SOURCE === '1' ? 'source-helper' : 'native-query'
const directory = '.suitecut/native-selector-grammar'
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  async function install() {
    if (mode !== 'source-helper') return
    await page.evaluate(() => Reflect.set(globalThis, '__suitecutAuthoredSelectorSource', ''))
    for (let index = 0; index < helper.length; index += 12000)
      await page.evaluate(
        (chunk) =>
          Reflect.set(
            globalThis,
            '__suitecutAuthoredSelectorSource',
            String(Reflect.get(globalThis, '__suitecutAuthoredSelectorSource')) + chunk,
          ),
        helper.slice(index, index + 12000),
      )
    await page.evaluate(
      "globalThis.__suitecutAuthoredSelectorUnderTest=(0,eval)('('+globalThis.__suitecutAuthoredSelectorSource+')')",
    )
  }
  /**
   * Install an actual document fixture.
   * @param {string} html
   */
  async function set(html) {
    await page.setContent(html)
    await install()
  }
  /**
   * Resolve the authored query against genuine native nodes.
   * @param {import('../src/native-query.js').NativeQueryStep[]} query
   */
  async function steps(query) {
    if (mode === 'source-helper')
      return z
        .array(z.string())
        .parse(
          await page.evaluate(
            'globalThis.__suitecutAuthoredSelectorUnderTest(' +
              JSON.stringify(query) +
              ',document).map(node=>node.id||node.localName)',
          ),
        )
    return new NativeLocator(page, '<authored selector query>', query).evaluateAll((nodes) =>
      nodes.map((node) => node.id || node.localName),
    )
  }
  /**
   * Observe actual selector identities in the selected scope.
   * @param {string} selector
   * @param {string} root
   */
  async function q(selector, root = 'document') {
    if (mode === 'source-helper')
      return z
        .array(z.string())
        .parse(
          await page.evaluate(
            'globalThis.__suitecutAuthoredSelectorUnderTest(' +
              JSON.stringify([{ engine: 'css', selector }]) +
              ',' +
              root +
              ').map(node=>node.id||node.localName)',
          ),
        )
    if (root === 'document')
      return page.$$eval(selector, (nodes) => nodes.map((node) => node.id || node.localName))
    const handle = await page.evaluateHandle(root),
      element = handle.asElement()
    assert(element)
    try {
      return await element.$$eval(selector, (nodes) =>
        nodes.map((node) => node.id || node.localName),
      )
    } finally {
      await handle.dispose()
    }
  }
  /**
   * Assert real ordered DOM identities.
   * @param {string} selector
   * @param {string[]} ids
   * @param {string} root
   */
  async function expect(selector, ids, root = 'document') {
    assert.deepEqual(await q(selector, root), ids, selector)
    checks.push(selector)
  }
  await set(
    '<section id="scope"><div id="one">One<span id="child">Child</span></div><div id="two" data-testid="same">Two</div><div id="three" data-testid="same" hidden>Three</div></section><span id="outside">Outside</span>',
  )
  for (const selector of ['*', '* *', '*>*', '*:not(div)', '*:is(*)', '#one ~ div', '#one + div']) {
    assert.deepEqual(
      await q(selector),
      await page.evaluate(
        (selector) =>
          Array.from(document.querySelectorAll(selector), (node) => node.id || node.localName),
        selector,
      ),
    )
    checks.push(selector)
  }
  await expect('#one >> :scope ~ div', ['two', 'three'])
  await expect('#one >> :scope+div', ['two'])
  await expect('#two >> #one ~ :scope', ['two'])
  await expect('#two >> #one + :scope', ['two'])
  await expect('#two >> * > :scope', [])
  await expect('#two >> * :scope', [])
  await expect('div >> visible=true', ['one', 'two'])
  await expect('div >> visible=false', ['three'])
  await expect('data-testid=same', ['two', 'three'])
  await expect('data-testid=same', ['two', 'three'], 'document.querySelector("#scope")')
  await expect('section >> internal:has="span"', ['scope'])
  await expect('div >> internal:has-not="span"', ['two', 'three'])
  await expect('div >> internal:and="[hidden]"', ['three'])
  await expect('div >> internal:or="#outside"', ['one', 'two', 'three', 'outside'])
  await expect('section >> internal:chain="span"', ['child'])
  await expect('*css=section >> span', ['scope'])
  await expect('section >> //div', ['one', 'two', 'three'])
  await expect('xpath=/*', ['one', 'two', 'three'], 'document.querySelector("#scope")')
  for (const selector of [
    'internal:has="div"',
    'div >> internal:has=abc',
    'div >> internal:has=22',
    'div >> internal:has="span!"',
    ':nth-match(2)',
    ':nth-match(div,foo)',
    ':near(50)',
  ])
    await assert.rejects(
      q(selector),
      /cannot be first|Malformed selector|Unexpected token|engine expects/,
    )
  const original = "//*[contains(@Class, 'foo']"
  const error = await q(original).catch((error) => z.instanceof(Error).parse(error))
  assert(error instanceof Error)
  assert(error.message.includes("//*[contains(@Class, \\'foo\\']"))
  assert(!error.message.includes(".//*[contains(@Class, 'foo']"))
  checks.push('genuine malformed selector diagnostics and original XPath spelling')
  await set(
    '<div id="split">Hello<i id="comma">,</i> <span id="world">world</span><b id="bang">!</b></div><div id="empty"></div><div id="comments">hel<!-- actual comment -->lo world</div>',
  )
  await expect('text=Hello, world!', ['split'])
  await expect('text="Hello, world!"', [])
  await expect('text="Hello"', ['split'])
  await expect('text="world"', ['world'])
  await expect(':text-is("Hello")', ['split'])
  await expect(':text("world")', ['world', 'comments'])
  await expect('div >> text=""', ['empty'])
  await expect(':text-is("hello world")', ['comments'])
  assert.deepEqual(await steps([{ engine: 'text', text: 'Hello, world!', exact: true }]), ['split'])
  checks.push('exact getByText full-node semantics remain distinct from quoted immediate chunks')
  await expect('#split >> text=Hello', ['split'])
  await expect('text="Hello" >> text="Hello"', ['split'])
  await expect('*css=div >> text=Hello >> text=world', ['split', 'comments'])
  await expect('text=/world/i', ['world', 'comments'])
  await expect('text=/^[!]+$/', ['bang'])
  await set(
    '<head><title>title</title><script>var script</script><style>.style{}</style></head><body><script>var script</script><style>.style{}</style><div id="safe">title script style</div><div id="quote">hello"world<span id="nested">yay</span></div><div id="slash">\\</div><div id="x">x</div><div id="single">\'</div><div id="double">"</div></body>',
  )
  for (const word of ['title', 'script', 'style']) await expect('text=' + word, ['safe'])
  for (const selector of [
    'text=lo" >> span',
    'text  =lo" >> span',
    'text=  lo" >> span',
    ' text = lo" >> span',
  ])
    await expect(selector, ['nested'])
  await expect('text="\\x"', ['x'])
  await expect("text='\\\\'", ['slash'])
  await expect('text="', ['quote', 'double'])
  await expect("text='", ['single'])
  for (const selector of ['"', "'"]) await assert.rejects(q(selector), /Unterminated/)
  await set('<div id="css">hello\n wo"r&gt;&gt;ld</div>')
  await expect('div:has-text("hello\\a wo\\"r>>ld")', ['css'])
  for (const selector of [':text("foo","bar")', ':text(foo > bar)'])
    await assert.rejects(q(selector), /engine expects a single string/)
  await set('<div id="host"><span id="light" data-testid="light">Hello from light</span></div>')
  await page.evaluate(() => {
    document.querySelector('#host').attachShadow({ mode: 'open' }).innerHTML =
      '<span id="shadow" data-testid="shadow">Hello from shadow</span><slot></slot>'
  })
  await expect('div >> text=Hello', ['light', 'shadow'])
  await expect('css:light=#shadow', [])
  await expect('css:light=#shadow', ['shadow'], 'document.querySelector("#host").shadowRoot')
  await expect('text:light=shadow', [])
  await expect('data-testid:light=shadow', [])
  await expect('id:light=shadow', [])
  await expect('data-testid=shadow', ['shadow'])
  await expect('text=light', ['light'])
  await expect('text=shadow', ['shadow'])
  await set(
    '<div id="scene" style="position:relative"><div class="box" id="a" style="position:absolute;left:0;top:0;width:20px;height:20px"></div><div class="box" id="b" style="position:absolute;left:25px;top:0;width:20px;height:20px"></div><div class="box" id="c" style="position:absolute;left:60px;top:5px;width:10px;height:10px"></div><div class="box" id="d" style="position:absolute;left:0;top:30px;width:15px;height:15px"></div></div>',
  )
  await expect('.box:right-of(#a)', ['b', 'c'])
  await expect('.box:left-of(#c)', ['b', 'a', 'd'])
  await expect('.box:below(#a)', ['d'])
  await expect('.box:above(#d)', ['a', 'b', 'c'])
  await expect('.box:above(#d,10)', ['a', 'b'])
  await expect('.box:near(#a)', ['b', 'd', 'c'])
  await expect('.box:near(#a,5)', ['b'])
  await expect('.box:near(#a,4)', [])
  await expect('.box:right-of(#a):above(#d)', ['b', 'c'])
  await page.evaluate(() => {
    const node = document.querySelector('#b')
    if (!(node instanceof HTMLElement)) throw new Error('Actual geometry fixture missing')
    node.style.transform = 'translateX(100px)'
  })
  await expect('.box:near(#a)', ['d', 'c'])
  checks.push('layout geometry refreshes from actual transformed browser rectangles on each query')
  if (mode === 'native-query') {
    await set(
      '<section id="customRoot">literal "mark<span id="customChild">Child</span></section><aside id="customAside">Other</aside>',
    )
    await page.evaluate(() => Reflect.set(globalThis, 'grammarAuthorMarker', true))
    const selectors = new NativeSelectors(source)
    await selectors.register('grammarMain', () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (Reflect.get(globalThis, 'grammarAuthorMarker') !== true)
          throw new Error('Actual main selector realm missing')
        return root.querySelectorAll(selector)
      },
    }))
    await selectors.register(
      'grammarUtility',
      () => ({
        query(root, selector) {
          return root.querySelector(selector)
        },
        queryAll(root, selector) {
          if (Reflect.get(globalThis, 'grammarAuthorMarker') !== undefined)
            throw new Error('Actual utility selector realm was bypassed')
          return root.querySelectorAll(selector)
        },
      }),
      { contentScript: true },
    )
    await expect('grammarMain=section >> internal:has="grammarUtility=span"', ['customRoot'])
    await expect('grammarMain=section >> internal:and="grammarUtility=section"', ['customRoot'])
    await expect('grammarMain=section >> internal:or="grammarUtility=aside"', [
      'customRoot',
      'customAside',
    ])
    await expect('text=al " >> grammarUtility=span', ['customChild'])
  }
  await set('<div id="a"></div><div id="b"><span id="c"><span id="d"></span></span></div>')
  await expect('* > *', ['c', 'd'], 'document.body')
  await expect('* *', ['c', 'd'], 'document.body')
  await expect('* *:not(span)', [], 'document.body')
  await expect(':scope * > *', ['c', 'd'], 'document.body')
  await expect(':scope > *', ['a', 'b'], 'document.body')
  await set(
    '<section><span id="span1"><span id="inner"></span></span><span id="span2"></span></section>',
  )
  await expect('#span1 >> span:not(:has(:scope > div))', ['inner'])
  await expect('#span1 >> #inner,:scope', ['span1', 'inner'])
  await expect('#span1 >> span,:scope', ['span1', 'inner'])
  await expect('#span1 >> span:not(:scope)', ['inner'])
  await set('<div id="tick">hello`world<span id="tickChild">oh</span></div>')
  await expect('text=` >> span', [])
  await expect('text=ello`wor >> span', ['tickChild'])
  await expect('text=ello`wor', ['tick'])
  await set(
    '<section><div id="target" style="position:absolute;left:0;top:0;width:10px;height:10px"></div><div class="candidate" id="near" style="position:absolute;left:60px;top:60px;width:10px;height:10px"></div><div class="candidate" id="far" style="position:absolute;left:61px;top:61px;width:10px;height:10px"></div></section>',
  )
  await expect('.candidate:near(#target,100)', ['near'])
  await expect('.candidate:near(#target,102)', ['near', 'far'])
  await expect('.candidate:near(#target,99)', [])
  await set('<section id="root"><span id="inside"></span></section><span id="outside"></span>')
  await expect('#root:has(:is(:scope, #root))', ['root'])
  await expect('#root:has(:where(:scope))', ['root'])
  await expect('#root >> :is(:scope,span)', ['root', 'inside'])
  await expect('#root >> :where(:is(:scope))', ['root'])
  await expect('#root >> :is(:scope > span)', ['inside'])
  await expect('#root >> :is(:scope > span, #root)', ['inside'])
  await expect('#root >> :is(:scope)+span', ['outside'])
  await expect('#root >> :not(:has(:scope > div))', ['inside'])
  const state = z
    .object({
      otherOperations: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      nativeHandleDisposals: z.number(),
      nativeHandleDisposalPhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ mode, checks, state }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ mode, checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
