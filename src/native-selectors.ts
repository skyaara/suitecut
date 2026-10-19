// SPDX-License-Identifier: MIT
import { readFile, stat } from 'node:fs/promises'

import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import { setNativeTestIdAttribute } from './native-query.js'

export interface NativeSelectorEngine {
  query(root: Document | Element | ShadowRoot, selector: string): Element | null
  queryAll(root: Document | Element | ShadowRoot, selector: string): Iterable<Element>
}

/** The native engine owns registrations; authored DOM engines execute in its renderer. */
export class NativeSelectors {
  constructor(private readonly browser: NativeBrowser) {}

  async register(
    name: string,
    engine: (() => NativeSelectorEngine) | string | { path?: string; content?: string },
    options: { contentScript?: boolean } = {},
  ): Promise<void> {
    z.string()
      .min(1)
      .max(64)
      .regex(/^[A-Za-z0-9_-]+$/u)
      .parse(name)
    const config = z.strictObject({ contentScript: z.boolean().optional() }).parse(options)
    let source: string
    if (typeof engine === 'function') source = engine.toString()
    else if (typeof engine === 'string') source = engine
    else {
      const input = z
        .strictObject({ path: z.string().optional(), content: z.string().optional() })
        .parse(engine)
      if ((input.path !== undefined) === (input.content !== undefined))
        throw new Error('A native selector registration requires exactly one of path or content')
      if (input.path !== undefined) {
        if ((await stat(input.path)).size > 1024 * 1024)
          throw new Error('Native selector source exceeds 1 MiB')
        source = await readFile(input.path, 'utf8')
      } else source = input.content ?? ''
    }
    if (!source || Buffer.byteLength(source, 'utf8') > 1024 * 1024)
      throw new Error('Native selector source must contain 1 byte to 1 MiB')
    if (source.includes('[native code]'))
      throw new Error('Native selector factories must have serializable JavaScript source')
    if (!/^async\s+function\b/u.test(source) && /^async\s+[\p{L}\p{N}_$]+\s*\(/u.test(source))
      source = source.replace(/^async\s+/u, 'async function ')
    else if (
      /^[\p{L}\p{N}_$]+\s*\(/u.test(source) &&
      !source.startsWith('function') &&
      !/^async\s*\(/u.test(source)
    )
      source = `function ${source}`
    await this.browser.runAutomation('selectors', {
      action: 'register',
      name,
      source,
      contentScript: config.contentScript ?? false,
    })
  }

  setTestIdAttribute(attribute: string): void {
    setNativeTestIdAttribute(attribute)
  }
}
