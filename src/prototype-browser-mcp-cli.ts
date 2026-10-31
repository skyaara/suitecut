#!/usr/bin/env node
// SPDX-License-Identifier: MIT
import process from 'node:process'
import { parseArgs } from 'node:util'

import { servePrototypeBrowserMcp } from './prototype-browser-mcp.js'

// suitecut-prototype-mcp --executable <suitecut-browser> [--permissions ask|allow|deny] [--url <first page>]
const { values } = parseArgs({
  options: {
    executable: { type: 'string', default: process.env.SUITECUT_PROTOTYPE_BROWSER ?? '' },
    permissions: { type: 'string', default: 'ask' },
    url: { type: 'string', default: 'about:blank' },
  },
})
if (!values.executable) {
  process.stderr.write(
    'suitecut-prototype-mcp: pass --executable or set SUITECUT_PROTOTYPE_BROWSER\n',
  )
  process.exit(2)
}
const permissions =
  values.permissions === 'allow' || values.permissions === 'deny' ? values.permissions : 'ask'
await servePrototypeBrowserMcp({
  browser: { executablePath: values.executable, permissions, url: values.url },
  input: process.stdin,
  output: process.stdout,
})
