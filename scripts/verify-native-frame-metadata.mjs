// SPDX-License-Identifier: MIT
/* global document, HTMLIFrameElement, location, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = '.suitecut/native-frame-metadata'
await mkdir(directory, { recursive: true })
let stderr = ''
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html;charset=utf-8')
  response.end('<p>Actual frame metadata document</p>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const checks = [],
  rows = []
/** @typedef {{frame:{id:string,name?:string|undefined,url:string},childFrames?:InspectorTree[]|undefined}} InspectorTree */
/**
 * Validate a genuine Inspector frame tree before traversing its native IDs.
 * @type {z.ZodType<InspectorTree>}
 */
const inspectorTreeSchema = z.lazy(() =>
  z.object({
    frame: z.object({ id: z.string().min(1), name: z.string().optional(), url: z.string() }),
    childFrames: z.array(inspectorTreeSchema).optional(),
  }),
)
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeHandleDisposals: z.number(),
  nativeHandleDisposalPhysical: z.number(),
  bindingCalls: z.number(),
  bindingArgumentBytes: z.number(),
  bindingPendingRegistrations: z.number(),
  pendingBindingCommands: z.number(),
})
try {
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext(),
    page = await context.newPage()
  await page.goto(base)
  /**
   * Create one actual iframe and resolve its canonical native owner.
   * @param {string} name Actual author name.
   * @param {string} url Actual navigation URL.
   */
  const make = async (name, url) => {
    const actual = z.object({ name: z.string(), url: z.string() }).parse(
      await page.evaluate(
        async ({ name, url }) => {
          for (const node of Array.from(document.querySelectorAll('iframe'))) node.remove()
          const frame = document.createElement('iframe')
          frame.id = 'actual-frame-metadata'
          frame.name = name
          frame.src = url
          /** @type {Promise<{name:string,url:string}>} */
          const loaded = new Promise((resolve, reject) => {
            frame.onload = () => {
              if (!frame.contentWindow) {
                reject(new Error('Actual child window missing'))
                return
              }
              resolve({ name: frame.contentWindow.name, url: frame.contentWindow.location.href })
            }
            frame.onerror = () => reject(new Error('Actual frame navigation failed'))
            document.body.appendChild(frame)
          })
          return await loaded
        },
        { name, url },
      ),
    )
    assert.equal(actual.name, name, 'Actual author window name must remain exact')
    let found
    for (let attempt = 0; attempt < 100; attempt++) {
      for (const frame of page.frames().filter((frame) => frame.parentFrame())) {
        const element = await frame.frameElement()
        try {
          if ((await element.getAttribute('id')) === 'actual-frame-metadata') found = frame
        } finally {
          await element.dispose()
        }
      }
      if (found?.name() === actual.name && found.url() === actual.url) break
      await delay(5)
    }
    assert(found, 'A genuine frame owner element must identify the native frame')
    assert.equal(found.name(), actual.name)
    assert.equal(found.url(), actual.url)
    assert.equal(page.frame({ name }), found)
    rows.push({
      name,
      actual,
      observed: { id: found.identity(), name: found.name(), url: found.url() },
    })
    return found
  }
  for (const name of ['Actual ASCII name', 'Actual 😀 Ω name']) await make(name, base + '/child')
  checks.push(
    'ordinary ASCII and paired Unicode frame names retain the actual owner identity and author document URL',
  )
  for (const name of ['Actual\ud800\0\\"name', 'Actual\udfff name'])
    await make(name, base + '/child')
  checks.push(
    'selected Inspector frame-name transport retains isolated high/low units, NUL, backslash and quote without dropping the genuine URL update',
  )
  for (const name of ['{"__suitecutNativeUTF16Scalar":"d800"}', '"\\ud800"'])
    await make(name, base + '/child')
  checks.push(
    'ordinary author names resembling private scalar dictionaries or JSON tokens remain opaque literal strings',
  )
  await make('Ω'.repeat(1024), base + '/child')
  checks.push(
    'the native and SDK frame-name bounds count actual UTF16 units rather than UTF8 bytes',
  )
  await make('\ud800'.repeat(1024), base + '/child')
  checks.push(
    'the full supported isolated-unit bound survives the dedicated bounded native nameJSON envelope',
  )
  /**
   * Compare SDK fields with a fresh independent actual Inspector reply, never
   * with another SDK cache. The frame's genuine session is required for OOPIFs.
   * Chromium may reset window.name on browser-initiated navigation and expose
   * the owner ID as Page.Frame.name; record those actual facts without restoring
   * author state or assuming that navigation preserves an isolated-unit name.
   * @param {import('../dist/native.js').NativeFrame} frame Actual native frame.
   * @param {string} phase Actual lifecycle phase.
   */
  const observe = async (frame, phase) => {
    const tree = inspectorTreeSchema.parse(
      z
        .object({ frameTree: inspectorTreeSchema })
        .parse(await frame.sendDevToolsCommand('Page.getFrameTree')).frameTree,
    )
    const pending = [tree]
    /** @type {InspectorTree['frame']|undefined} */
    let inspector
    for (let work = 0; pending.length; work++) {
      assert(work < 4096, 'Actual Inspector tree traversal must stay bounded')
      const entry = pending.pop()
      assert(entry)
      if (entry.frame.id === frame.identity()) {
        inspector = entry.frame
        break
      }
      pending.push(...(entry.childFrames ?? []))
    }
    assert(inspector, 'The actual Inspector target must contain the immutable native frame ID')
    const actualWindow = z
      .object({ name: z.string(), url: z.string(), isTop: z.literal(false) })
      .parse(
        await frame.evaluate(() => ({
          name: window.name,
          url: location.href,
          isTop: window === window.top,
        })),
      )
    const actualParent = z.object({ id: z.string(), name: z.string(), src: z.string() }).parse(
      await page.evaluate(() => {
        const owner = document.querySelector('#actual-frame-metadata')
        if (!(owner instanceof HTMLIFrameElement)) throw new Error('Actual owner element missing')
        return { id: owner.id, name: owner.name, src: owner.src }
      }),
    )
    assert.equal(frame.identity(), inspector.id)
    assert.equal(frame.name(), inspector.name ?? '')
    assert.equal(frame.url(), inspector.url)
    assert.equal(frame.url(), actualWindow.url)
    assert.equal(
      page.frames().find((candidate) => candidate.identity() === inspector.id),
      frame,
      'Native canonical frame identity must remain genuine',
    )
    rows.push({
      phase,
      inspector,
      actualWindow,
      actualParent,
      observed: { id: frame.identity(), name: frame.name(), url: frame.url() },
    })
    return { inspector, actualWindow, actualParent }
  }
  const name = 'Actual navigation\ud800 name',
    child = await make(name, base + '/child'),
    originalId = child.identity()
  const initial = await observe(child, 'before-real-navigation')
  assert.equal(initial.inspector.name, name)
  assert.equal(initial.actualWindow.name, name)
  await child.goto(base + '/next')
  const same = await observe(child, 'after-same-origin-navigation')
  assert.equal(child.identity(), originalId)
  assert.equal(same.inspector.url, base + '/next')
  assert.equal(same.actualParent.name, name)
  checks.push(
    'same-frame real navigation reports the current actual Inspector name, ID and URL, independently recording Chromium window-name reset and owner-ID fallback',
  )
  const bridge = await page.exposeBinding('actualFrameMetadataSource', (where) => ({
    name: where.frame.name(),
    url: where.frame.url(),
    identity: where.frame.identity(),
    sameFrame: where.frame === child,
    samePage: where.page === page,
    sameContext: where.context === context,
  }))
  try {
    await child.goto(`http://localhost:${address.port}/cross`)
    const cross = await observe(child, 'after-cross-origin-OOPIF-navigation')
    assert.equal(child.identity(), originalId)
    assert.equal(cross.actualParent.name, name)
    assert.equal(cross.inspector.url, `http://localhost:${address.port}/cross`)
    assert(
      z
        .object({ sessions: z.number().positive() })
        .parse(await page.source.runAutomation('diagnostics')),
    )
    assert.deepEqual(await child.evaluate('globalThis.actualFrameMetadataSource()'), {
      name: cross.inspector.name ?? '',
      url: cross.inspector.url,
      identity: cross.inspector.id,
      sameFrame: true,
      samePage: true,
      sameContext: true,
    })
  } finally {
    await bridge.dispose()
  }
  checks.push(
    'real OOPIF actual-session Inspector metadata and binding source agree on the same genuine canonical frame, current name and URL',
  )
  for (let attempt = 0; attempt < 200; attempt++) {
    if (
      Object.values(schema.parse(await page.source.runAutomation('diagnostics'))).every(
        (value) => value === 0,
      )
    )
      break
    await delay(5)
  }
  const state = schema.parse(await page.source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await context.close()
  checks.push(
    'all actual handles, query preparations, binding calls, command ownership and evaluation groups retire to zero',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify(
      {
        checks,
        rows,
        state,
        provenance:
          'Initial isolated-unit transport is checked against actual author values; post-navigation names, IDs and URLs are checked against independent actual per-target Page.getFrameTree replies. Actual window.name and parent iframe.name are recorded without restoring or synthesizing them.',
      },
      null,
      2,
    ) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native frame metadata diagnostics must remain empty')
