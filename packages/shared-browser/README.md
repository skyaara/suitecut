# Shared Browser

`@suitecut/shared-browser` opens one native browser window that you and an AI
agent use at the same time. The window has suitecut's own tab strip and
toolbar; every tab is a full Chromium page drawn in the window, not a stream.
The agent drives tabs through a TypeScript SDK or an MCP server, and you can
click, type and open tabs in the same window while it works.

This package is a preview (0.x). Its API may change before 1.0.

- **Shared tabs.** Tabs you open are tabs the agent can see and drive, and the reverse.
- **Nothing to attach to.** Commands go over the browser's stdin and CDP runs in-process. There is no remote debugging port for another program to find.
- **Looks like a person.** Input arrives as trusted events. In our 2026-11-01 run, sannysoft, CreepJS, BrowserScan, incolumitas and BotD all reported a normal browser.
- **Prompts stay yours.** Permission requests show Allow and Block in the toolbar. Dialogs wait for an answer from you or the agent.

Requires Node.js 22 or newer, macOS on Apple silicon, and the suitecut browser
runtime (`suitecut-browser`), which is distributed separately from npm.

## MCP server

```sh
npx -y @suitecut/shared-browser --executable /path/to/suitecut-browser
```

The server speaks MCP over stdio. The window opens on the first tool call, and
opens again if you close it.

```json
{
  "mcpServers": {
    "shared-browser": {
      "command": "npx",
      "args": ["-y", "@suitecut/shared-browser"],
      "env": { "SUITECUT_SHARED_BROWSER": "/path/to/suitecut-browser" }
    }
  }
}
```

| Flag            | Default                    | Meaning                                                       |
| --------------- | -------------------------- | ------------------------------------------------------------- |
| `--executable`  | `$SUITECUT_SHARED_BROWSER` | Path to `suitecut-browser`                                    |
| `--permissions` | `ask`                      | `ask` shows Allow and Block; `allow` or `deny` answer for you |
| `--url`         | `about:blank`              | First page                                                    |

Tools: `browser_navigate`, `browser_snapshot`, `browser_click`,
`browser_hover`, `browser_type`, `browser_press`, `browser_scroll`,
`browser_evaluate`, `browser_screenshot`, `browser_wait_for`, `browser_back`,
`browser_reload`, `browser_tabs`, `browser_new_tab`, `browser_select_tab`,
`browser_close_tab`, `browser_dialog`, `browser_permission`, `browser_events`,
`browser_record_start`, `browser_record_stop`.

## TypeScript SDK

```ts
import { launchSharedBrowser } from '@suitecut/shared-browser'

const browser = await launchSharedBrowser({
  executablePath: '/path/to/suitecut-browser',
  url: 'https://example.com/',
})
const tab = await browser.activeTab()
console.log(await tab.snapshot()) // headings, text and controls with refs such as e3
await tab.fillRef('e3', 'hello')
await tab.clickRef('e4')
const png = await tab.screenshot()
await browser.close()
```

- **Tabs:** `browser.tabs()`, `newTab(url)`, `tab(id)`, `activeTab()`. Popups and `target=_blank` links open as tabs and arrive as `popup` events.
- **Navigation:** `tab.goto(url)`, `back()`, `forward()` and `reload()` wait for loading to end and return load errors instead of throwing.
- **Reading:** `evaluate(expression)`, `title()`, `url()`, `content()`, `screenshot()` and `snapshot()`. A ref is found again each time it is used, so the page's DOM and globals are never touched.
- **Input:** `mouseMove`, `click`, `wheel`, `type`, `press`, `clickRef`, `fillRef`, sent as CDP `Input.*` events.
- **Dialogs:** with `dialogs: 'control'` (the default) a dialog waits for `tab.answerDialog(accept, text)`. Input that opens one returns at once.
- **Permissions:** `permissions: 'ask'` puts Allow and Block in the toolbar; `browser.answerPermission(allow)` answers from code.
- **Background tabs:** the first input or screenshot on a tab turns on focus emulation, so a tab behind others, or a covered window, keeps rendering.
- **Recording:** `browser.startRecording({ path, fps, audio })` records the window from Chromium's own compositor frames, not screen recording, through suitecut's hardware H.264 encoder, with the front tab's sound if asked. `stop()` returns the frame counts. Cropped to the tab in front by default.
- **Events:** `browser.on(listener)`, `waitForEvent(match)` and `eventsSince(cursor)` cover loads, popups, dialogs, downloads and permission prompts.

## Limits

- macOS on Apple silicon only.
- The window is on screen; there is no headless mode.
- Back and Forward reload pages instead of restoring them from the cache. The browser turns off BackForwardCache so that a new page never reports a zero window size.
- Not tested against commercial bot protection such as Cloudflare or DataDome.

See [docs/shared-browser.md](../../docs/shared-browser.md) for the control protocol and the bot-detection notes.
