import { beforeEach, describe, expect, it, vi } from 'vitest'

import { connectBrowser, type BrowserConnectionOptions } from '../src/browser-connection.js'

const connections = vi.hoisted(() => ({
  cdp: vi.fn(),
  chromium: vi.fn(),
  firefox: vi.fn(),
  webkit: vi.fn(),
}))
vi.mock('playwright', () => ({
  chromium: { connectOverCDP: connections.cdp, connect: connections.chromium },
  firefox: { connect: connections.firefox },
  webkit: { connect: connections.webkit },
}))

beforeEach(() => {
  for (const connection of Object.values(connections)) connection.mockReset().mockResolvedValue({})
})

describe('external browser connection', () => {
  it('preserves the existing CDP context settings by default and bounds connection time', async () => {
    await connectBrowser({ endpoint: 'http://localhost:9222' })
    expect(connections.cdp).toHaveBeenCalledWith('http://localhost:9222', {
      noDefaults: true,
      timeout: 30000,
      slowMo: 0,
    })
  })

  it('forwards authenticated CDP options and explicit overrides', async () => {
    await connectBrowser({
      endpoint: 'wss://browser.example/devtools/browser/id',
      headers: { Authorization: 'Bearer example' },
      timeoutMs: 0,
      slowMoMs: 25,
      noDefaults: false,
    })
    expect(connections.cdp).toHaveBeenCalledWith('wss://browser.example/devtools/browser/id', {
      headers: { Authorization: 'Bearer example' },
      timeout: 0,
      slowMo: 25,
      noDefaults: false,
    })
  })

  it.each(['chromium', 'firefox', 'webkit'] as const)(
    'selects the %s Playwright protocol and forwards network exposure',
    async (browserName) => {
      await connectBrowser({
        protocol: 'playwright',
        browserName,
        endpoint: 'ws://localhost:3000/browser',
        headers: { 'X-Test': 'value' },
        exposeNetwork: '<loopback>',
      })
      expect(connections[browserName]).toHaveBeenCalledWith('ws://localhost:3000/browser', {
        timeout: 30000,
        slowMo: 0,
        headers: { 'X-Test': 'value' },
        exposeNetwork: '<loopback>',
      })
      expect(connections.cdp).not.toHaveBeenCalled()
    },
  )

  it.each([
    { endpoint: 'file:///tmp/browser' },
    { endpoint: 'not-a-url' },
    { endpoint: 'http://localhost:9222', browserName: 'firefox' },
    { endpoint: 'http://localhost:3000', protocol: 'playwright' },
    { endpoint: 'ws://localhost:3000', protocol: 'invalid' },
    { endpoint: 'ws://localhost:3000', timeoutMs: -1 },
    { endpoint: 'ws://localhost:3000', timeoutMs: 1.5 },
    { endpoint: 'ws://localhost:3000', slowMoMs: -1 },
    { endpoint: 'ws://localhost:3000', launch: {} },
    { endpoint: 'ws://localhost:3000', headers: { Authorization: 123 } },
    { endpoint: 'ws://localhost:3000', protocol: 'playwright', noDefaults: true },
  ])('rejects invalid connection settings before opening a connection: %j', async (options) => {
    await expect(connectBrowser(options as BrowserConnectionOptions)).rejects.toThrow()
    for (const connection of Object.values(connections)) expect(connection).not.toHaveBeenCalled()
  })

  it('propagates connection failures', async () => {
    const error = new Error('Connection refused')
    connections.cdp.mockRejectedValue(error)
    await expect(connectBrowser({ endpoint: 'http://localhost:9222' })).rejects.toBe(error)
  })
})
