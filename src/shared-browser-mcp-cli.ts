#!/usr/bin/env node
// SPDX-License-Identifier: MIT
import process from 'node:process'
import { parseArgs } from 'node:util'

import { serveSharedBrowserMcp } from './shared-browser-mcp.js'

// shared-browser-mcp --executable <suitecut-browser> [--permissions ask|allow|deny] [--url <first page>]
const { values } = parseArgs({
  options: {
    executable: { type: 'string', default: process.env.SUITECUT_SHARED_BROWSER ?? '' },
    permissions: { type: 'string', default: 'ask' },
    url: { type: 'string', default: 'about:blank' },
  },
})
if (!values.executable) {
  process.stderr.write('shared-browser-mcp: pass --executable or set SUITECUT_SHARED_BROWSER\n')
  process.exit(2)
}
const permissions =
  values.permissions === 'allow' || values.permissions === 'deny' ? values.permissions : 'ask'
await serveSharedBrowserMcp({
  browser: { executablePath: values.executable, permissions, url: values.url },
  input: process.stdin,
  output: process.stdout,
})
