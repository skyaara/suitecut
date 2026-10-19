#!/usr/bin/env node
// SPDX-License-Identifier: MIT
import { realpathSync } from 'node:fs'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import { NATIVE_VIEWER_USAGE, runNativeViewerCli } from './native-viewer-cli.js'

export const BROWSER_CONTROL_USAGE = `Browser Control

${NATIVE_VIEWER_USAGE.replace('suitecut view', 'browser-control view')}`

export async function runBrowserControlCli(args: readonly string[]): Promise<number> {
  try {
    const [command, ...rest] = args
    if (command === undefined || command === '--help' || command === '-h' || command === 'help') {
      process.stdout.write(BROWSER_CONTROL_USAGE)
      return 0
    }
    if (command === 'view') {
      if (rest.includes('--help') || rest.includes('-h')) {
        process.stdout.write(BROWSER_CONTROL_USAGE)
        return 0
      }
      return await runNativeViewerCli(rest)
    }
    throw new Error(`Unknown browser-control command: ${command}`)
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`)
    return 1
  }
}

const entryPath = process.argv[1]
const isEntryPoint =
  entryPath !== undefined && import.meta.url === pathToFileURL(realpathSync(entryPath)).href
if (isEntryPoint) process.exitCode = await runBrowserControlCli(process.argv.slice(2))
