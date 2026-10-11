import { chromium, firefox, webkit, type Browser } from 'playwright'
import * as z from 'zod'

const CommonOptions = {
  endpoint: z.url(),
  headers: z.record(z.string(), z.string()).optional(),
  timeoutMs: z.number().int().nonnegative().default(30000),
  slowMoMs: z.number().nonnegative().default(0),
}
const CDPOptions = z.strictObject({
  ...CommonOptions,
  protocol: z.literal('cdp').default('cdp'),
  browserName: z.literal('chromium').default('chromium'),
  noDefaults: z.boolean().default(true),
})
const PlaywrightOptions = z.strictObject({
  ...CommonOptions,
  protocol: z.literal('playwright'),
  browserName: z.enum(['chromium', 'firefox', 'webkit']).default('chromium'),
  exposeNetwork: z.string().optional(),
})
const ConnectionOptions = z.union([CDPOptions, PlaywrightOptions]).superRefine((value, ctx) => {
  const scheme = new URL(value.endpoint).protocol
  const schemes = value.protocol === 'cdp' ? ['http:', 'https:', 'ws:', 'wss:'] : ['ws:', 'wss:']
  if (!schemes.includes(scheme)) {
    ctx.addIssue({
      code: 'custom',
      path: ['endpoint'],
      message: `The ${value.protocol} protocol requires ${schemes.join(', ')} URLs`,
    })
  }
})

/** Settings for attaching to an independently managed browser endpoint. */
export type BrowserConnectionOptions = z.input<typeof ConnectionOptions>

/** Connects ordinary Playwright automation to a caller-owned external browser. */
export async function connectBrowser(options: BrowserConnectionOptions): Promise<Browser> {
  const settings = ConnectionOptions.parse(options)
  const common = {
    timeout: settings.timeoutMs,
    slowMo: settings.slowMoMs,
    ...(settings.headers === undefined ? {} : { headers: settings.headers }),
  }
  if (settings.protocol === 'cdp') {
    return chromium.connectOverCDP(settings.endpoint, {
      ...common,
      noDefaults: settings.noDefaults,
    })
  }
  const browserType = { chromium, firefox, webkit }[settings.browserName]
  return browserType.connect(settings.endpoint, {
    ...common,
    ...(settings.exposeNetwork === undefined ? {} : { exposeNetwork: settings.exposeNetwork }),
  })
}
