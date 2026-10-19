// SPDX-License-Identifier: MIT
import { type LaunchProtocolBrowserOptions } from './browser-protocol-types.js'
import {
  launchBrowser as launchNativeController,
  type NativeBrowserController,
} from './native-browser-controller.js'
import { type NativeBrowserOptions } from './native-browser.js'
import { type ProtocolBrowser } from './remote-browser.js'

export type LaunchBrowserOptions =
  (NativeBrowserOptions & { backend?: 'cef' }) | LaunchProtocolBrowserOptions

/** CEF remains the default. An explicit protocol backend drives an installed browser. */
export function launchBrowser(
  options: NativeBrowserOptions & { backend?: 'cef' },
): Promise<NativeBrowserController>
export function launchBrowser(options: LaunchProtocolBrowserOptions): Promise<ProtocolBrowser>
export function launchBrowser(
  options: LaunchBrowserOptions,
): Promise<NativeBrowserController | ProtocolBrowser>
export async function launchBrowser(
  options: LaunchBrowserOptions,
): Promise<NativeBrowserController | ProtocolBrowser> {
  if (options.backend === undefined || options.backend === 'cef') {
    const nativeOptions = { ...options }
    delete nativeOptions.backend
    return launchNativeController(nativeOptions)
  }
  if (options.backend !== 'cdp' && options.backend !== 'bidi')
    throw new Error('Unsupported browser backend')
  return (await import('./remote-browser.js')).launchProtocolBrowser(options)
}
