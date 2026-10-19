// SPDX-License-Identifier: MIT
import {
  launchNativeBrowser,
  type NativeBrowser,
  type NativeBrowserOptions,
} from './native-browser.js'
import {
  NativeBrowserContexts,
  NativeBrowserContext,
  type NativeBrowserContextOptions,
  type NativeContextTransport,
} from './native-context.js'
import { NativeAPIRequestContext, type NativeAPIRequestContextOptions } from './native-http.js'
import { type NativePage } from './native-page.js'
import { type NativeFixtureDefinition } from './native-suite.js'

export interface NativeBrowserFixtureOptions {
  readonly browser: NativeBrowserOptions
  readonly context?: NativeBrowserContextOptions
  readonly request?: NativeAPIRequestContextOptions
}
export type NativeBrowserFixtureDefinitions = Readonly<
  Record<'nativeSource' | 'browser' | 'context' | 'page' | 'request', NativeFixtureDefinition>
>
function contextTransport(source: NativeBrowser): source is NativeBrowser & NativeContextTransport {
  return (
    typeof source.runContext === 'function' &&
    typeof source.pageSource === 'function' &&
    typeof source.onContextEvent === 'function'
  )
}
/** The browser fixture owns the actual helper process as well as its contexts. */
export class NativeFixtureBrowser {
  private readonly contextsController: NativeBrowserContexts
  constructor(private readonly source: NativeBrowser & NativeContextTransport) {
    this.contextsController = new NativeBrowserContexts(source)
  }
  contexts(): NativeBrowserContext[] {
    return this.contextsController.contexts()
  }
  defaultContext(): NativeBrowserContext {
    return this.contextsController.defaultContext()
  }
  newContext(options?: NativeBrowserContextOptions): Promise<NativeBrowserContext> {
    return this.contextsController.newContext(options)
  }
  newPage(options?: NativeBrowserContextOptions): Promise<NativePage> {
    return this.contextsController.newPage(options)
  }
  async close(): Promise<void> {
    await this.source.close()
    await this.source.closed
  }
}

/** Real CEF fixture factories. The native runner owns scopes, deadlines and retries. */
export function createNativeBrowserFixtures(
  options: NativeBrowserFixtureOptions,
): NativeBrowserFixtureDefinitions {
  const issuedSources = new WeakMap<object, NativeBrowser>()
  return {
    nativeSource: {
      scope: 'worker',
      setup: async (_dependencies, use) => {
        const source = await launchNativeBrowser(options.browser)
        issuedSources.set(source, source)
        try {
          await use(source)
        } finally {
          try {
            await source.close()
          } finally {
            await source.closed
            issuedSources.delete(source)
          }
        }
      },
    },
    browser: {
      scope: 'worker',
      dependencies: ['nativeSource'],
      setup: async ({ nativeSource }, use) => {
        const source =
          nativeSource && typeof nativeSource === 'object'
            ? issuedSources.get(nativeSource)
            : undefined
        if (!source || !contextTransport(source))
          throw new Error('Native browser fixture requires its actual owned context transport')
        await use(new NativeFixtureBrowser(source))
      },
    },
    context: {
      dependencies: ['browser'],
      setup: async ({ browser }, use, info) => {
        if (info.scope === 'hook')
          throw new Error(
            'Native context fixtures require a test scope; create an explicit context in an all hook',
          )
        if (!(browser instanceof NativeFixtureBrowser))
          throw new Error('Native context fixture requires an actual native browser')
        const context = await browser.newContext(options.context)
        try {
          await use(context)
        } finally {
          await context.close()
        }
      },
    },
    page: {
      dependencies: ['context'],
      setup: async ({ context }, use) => {
        if (!(context instanceof NativeBrowserContext))
          throw new Error('Native page fixture requires an actual native context')
        const page = await context.newPage()
        try {
          await use(page)
        } finally {
          await page.close()
        }
      },
    },
    request: {
      dependencies: ['nativeSource'],
      setup: async ({ nativeSource }, use) => {
        const source =
          nativeSource && typeof nativeSource === 'object'
            ? issuedSources.get(nativeSource)
            : undefined
        if (!source) throw new Error('Native request fixture requires its actual owned source')
        const request = await NativeAPIRequestContext.create(source, options.request)
        try {
          await use(request)
        } finally {
          await request.dispose()
        }
      },
    },
  }
}
