import { spawn } from 'node:child_process'
import { mkdir } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { createInterface } from 'node:readline'

// An agent shops on eBay in the shared browser's native window, through its MCP
// server, and records what it does. This script stands in for the MCP host: it
// speaks JSON-RPC over the server's stdin and stdout, and every step is a real
// tool call a model could make. It only searches and looks: no sign-in, no cart.
// Set SUITECUT_SHARED_BROWSER to the built suitecut-browser first. Outside this
// repository, start the server with `npx -y @suitecut/shared-browser` instead.
const executable = process.env.SUITECUT_SHARED_BROWSER
if (!executable) throw new Error('Set SUITECUT_SHARED_BROWSER to the built suitecut-browser')
const output = resolve(process.env.SUITECUT_EXAMPLE_OUTPUT ?? '.suitecut/examples')
const video = join(output, 'agent-window.mp4')
await mkdir(output, { recursive: true })

const server = spawn(
  process.execPath,
  [
    join(import.meta.dirname, '../../packages/shared-browser/dist/shared-browser-mcp-cli.js'),
    '--executable',
    executable,
  ],
  { stdio: ['pipe', 'pipe', 'inherit'] },
)

interface Reply {
  readonly id: number
  readonly result?: { content?: { type: string; text?: string }[]; isError?: boolean }
  readonly error?: { message: string }
}
const waiting = new Map<number, (reply: Reply) => void>()
createInterface({ input: server.stdout }).on('line', (line) => {
  const reply = JSON.parse(line) as Reply
  waiting.get(reply.id)?.(reply)
  waiting.delete(reply.id)
})
let next = 0
const send = (method: string, params: object) =>
  new Promise<Reply>((done) => {
    const id = ++next
    waiting.set(id, done)
    server.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`)
  })

/** Calls one tool, logs it, and returns its text; a tool error stops the run. */
async function call(name: string, args: Record<string, string | number | boolean> = {}) {
  const reply = await send('tools/call', { name, arguments: args })
  const text = reply.result?.content?.map((part) => part.text ?? '').join('\n') ?? ''
  if (reply.error || reply.result?.isError)
    throw new Error(`${name}: ${reply.error?.message ?? text}`)
  console.log(`${name} ${JSON.stringify(args)}\n  → ${text.split('\n')[0]?.slice(0, 100)}`)
  return text
}

/** The ref of the first element in a snapshot whose line matches. */
function find(snapshot: string, pattern: RegExp) {
  const line = snapshot.split('\n').find((text) => pattern.test(text))
  return line?.match(/\[ref=(e\d+)\]/)?.[1]
}

try {
  await send('initialize', {
    protocolVersion: '2025-06-18',
    capabilities: {},
    clientInfo: { name: 'suitecut-example-agent', version: '1.0.0' },
  })
  server.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' })}\n`)

  // Start on a page that has painted, so the video doesn't open on a blank window.
  await call('browser_navigate', { url: 'https://www.ebay.com/' })
  await call('browser_wait_for', { ms: 2500 })
  await call('browser_record_start', { path: video, fps: 30 })
  // A still page sends no frames; a small scroll paints the first one at once.
  await call('browser_scroll', { deltaY: 240 })
  await call('browser_wait_for', { ms: 800 })
  await call('browser_scroll', { deltaY: -240 })
  await call('browser_wait_for', { ms: 800 })

  // Find the search field by what it is, not where it is, and search.
  const home = await call('browser_snapshot')
  const search = find(home, /^- (combobox|searchbox|textbox) "Search for anything"/)
  if (!search) throw new Error('No search field on the page')
  await call('browser_type', { ref: search, text: 'mechanical keyboard' })
  await call('browser_wait_for', { ms: 600 })
  await call('browser_press', { key: 'Enter' })
  await call('browser_wait_for', { text: 'results' })
  await call('browser_wait_for', { ms: 1500 })
  await call('browser_scroll', { deltaY: 600 })
  await call('browser_wait_for', { ms: 1500 })

  // Open the first real keyboard: eBay's "Shop on eBay" cards are placeholders, and
  // keypads and key caps match "keyboard" too.
  const results = await call('browser_snapshot')
  const listing = find(
    results,
    /^- link "(?!Shop on eBay)(?![^"]*(keypad|key ?caps?))[^"]*keyboard.*href="https:\/\/www\.ebay\.com\/itm\//i,
  )
  if (!listing) throw new Error('No listing in the results')
  const before = JSON.parse(await call('browser_tabs')) as { id: number }[]
  const resultsTab = before[0]?.id
  if (resultsTab === undefined) throw new Error('No tab is open')
  await call('browser_click', { ref: listing })

  // The listing opens in a new tab. Bring it to the front and look at it.
  let item: number | undefined
  for (let attempt = 0; attempt < 10 && item === undefined; attempt++) {
    await call('browser_wait_for', { ms: 1000 })
    const tabs = JSON.parse(await call('browser_tabs')) as { id: number }[]
    item = tabs.find((tab) => !before.some((old) => old.id === tab.id))?.id
  }
  if (item === undefined) throw new Error('The listing did not open a tab')
  await call('browser_select_tab', { tab: item })
  await call('browser_wait_for', { tab: item, text: 'Condition' })
  await call('browser_wait_for', { ms: 1500 })
  await call('browser_scroll', { tab: item, deltaY: 450 })
  await call('browser_wait_for', { ms: 2000 })
  await call('browser_scroll', { tab: item, deltaY: 450 })
  await call('browser_wait_for', { ms: 1500 })

  // Back to the results.
  await call('browser_select_tab', { tab: resultsTab })
  await call('browser_wait_for', { ms: 2000 })

  console.log(await call('browser_record_stop'))
} finally {
  server.stdin.end() // the server closes the browser when its input ends
  await new Promise((done) => server.once('exit', done))
}
