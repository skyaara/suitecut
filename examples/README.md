# Examples

| Example                                                            | What it shows                                                                                                                               | Run                                                                        |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| [native.ts](native.ts)                                             | The smallest recording: a narrated look at Apple's iPhone store with page audio, rendered to a video                                        | `pnpm example:native`                                                      |
| [real-sites/ebay.ts](real-sites/ebay.ts)                           | Shop for a keyboard on eBay: real typing, a highlight on the results, a listing opened, its photo zoomed and its price pointed at, narrated | `node --experimental-strip-types examples/real-sites/ebay.ts`              |
| [real-sites/song-maker.ts](real-sites/song-maker.ts)               | Write and play a tune in Chrome Music Lab's Song Maker, recorded with the page's own sound                                                  | `node --experimental-strip-types examples/real-sites/song-maker.ts`        |
| [engine/frame-rates.ts](engine/frame-rates.ts)                     | Record Apple's iPhone store once at 120 fps, then render it at 24, 60 and 120                                                               | `node --experimental-strip-types examples/engine/frame-rates.ts`           |
| [browser-control/record-video.ts](browser-control/record-video.ts) | A Playwright-style headless eBay search with the context's `recordVideo` turned on                                                          | `node --experimental-strip-types examples/browser-control/record-video.ts` |
| [browser-control/live-viewer.ts](browser-control/live-viewer.ts)   | Stream a headless browser to your phone or another computer: an agent searches eBay while you watch, take control and hand it back          | `node --experimental-strip-types examples/browser-control/live-viewer.ts`  |
| [shared-browser/agent.ts](shared-browser/agent.ts)                 | An MCP host drives the shared browser window through eBay across two tabs while the window records itself                                   | `node --experimental-strip-types examples/shared-browser/agent.ts`         |
| [song-maker](song-maker/README.md)                                 | A real web app automated through native input and recorded at 1080p60 with page audio, installed from packed tarballs like a real consumer  | `pnpm example:song-maker`                                                  |

All of them need the built CEF executable and network access; set `SUITECUT_NATIVE_EXECUTABLE` to its
absolute path. The shared-browser example takes the same executable from
`SUITECUT_SHARED_BROWSER` instead. Every site run only browses and searches: no sign-in,
no cart and no checkout. See [build requirements](../docs/building.md).

The `real-sites` examples are the demos on the home page of
[suitecut.aakashreddy.com](https://suitecut.aakashreddy.com). Run `pnpm build` first, and
write their output somewhere else with `SUITECUT_EXAMPLE_OUTPUT`;
`scripts/record-site-demos.mjs` records eBay and Song Maker and writes the site's web copies.
