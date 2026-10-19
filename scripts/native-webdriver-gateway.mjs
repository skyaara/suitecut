// SPDX-License-Identifier: MIT
// Test-only W3C wire adapter; every browser operation uses the native CEF engine.
/* global document, getComputedStyle, window */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { randomUUID } from 'node:crypto'
import { readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const directory = z.string().parse(process.argv[2])
/**
 * The immutable module is checked against the public SDK interface.
 * @type {typeof import('../src/native.js')}
 */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- The fixture owns and hashes this exact dynamic module.
const native = await import(pathToFileURL(`${directory}/sdk/native.js`).href)
const pin = z
  .object({ cefVersion: z.string(), chromiumVersion: z.string() })
  .parse(JSON.parse(await readFile(resolve('native/browser/cef-build.json'), 'utf8')))
let diagnostics = ''
const source = await native.launchNativeBrowser({
  executablePath: `${directory}/runtime/suitecut-browser`,
  width: 1280,
  height: 720,
  headless: true,
  onDiagnostic(message) {
    if (diagnostics.length < 1048576) diagnostics += message.slice(0, 1048576 - diagnostics.length)
  },
})
assert.equal(source.cefVersion, pin.cefVersion)
const browser = new native.NativeBrowserContexts(source)
/**
 * Each opaque W3C session references actual native objects.
 * @typedef {{context: import('../src/native.js').NativeBrowserContext, page: import('../src/native.js').NativePage, frame?: import('../src/native.js').NativeFrame,frameParent?: import('../src/native.js').NativeFrame, window:string, windows:Map<string,import('../src/native.js').NativePage>, elements:Map<string,{handle:import('../src/native.js').NativeElementHandle,window:string,frame:string,shadow:boolean,files?:string[]}>}} NativeWireSession
 * @type {Map<string, NativeWireSession>}
 */
const sessions = new Map()
/** @type {Map<string,{session:string,content:string,mime:string}>} */
const documents = new Map()
let documentBytes = 0
class WireError extends Error {
  /**
   * Carries a standard protocol error without changing native browser state.
   * @param {string} code
   * @param {string} message
   */
  constructor(code, message) {
    super(message)
    this.code = code
  }
}
const statuses = new Map([
  ['invalid argument', 400],
  ['invalid session id', 404],
  ['no such window', 404],
  ['unknown command', 404],
  ['no such element', 404],
  ['no such frame', 404],
  ['no such shadow root', 404],
  ['stale element reference', 404],
  ['invalid selector', 400],
  ['invalid element state', 400],
  ['element not interactable', 400],
])
/**
 * Checks actual native window and optional frame liveness.
 * @param {{page:import('../src/native.js').NativePage,frame?:import('../src/native.js').NativeFrame}} session
 * @param {boolean} frame
 */
function ensureOpen(session, frame = true) {
  if (session.page.isClosed())
    throw new WireError('no such window', 'The native top-level browsing context is closed')
  if (frame && session.frame?.isDetached())
    throw new WireError('no such window', 'The native selected frame is detached')
}
/**
 * Canonicalizes opaque wire references by actual native DOM identity.
 * @param {NativeWireSession} session
 * @param {import('../src/native.js').NativeElementHandle} handle
 * @param {boolean} shadow
 * @returns {Promise<string>}
 */
async function registerElement(session, handle, shadow) {
  const frame = (session.frame ?? session.page.mainFrame()).identity()
  for (const [id, entry] of session.elements)
    if (entry.window === session.window && entry.frame === frame && entry.shadow === shadow) {
      try {
        if (await handle.evaluate((node, other) => node === other, entry.handle)) {
          await handle.dispose()
          return id
        }
      } catch {
        // A retained reference can belong to an actually destroyed native realm.
      }
    }
  const id = randomUUID()
  session.elements.set(id, { handle, window: session.window, frame, shadow })
  return id
}
/**
 * Releases references and actual native context ownership once.
 * @param {string} id
 * @param {NativeWireSession} session
 */
async function deleteSession(id, session) {
  await Promise.allSettled([...session.elements.values()].map((entry) => entry.handle.dispose()))
  session.elements.clear()
  for (const [key, document] of documents)
    if (document.session === id) {
      documentBytes -= Buffer.byteLength(document.content)
      documents.delete(key)
    }
  await session.context.close()
  sessions.delete(id)
}
/**
 * Dispatches this fixture's implemented W3C commands to real native operations.
 * @param {string} method
 * @param {string} path
 * @param {unknown} body
 * @returns {Promise<unknown>}
 */
async function command(method, path, body) {
  if (method === 'POST' && path === '/session') {
    const context = await browser.newContext()
    const page = await context.newPage(),
      sessionId = randomUUID()
    const window = randomUUID()
    sessions.set(sessionId, {
      context,
      page,
      window,
      windows: new Map([[window, page]]),
      elements: new Map(),
    })
    return {
      sessionId,
      capabilities: {
        browserName: 'suitecut-cef',
        browserVersion: pin.chromiumVersion,
        platformName: process.platform,
      },
    }
  }
  const match = /^\/session\/([^/]+)(.*)$/u.exec(path)
  if (!match)
    throw new WireError('unknown command', `The native fixture has no command ${method} ${path}`)
  const session = sessions.get(match[1] ?? '')
  if (!session) throw new WireError('invalid session id', 'This native session does not exist')
  const suffix = match[2]
  if (method === 'DELETE' && !suffix) {
    await deleteSession(match[1] ?? '', session)
    return null
  }
  if (method === 'POST' && suffix === '/fixture/document') {
    const document = z
      .object({
        content: z.string(),
        mime: z.enum(['text/html', 'application/xml', 'application/xhtml+xml']),
      })
      .parse(body)
    const bytes = Buffer.byteLength(document.content)
    if (documents.size >= 1024 || bytes > 1048576 || documentBytes + bytes > 16 * 1048576)
      throw new WireError('invalid argument', 'Authored inline document capacity exceeded')
    const id = randomUUID(),
      address = server.address()
    assert(address && typeof address !== 'string')
    documents.set(id, { ...document, session: match[1] ?? '' })
    documentBytes += bytes
    return `http://127.0.0.1:${address.port}/fixture/document/${id}`
  }
  if (method === 'GET' && suffix === '/window/handles')
    return [...session.windows].filter(([, page]) => !page.isClosed()).map(([id]) => id)
  if (method === 'POST' && suffix === '/window') {
    const handle = z.object({ handle: z.string() }).parse(body).handle,
      page = session.windows.get(handle)
    if (!page || page.isClosed())
      throw new WireError('no such window', 'The actual native window is unavailable')
    session.window = handle
    session.page = page
    delete session.frame
    delete session.frameParent
    return null
  }
  if (method === 'POST' && suffix === '/window/new') {
    ensureOpen(session, false)
    const params = z.object({ type: z.string().nullable().optional() }).safeParse(body)
    if (!params.success)
      throw new WireError('invalid argument', 'The new context type must be a string or null')
    const page = await session.context.newPage(),
      handle = randomUUID()
    session.windows.set(handle, page)
    return { handle, type: 'tab' }
  }
  if (method === 'GET' && suffix === '/title') {
    ensureOpen(session, false)
    return session.page.title()
  }
  if (method === 'GET' && suffix === '/url') {
    ensureOpen(session)
    return session.page.url()
  }
  if (method === 'POST' && suffix === '/url') {
    ensureOpen(session, false)
    const parsed = z.object({ url: z.string() }).safeParse(body)
    if (!parsed.success || !URL.canParse(parsed.data.url))
      throw new WireError('invalid argument', 'The url parameter must be an absolute URL')
    await session.page.goto(parsed.data.url)
    delete session.frame
    delete session.frameParent
    return null
  }
  if (method === 'DELETE' && suffix === '/window') {
    ensureOpen(session, false)
    await session.page.close()
    const handles = [...session.windows].filter(([, page]) => !page.isClosed()).map(([id]) => id)
    if (!handles.length) await deleteSession(match[1] ?? '', session)
    return handles
  }
  if (method === 'POST' && suffix === '/frame/parent') {
    ensureOpen(session, false)
    const parent = session.frame?.parentFrame() ?? session.frameParent
    if (parent?.isDetached())
      throw new WireError('no such window', 'The actual native parent context is detached')
    if (parent?.parentFrame()) session.frame = parent
    else delete session.frame
    const next = parent?.parentFrame()
    if (next) session.frameParent = next
    else delete session.frameParent
    return null
  }
  ensureOpen(session)
  if (method === 'GET' && suffix === '/window') return session.window
  const target = session.frame ?? session.page
  const frameIdentity = (session.frame ?? session.page.mainFrame()).identity()
  if (method === 'POST' && suffix === '/fixture/evaluate') {
    const expression = z.object({ expression: z.string() }).parse(body).expression
    const handle = await target.evaluateHandle(expression)
    try {
      const type = await handle.evaluate((value) => typeof value)
      const metadata = { type, nativeHandle: handle.guid }
      if (type === 'undefined' || type === 'object' || type === 'function') return metadata
      return { ...metadata, value: await handle.jsonValue() }
    } finally {
      await handle.dispose()
    }
  }
  if (method === 'GET' && suffix === '/element/active') {
    const handle = await target.evaluateHandle(() => document.activeElement)
    const element = handle.asElement()
    if (!element) {
      await handle.dispose()
      throw new WireError('no such element', 'The actual document has no active element')
    }
    return {
      ['element-6066-11e4-a52e-4f735466cecf']: await registerElement(session, element, false),
    }
  }
  if (method === 'POST' && suffix === '/elements') {
    const query = z.object({ using: z.literal('css selector'), value: z.string() }).safeParse(body)
    if (!query.success)
      throw new WireError('invalid argument', 'The native fixture supports CSS selector lookup')
    const array = await target.evaluateHandle(
      (selector) => Array.from(document.querySelectorAll(selector)),
      query.data.value,
    )
    const elements = []
    try {
      for (const [name, handle] of await array.getProperties()) {
        const element = handle.asElement()
        if (/^\d+$/u.test(name) && element)
          elements.push({
            ['element-6066-11e4-a52e-4f735466cecf']: await registerElement(session, element, false),
          })
        else await handle.dispose()
      }
    } finally {
      await array.dispose()
    }
    return elements
  }
  if (method === 'POST' && suffix === '/element') {
    const query = z.object({ using: z.literal('css selector'), value: z.string() }).safeParse(body)
    if (!query.success)
      throw new WireError('invalid argument', 'The native fixture supports CSS selector lookup')
    let handle
    try {
      handle = await target.evaluateHandle(
        (selector) => document.querySelector(selector),
        query.data.value,
      )
    } catch (error) {
      throw new WireError(
        'invalid selector',
        error instanceof Error ? error.message : String(error),
      )
    }
    const element = handle.asElement()
    if (!element) {
      await handle.dispose()
      throw new WireError('no such element', 'The actual native document has no matching element')
    }
    return {
      ['element-6066-11e4-a52e-4f735466cecf']: await registerElement(session, element, false),
    }
  }
  if (method === 'POST' && suffix === '/frame') {
    const parsed = z
      .object({
        id: z.union([
          z.number().int().min(0).max(65535),
          z.record(z.string(), z.unknown()),
          z.null(),
        ]),
      })
      .safeParse(body)
    if (!parsed.success)
      throw new WireError(
        'invalid argument',
        'Frame id must be null, an unsigned 16-bit index, or an element reference',
      )
    const value = parsed.data.id
    if (value === null) {
      delete session.frame
      delete session.frameParent
      return null
    }
    let handle
    let temporary = false
    if (typeof value === 'number') {
      const candidate = await target.evaluateHandle((index) => {
        const windowRef = window.frames[index]
        if (!windowRef) return null
        return (
          Array.from(document.querySelectorAll('iframe,frame')).find((node) => {
            const frame = /** @type {HTMLIFrameElement} */ (node)
            return frame.contentWindow === windowRef
          }) ?? null
        )
      }, value)
      handle = candidate.asElement()
      if (!handle) {
        await candidate.dispose()
        throw new WireError('no such frame', 'The actual native frame index does not exist')
      }
      temporary = true
    } else {
      const identifier = z.string().safeParse(value['element-6066-11e4-a52e-4f735466cecf'])
      if (!identifier.success)
        throw new WireError(
          'invalid argument',
          'A frame element reference requires a string identifier',
        )
      const entry = session.elements.get(identifier.data)
      if (
        !entry ||
        entry.shadow ||
        entry.window !== session.window ||
        entry.frame !== frameIdentity
      )
        throw new WireError(
          'no such element',
          'The actual native element reference is outside the current browsing context',
        )
      handle = entry.handle
      try {
        if (!(await handle.evaluate((node) => /** @type {Node} */ (node).isConnected)))
          throw new WireError(
            'stale element reference',
            'The actual native frame element is detached',
          )
      } catch (error) {
        if (error instanceof WireError) throw error
        throw new WireError(
          'stale element reference',
          error instanceof Error ? error.message : String(error),
        )
      }
    }
    let frame
    try {
      frame = await handle.contentFrame()
    } finally {
      if (temporary) await handle.dispose()
    }
    if (!frame) throw new WireError('no such frame', 'This actual native element has no frame')
    session.frame = frame
    const parent = frame.parentFrame()
    if (parent) session.frameParent = parent
    else delete session.frameParent
    return null
  }
  const elementRoute =
    /^\/element\/([^/]+)\/(click|clear|value|rect|text|shadow|name|selected|enabled|(?:css|property|attribute)\/[^/]+)$/u.exec(
      suffix ?? '',
    )
  if (elementRoute) {
    const entry = session.elements.get(elementRoute[1] ?? '')
    if (!entry || entry.shadow || entry.window !== session.window || entry.frame !== frameIdentity)
      throw new WireError(
        'no such element',
        'The real native element reference is outside the current scope',
      )
    let connected
    try {
      connected = await entry.handle.evaluate((node) => /** @type {Node} */ (node).isConnected)
    } catch (error) {
      throw new WireError(
        'stale element reference',
        error instanceof Error ? error.message : String(error),
      )
    }
    if (!connected)
      throw new WireError('stale element reference', 'The actual native node is detached')
    if (method === 'GET' && elementRoute[2] === 'text')
      return entry.handle.evaluate((node) => /** @type {HTMLElement} */ (node).innerText)
    if (method === 'GET' && elementRoute[2]?.startsWith('attribute/'))
      return entry.handle.evaluate(
        (node, name) => /** @type {Element} */ (node).getAttribute(name),
        decodeURIComponent(elementRoute[2].slice(10)),
      )
    if (method === 'GET' && elementRoute[2]?.startsWith('property/'))
      return entry.handle.evaluate(
        (node, property) =>
          /** @type {unknown} */ (Reflect.get(/** @type {object} */ (node), property)),
        decodeURIComponent(elementRoute[2].slice(9)),
      )
    if (method === 'GET' && elementRoute[2] === 'rect')
      return entry.handle.evaluate((node) => {
        const box = /** @type {Element} */ (node).getBoundingClientRect()
        return {
          x: box.x + window.scrollX,
          y: box.y + window.scrollY,
          width: box.width,
          height: box.height,
        }
      })
    if (method === 'POST' && elementRoute[2] === 'value') {
      const parsedText = z.object({ text: z.string() }).safeParse(body)
      if (!parsedText.success) throw new WireError('invalid argument', 'Text must be a string')
      const text = parsedText.data.text
      const fileInput = await entry.handle.evaluate(
        (node) => node instanceof HTMLInputElement && node.type === 'file',
      )
      if (fileInput) {
        const count = await entry.handle.evaluate(
          (node) => /** @type {HTMLInputElement} */ (node).files?.length ?? 0,
        )
        if (count !== (entry.files?.length ?? 0))
          throw new WireError(
            'unknown error',
            'Native file append cannot adopt externally replaced file paths',
          )
        const paths = [...(entry.files ?? []), ...text.split('\n')]
        await entry.handle.setInputFiles(paths)
        entry.files = paths
      } else {
        try {
          await entry.handle.sendKeys(text, { timeout: 1000 })
        } catch (error) {
          if (error instanceof Error && error.message.includes('Element not interactable'))
            throw new WireError('element not interactable', error.message)
          throw error
        }
      }
      return null
    }
    if (method === 'POST' && elementRoute[2] === 'clear') {
      const state = await entry.handle.evaluate((node) => {
        const element = /** @type {HTMLElement} */ (node)
        const input = element instanceof HTMLInputElement
        const textarea = element instanceof HTMLTextAreaElement
        const resettable = input || textarea
        const file = input && element.type === 'file'
        return {
          file,
          readOnly: resettable && element.readOnly,
          disabled: element.matches(':disabled'),
          editable: resettable
            ? !input ||
              !['checkbox', 'radio', 'hidden', 'submit', 'button', 'image'].includes(element.type)
            : element.isContentEditable,
          empty: resettable
            ? file
              ? !element.files?.length
              : element.value === '' && !element.validity.badInput
            : false,
        }
      })
      if (state.readOnly || state.disabled || !state.editable)
        throw new WireError(
          'invalid element state',
          'The actual native element is readonly, disabled, or not an editable control',
        )
      if (state.empty) {
        if (state.file) entry.files = []
        return null
      }
      try {
        if (state.file) {
          await entry.handle.setInputFiles([])
          entry.files = []
        } else await entry.handle.clear({ timeout: 1000 })
        // W3C clear finishes by removing focus; invoke the real platform method.
        // Input values and input/change events are owned by the native action.
        await entry.handle.evaluate((node) => /** @type {HTMLElement} */ (node).blur())
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error)
        if (
          /readonly|editable|cannot be filled|cannot fill|unsupported input|input of type/iu.test(
            message,
          )
        )
          throw new WireError('invalid element state', message)
        if (/visible|viewport|intercept|timeout/iu.test(message))
          throw new WireError('element not interactable', message)
        throw error
      }
      return null
    }
    if (method === 'GET' && elementRoute[2] === 'name')
      return entry.handle.evaluate((node) => {
        const element = /** @type {Element} */ (node)
        return element.prefix ? `${element.prefix}:${element.localName}` : element.localName
      })
    if (method === 'GET' && elementRoute[2] === 'selected')
      return entry.handle.evaluate((node) => {
        if (node instanceof HTMLInputElement && ['checkbox', 'radio'].includes(node.type))
          return node.checked
        return node instanceof HTMLOptionElement ? node.selected : false
      })
    if (method === 'GET' && elementRoute[2] === 'enabled')
      return entry.handle.evaluate((node) => {
        const element = /** @type {Element} */ (node)
        return (
          element.ownerDocument.contentType !== 'application/xml' && !element.matches(':disabled')
        )
      })
    if (method === 'GET' && elementRoute[2]?.startsWith('css/'))
      return entry.handle.evaluate(
        (node, property) =>
          getComputedStyle(/** @type {Element} */ (node)).getPropertyValue(property),
        decodeURIComponent(elementRoute[2].slice(4)),
      )
    if (method === 'GET' && elementRoute[2] === 'shadow') {
      const shadow = await target.evaluateHandle(
          (node) => /** @type {Element} */ (node).shadowRoot,
          entry.handle,
        ),
        element = shadow.asElement()
      if (!element) {
        await shadow.dispose()
        throw new WireError(
          'no such shadow root',
          'The actual native element has no open shadow root',
        )
      }
      return {
        ['shadow-6066-11e4-a52e-4f735466cecf']: await registerElement(session, element, true),
      }
    }
    if (method === 'POST' && elementRoute[2] === 'click') {
      await entry.handle.click({ timeout: 1000 })
      return null
    }
  }
  if (method === 'POST' && suffix === '/execute/sync') {
    const parsed = z.object({ script: z.string(), args: z.array(z.unknown()) }).safeParse(body)
    if (!parsed.success) throw new WireError('invalid argument', 'Script and args are required')
    const args = parsed.data.args.map((value) => {
      if (value && typeof value === 'object' && 'element-6066-11e4-a52e-4f735466cecf' in value) {
        const id = z.string().parse(Reflect.get(value, 'element-6066-11e4-a52e-4f735466cecf')),
          entry = session.elements.get(id)
        if (!entry)
          throw new WireError('no such element', 'The actual native script argument is unavailable')
        return entry.handle
      }
      return value
    })
    const value = await target.evaluateHandle(
      ({ script, args }) => {
        // eslint-disable-next-line @typescript-eslint/no-implied-eval -- Executes the actual W3C script body in the browser, never in this host process.
        const fn = new Function(script)
        const result = /** @type {unknown} */ (Reflect.apply(fn, globalThis, args))
        return result
      },
      { script: parsed.data.script, args },
    )
    const element = value.asElement()
    if (element)
      return {
        ['element-6066-11e4-a52e-4f735466cecf']: await registerElement(session, element, false),
      }
    try {
      const data = await value.jsonValue()
      return data === undefined ? null : data
    } finally {
      await value.dispose()
    }
  }
  if (method === 'POST' && suffix === '/fixture/closed-window') {
    const original = session.window,
      page = await session.context.newPage(),
      window = randomUUID()
    session.windows.set(window, page)
    session.window = window
    session.page = page
    delete session.frame
    await page.setContent('<p id=closed-element>Closed window</p>')
    const handle = await page.locator('p').elementHandle()
    assert(handle)
    const element = await registerElement(session, handle, false)
    await page.close()
    return { original, element }
  }
  if (method === 'POST' && suffix === '/fixture/closed-frame') {
    const attached = new Promise((accept) => session.page.once('frameattached', accept))
    await session.page.setContent(
      '<div id=delete>Actual retained parent</div><iframe srcdoc="<title>Native child</title><body>Child</body>"></iframe>',
    )
    await session.page.frameLocator('iframe').locator('body').waitFor()
    const frame = /** @type {unknown} */ (await attached)
    assert(frame instanceof native.NativeFrame, 'The browser must emit its actual child frame')
    session.frame = frame
    const parent = frame.parentFrame()
    if (parent) session.frameParent = parent
    else delete session.frameParent
    const detached = new Promise((accept) => session.page.once('framedetached', accept))
    await session.page.locator('iframe').evaluate((element) => element.remove())
    await detached
    assert(frame.isDetached(), 'The native frame must actually be detached')
    return null
  }
  throw new WireError('unknown command', `The native fixture has no command ${method} ${suffix}`)
}
const server = createServer((request, response) => {
  void (async () => {
    if (
      request.method === 'GET' &&
      /^\/webdriver\/tests\/support\/html\/[a-z]+\.html$/u.test(request.url ?? '')
    ) {
      response.setHeader('content-type', 'text/html; charset=utf-8')
      response.end(await readFile(`${directory}/original${request.url ?? ''}`))
      return
    }
    if (request.method === 'GET' && request.url?.startsWith('/fixture/document/')) {
      const document = documents.get(request.url.slice('/fixture/document/'.length))
      response.setHeader('cache-control', 'no-cache')
      if (!document) {
        response.writeHead(404).end('The authored document does not exist')
        return
      }
      response.setHeader('content-type', `${document.mime}; charset=utf-8`)
      response.end(document.content)
      return
    }
    response.setHeader('cache-control', 'no-cache')
    response.setHeader('content-type', 'application/json; charset=utf-8')
    const chunks = []
    let bytes = 0
    for await (const chunk of request) {
      const data = z.instanceof(Uint8Array).parse(chunk)
      bytes += data.byteLength
      if (bytes > 1048576)
        throw new WireError('invalid argument', 'Native fixture request exceeds 1 MiB')
      chunks.push(data)
    }
    const body = /** @type {unknown} */ (
      bytes ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : undefined
    )
    if (request.url === '/shutdown') {
      for (const session of sessions.values()) await session.context.close()
      sessions.clear()
      await source.close()
      await writeFile(`${directory}/native-stderr.log`, diagnostics)
      response.end(JSON.stringify({ value: null }))
      server.close()
      return
    }
    try {
      const value = await command(request.method ?? '', request.url ?? '', body)
      response.statusCode = 200
      response.end(JSON.stringify({ value }))
    } catch (error) {
      const code = error instanceof WireError ? error.code : 'unknown error'
      response.statusCode = statuses.get(code) ?? 500
      response.end(
        JSON.stringify({
          value: {
            error: code,
            message: error instanceof Error ? error.message : String(error),
            stacktrace: error instanceof Error ? (error.stack ?? '') : '',
          },
        }),
      )
    }
  })().catch((error) => {
    response.statusCode = 500
    response.end(
      JSON.stringify({
        value: {
          error: 'unknown error',
          message: error instanceof Error ? error.message : String(error),
          stacktrace: '',
        },
      }),
    )
  })
})
await new Promise((accept, reject) => {
  server.once('error', reject)
  server.listen(0, '127.0.0.1', accept)
})
const address = server.address()
assert(address && typeof address !== 'string')
console.log(JSON.stringify({ endpoint: `http://127.0.0.1:${address.port}` }))
