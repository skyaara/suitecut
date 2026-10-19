// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = '.suitecut/native-shadow-hit'
await mkdir(directory, { recursive: true })
let stderr = ''
const mode = process.env.SUITECUT_QUERY_HELPER_SOURCE === '1' ? 'source-helper' : 'native-action'
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
  const text = await readFile('native/browser/browser.cc', 'utf8')
  const logic = z
    .string()
    .parse(
      /let target=document\.elementFromPoint\(x,y\);while\(target&&target.shadowRoot\)\{[\s\S]*?if\(!receives\)return \{ready:false\};/u.exec(
        text,
      )?.[0],
    )
  const helper = 'function(e,x,y){' + logic + 'return {ready:true};}'
  /**
   * Read the exact authored readiness expression over the real shadow node.
   * @param {string} expression
   */
  async function inspect(expression) {
    return z
      .object({ ready: z.boolean() })
      .parse(
        await page.evaluate(
          '(()=>{const e=' +
            expression +
            ',r=e.getBoundingClientRect(),x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;return (' +
            helper +
            ')(e,x,y);})()',
        ),
      ).ready
  }
  await page.setContent('<div id="host">Actual slotted text</div>')
  await page.evaluate(() => {
    document.querySelector('#host').attachShadow({ mode: 'open' }).innerHTML =
      '<a id="link" href="#actual" onclick="globalThis.nativeShadowClicks=(globalThis.nativeShadowClicks||0)+1"><slot></slot></a>'
  })
  assert.equal(await inspect('document.querySelector("#host").shadowRoot.querySelector("a")'), true)
  await page.click('#link', { timeout: 1000, ...(mode === 'source-helper' ? { force: true } : {}) })
  assert.equal(await page.evaluate(() => Reflect.get(globalThis, 'nativeShadowClicks') === 1), true)
  checks.push(
    'actual slotted Text hit stack admits its real shadow anchor and genuine browser input fires the author listener',
  )
  await page.evaluate(() => {
    const overlay = document.createElement('div')
    overlay.id = 'overlay'
    overlay.style.cssText = 'position:fixed;inset:0;background:red;z-index:100'
    document.body.appendChild(overlay)
  })
  assert.equal(
    await inspect('document.querySelector("#host").shadowRoot.querySelector("a")'),
    false,
  )
  await assert.rejects(page.click('#link', { timeout: 150 }), /Timeout|timed out/)
  assert.equal(await page.evaluate(() => Reflect.get(globalThis, 'nativeShadowClicks') === 1), true)
  checks.push(
    'actual frontmost document overlay prevents native input and the original action deadline rejects',
  )
  await page.locator('#overlay').evaluate((element) => element.remove())
  await page.evaluate(() => {
    const host = document.querySelector('#host'),
      nested = document.createElement('div')
    nested.id = 'nested'
    host.shadowRoot.replaceChildren(nested)
    nested.attachShadow({ mode: 'open' }).innerHTML =
      '<button id="nestedButton" onclick="globalThis.nativeNestedClicks=(globalThis.nativeNestedClicks||0)+1">Actual nested target</button>'
  })
  assert.equal(
    await inspect(
      'document.querySelector("#host").shadowRoot.querySelector("#nested").shadowRoot.querySelector("button")',
    ),
    true,
  )
  await page.click('#nestedButton', {
    timeout: 1000,
    ...(mode === 'source-helper' ? { force: true } : {}),
  })
  assert.equal(await page.evaluate(() => Reflect.get(globalThis, 'nativeNestedClicks') === 1), true)
  checks.push(
    'genuine nested shadow hit stacks preserve exact descendant targeting across multiple host boundaries',
  )
  await page.evaluate(() => {
    const root = document.querySelector('#host').shadowRoot.querySelector('#nested').shadowRoot,
      overlay = document.createElement('div')
    overlay.id = 'innerOverlay'
    overlay.style.cssText = 'position:fixed;inset:0;background:red;z-index:100'
    root.appendChild(overlay)
  })
  assert.equal(
    await inspect(
      'document.querySelector("#host").shadowRoot.querySelector("#nested").shadowRoot.querySelector("button")',
    ),
    false,
  )
  await assert.rejects(page.click('#nestedButton', { timeout: 150 }), /Timeout|timed out/)
  assert.equal(await page.evaluate(() => Reflect.get(globalThis, 'nativeNestedClicks') === 1), true)
  checks.push(
    'genuine nested shadow overlay remains an obstruction and never fabricates a successful input',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      actionNavigationIntents: z.number(),
      actionNavigations: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ mode, checks, state }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ mode, checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
