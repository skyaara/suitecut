// SPDX-License-Identifier: MIT
import { type ProtocolDialog } from './browser-protocol-dialog.js'
import { type ProtocolDownload } from './browser-protocol-download.js'
import { type BrowserWire } from './browser-wire.js'
import { type UntrustedInput } from './untrusted.js'

export type BrowserProtocol = 'cdp' | 'bidi'
export interface BrowserViewport {
  width: number
  height: number
}
export type BrowserWaitUntil = 'none' | 'interactive' | 'complete'
export interface BrowserScreenshotOptions {
  type?: 'png' | 'jpeg'
  quality?: number
  fullPage?: boolean
}
export interface BrowserCookie {
  name: string
  value: string
  domain: string
  path?: string
  secure?: boolean
  httpOnly?: boolean
  sameSite?: 'Strict' | 'Lax' | 'None'
  expires?: number
}

/** Protocol features, rather than an assertion of native CEF feature parity. */
export interface BrowserProtocolCapabilities {
  readonly backend: BrowserProtocol
  readonly isolatedContexts: boolean
  readonly viewport: boolean
  readonly screenshots: boolean
  readonly fullPageScreenshot: boolean
  readonly cookies: boolean
  readonly nativeInput: boolean
  readonly rawProtocol: true
  readonly nativeCapture: false
  readonly nativeViewer: false
  readonly frames?: boolean
  readonly downloads?: boolean
  readonly uploads?: boolean
  readonly dialogs?: boolean
}

/** One endpoint and one multiplexed connection for the whole browser. */
export interface ConnectBrowserOptions {
  backend: BrowserProtocol
  /** Browser-level CDP ws URL, CDP HTTP origin, or WebDriver BiDi ws URL. */
  endpointURL: string
  timeoutMs?: number
  signal?: AbortSignal
  /** Closing an attached session disconnects by default; opt in to browser shutdown. */
  closeBrowserOnClose?: boolean
}

export interface LaunchProtocolBrowserOptions {
  backend: BrowserProtocol
  executablePath: string
  headless?: boolean
  args?: string[]
  viewport?: BrowserViewport
  timeoutMs?: number
  signal?: AbortSignal
}

export interface DriverPage {
  id: string
  contextId: string
  url: string
  title?: string
}

export interface DriverFrame {
  id: string
  pageId: string
  url: string
  name?: string
  parentId?: string
}

/** Immediate locator geometry, or a reason to retry before any button is pressed. */
export type DriverClickPoint = { x: number; y: number } | { retry: string }

/** Cached CDP sessions are shared by frame, input and file controls. */
export type CDPPageRequest = (
  pageId: string,
  method: string,
  params?: Record<string, UntrustedInput>,
  frameId?: string,
) => Promise<Record<string, UntrustedInput>>

/** Internal adapter boundary. Drivers receive already validated public input. */
export interface BrowserProtocolDriver {
  readonly backend: BrowserProtocol
  readonly wire: BrowserWire
  readonly capabilities: BrowserProtocolCapabilities
  version: string
  initialize(): Promise<void>
  contexts(): Promise<string[]>
  pages(): Promise<DriverPage[]>
  newContext(): Promise<string>
  closeContext(id: string): Promise<void>
  newPage(contextId?: string): Promise<DriverPage>
  closePage(id: string): Promise<void>
  navigate(id: string, url: string, wait: BrowserWaitUntil): Promise<void>
  evaluate(id: string, expression: string, frameId?: string): Promise<UntrustedInput>
  screenshot(id: string, options: BrowserScreenshotOptions): Promise<Buffer>
  setViewport(id: string, viewport: BrowserViewport): Promise<void>
  click(
    id: string,
    x: number,
    y: number,
    button: 'left' | 'right' | 'middle',
    count: number,
    frameId?: string,
  ): Promise<void>
  clickLocator?(
    id: string,
    prepare: () => Promise<DriverClickPoint>,
    frameId?: string,
    beforeDown?: () => void,
  ): Promise<DriverClickPoint>
  insertText(id: string, text: string): Promise<void>
  press(id: string, key: string): Promise<void>
  scroll(id: string, x: number, y: number, deltaX: number, deltaY: number): Promise<void>
  cookies(contextId: string): Promise<BrowserCookie[]>
  setCookies(contextId: string, cookies: BrowserCookie[]): Promise<void>
  pageInfo?(id: string): DriverPage | undefined
  pageTitle?(id: string): Promise<string>
  preparePage?(id: string): Promise<void>
  activatePage?(id: string): Promise<void>
  frames?(id: string): Promise<DriverFrame[]>
  frameInfo?(pageId: string, frameId: string): DriverFrame | undefined
  resolveFrame?(pageId: string, expression: string, parentFrameId?: string): Promise<string>
  setInputFiles?(
    pageId: string,
    expression: string,
    files: string[],
    frameId?: string,
  ): Promise<void>
  subscribeDownloads?(
    pageId: string,
    listener: (download: ProtocolDownload) => void,
  ): Promise<() => void>
  subscribeDialogs?(pageId: string, listener: (dialog: ProtocolDialog) => void): () => void
  moveMouse?(pageId: string, x: number, y: number, frameId?: string): Promise<void>
  dragMouse?(
    pageId: string,
    from: { x: number; y: number },
    to: { x: number; y: number },
    steps: number,
    frameId?: string,
  ): Promise<void>
  disposeResources?(): Promise<void>
  closeBrowser(): Promise<void>
  /** Ends only an automation session created by this driver, preserving the browser. */
  disposeSession?(): Promise<void>
}
