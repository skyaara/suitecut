# Shared browser: SDK and MCP

`@suitecut/shared-browser` (`packages/shared-browser`) is a preview. It runs one native CEF window with
SuiteCut's own tab strip and toolbar instead of Chrome's. Every tab is a full
Chromium page in Alloy style. Nothing is streamed: the pages draw in the
window, and a person can use the same tabs an agent is driving.

The browser is `suitecut-browser` started with `--suitecut-prototype=tabs`. See
`native/browser/process/prototype_tabs.h` for its flags. With
`--suitecut-prototype-control` it reads one JSON command per line on stdin. It
writes replies (`{id, ok, result}`) and events (`{event, tab, ...}`) on stdout.
CDP methods run in-process through `CefBrowserHost::ExecuteDevToolsMethod`.
There is no remote debugging port.

## TypeScript SDK

```ts
import { launchSharedBrowser } from '@suitecut/shared-browser'

const browser = await launchSharedBrowser({ executablePath, url: 'https://example.com/' })
const tab = await browser.activeTab()
console.log(await tab.snapshot()) // headings, text, and interactive elements with refs
await tab.fillRef('e3', 'hello')
await tab.clickRef('e4')
const png = await tab.screenshot()
await browser.close()
```

- **Tabs:**
  - `browser.tabs()`, `browser.newTab(url)`, `browser.tab(id)`, `browser.activeTab()`.
  - Popups and `target=_blank` links open as tabs, and are reported as `popup` events.
- **Navigation:**
  - `tab.goto(url)`, `back()`, `forward()` and `reload()` wait until loading ends.
  - Load errors are returned, not thrown.
- **Reading the page:**
  - `evaluate(expression)` returns JSON values; promises are awaited.
  - `title()`, `url()`, `content()`, `screenshot()`.
  - `snapshot()` gives an agent-oriented outline. Refs such as `e12` are positions among visible interactive elements. A ref is found again when it is used, so the page's DOM and globals are never touched.
- **Input:**
  - `mouseMove(x, y, steps)`, `click(x, y)`, `wheel(dx, dy)`, `type(text)`, `press(key)`, `clickRef(ref)`, `fillRef(ref, text)`.
  - Events go through CDP `Input.*`, so pages see them as trusted.
- **Dialogs:**
  - By default (`dialogs: 'control'`) a dialog waits for `tab.answerDialog(accept, text)`.
  - Input that opens a dialog returns at once.
  - Reads of a paused page fail with a clear message, and `snapshot()` reports the open dialog.
- **Permissions:**
  - `permissions: 'ask'` shows Allow and Block in our toolbar; `browser.answerPermission(allow)` answers it.
  - `'allow'` and `'deny'` answer every prompt without asking.
- **Background tabs:**
  - A hidden page draws no frames, whether its tab is in the background or its window is covered.
  - The first time the SDK sends input or takes a screenshot on a tab, it turns on `Emulation.setFocusEmulationEnabled`, as Playwright does. That tab then keeps rendering and reports itself visible and focused.

## Window size after navigation

In Alloy Views on macOS, a new document widget can report its window
geometry as 0 until shortly after `load`. The affected values are
`outerWidth`, `outerHeight` and `screenX`. A new widget comes from
RenderDocument, or from the process swap that BackForwardCache makes.

- **How often:** 7 of 40 navigations without the fix.
- **Why it matters:** Chrome never reports 0 for a visible window, and BotD
  classified such a page as headless Chrome.
- **Fix:** the prototype starts Chromium with
  `--disable-features=RenderDocument,BackForwardCache`. With that, 0 of 40
  navigations showed it, and about 1 in 80 in a second sample.
- **Cost:** Back and Forward reload pages instead of restoring them from the
  cache.

## MCP server

```bash
npx -y @suitecut/shared-browser --executable <path to suitecut-browser>
```

It speaks MCP over stdio. The browser starts on the first tool call, and again if
its window was closed. Example client configuration:

```json
{
  "mcpServers": {
    "suitecut-browser": {
      "command": "npx",
      "args": ["-y", "@suitecut/shared-browser", "--executable", "<suitecut-browser>"]
    }
  }
}
```

Tools: `browser_navigate`, `browser_snapshot`, `browser_click`, `browser_hover`,
`browser_type`, `browser_press`, `browser_scroll`, `browser_evaluate`,
`browser_screenshot`, `browser_wait_for`, `browser_back`, `browser_reload`,
`browser_tabs`, `browser_new_tab`, `browser_select_tab`, `browser_close_tab`,
`browser_dialog`, `browser_permission`, `browser_events`.

## Bot detection

`.suitecut/bot-detection-custom-ui-2026-11-01/` holds the 2026-11-01 benchmark,
driven through both the SDK and MCP. It uses the same five detectors, snapshot
script and input as the 2026-10-20 runs.
