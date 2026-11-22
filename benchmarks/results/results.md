# Browser agent benchmark

2026-11-09T10:28:16.926Z, darwin 27.0.0, Apple M4 (10 cores, 16 GB). suitecut 2.1.0 (a6511ee-dirty), agent-browser 0.41.2, browser-use 0.13.14 on Google Chrome 156.0.8078.80. 3 cold rounds per tool; 1-minute load average at round start 2–5.

CPU and memory cover every process a run starts (browser, renderers, GPU, driver, and agent-browser's daemon), sampled every 0.5 s; CPU is the median run's total, memory the peak of the summed resident sizes, which counts shared pages more than once.

Snapshot and screenshot times are medians of 3 calls per page, then of all pages and rounds. Tokens are characters / 4 of the text an agent reads: suitecut `ariaSnapshot({ mode: 'ai', interactive: true })`, agent-browser `snapshot -i`, browser-use `llm_representation()`.

## Summary

| Tool | Launch ms | Snapshot ms | Snapshot tokens (7 pages) | Screenshot ms | Tasks passed | Retries | Task ms | Task tokens (11 tasks) | CPU s per run | Peak memory MB |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| suitecut | 770 | 39 | 19,106 | 145 | 33/33 | 1 | 757 | 92,587 | 40 | 3,795 |
| agent-browser | 3,708 | 34 | 25,935 | 199 | 32/33 | 4 | 677 | 98,359 | 25 | 3,416 |
| browser-use | 654 | 105 | 16,953 | 129 | 33/33 | 0 | 1,899 | 45,862 | 37 | 2,709 |

## Tasks

| Task | Goal | suitecut: passed / ms / tokens | agent-browser: passed / ms / tokens | browser-use: passed / ms / tokens |
|---|---|--:|--:|--:|
| hn-open-comments | Open the comments of the top Hacker News story | 3/3 / 730 / 2,128 | 3/3 / 597 / 3,753 | 3/3 / 1,716 / 3,250 |
| wikipedia-search | Search Wikipedia for "Web browser" and reach the article | 3/3 / 687 / 2,800 | 3/3 / 892 / 2,664 | 3/3 / 1,090 / 2,931 |
| mdn-open-reference | From the MDN HTML page, open the HTML elements reference | 3/3 / 333 / 1,708 | 3/3 / 133 / 1,582 | 3/3 / 974 / 1,498 |
| github-open-issues | Open the Issues tab of microsoft/playwright | 3/3 / 1,374 / 2,453 | 3/3 / 1,210 / 4,407 | 3/3 / 1,769 / 4,621 |
| bbc-open-sport | Open BBC Sport from the BBC News navigation | 3/3 / 659 / 3,332 | 3/3 / 469 / 4,545 | 3/3 / 1,409 / 1,234 |
| wikipedia-hops | Follow Wikipedia links from Web browser to Google Chrome, Chromium and Blink | 3/3 / 1,840 / 55,262 | 2/3 / 1,476 / 42,123 | 3/3 / 4,761 / 10,958 |
| mdn-hops | From MDN HTML, open the elements reference, then the <a> element | 3/3 / 512 / 6,436 | 3/3 / 365 / 14,060 | 3/3 / 1,899 / 3,195 |
| github-hops | Open the Issues tab of microsoft/playwright, then its Labels | 3/3 / 2,191 / 5,830 | 3/3 / 1,232 / 8,726 | 3/3 / 2,195 / 8,473 |
| python-docs-hops | From the Python docs, open the library reference, then the re module | 3/3 / 807 / 7,281 | 3/3 / 967 / 7,110 | 3/3 / 2,081 / 3,121 |
| react-hops | From the React tutorial, open Thinking in React, then Adding Interactivity | 3/3 / 628 / 2,906 | 3/3 / 476 / 3,664 | 3/3 / 2,455 / 1,960 |
| github-open-file | Open package.json in the microsoft/playwright file list | 3/3 / 968 / 2,453 | 3/3 / 596 / 5,728 | 3/3 / 1,248 / 4,624 |

## Pages

| Page | Tool | Load ms | Snapshot ms | Snapshot tokens | Full snapshot tokens | Screenshot ms |
|---|---|--:|--:|--:|--:|--:|
| news.ycombinator.com | suitecut | 1,155 | 28 | 2,071 | 9,289 | 143 |
| news.ycombinator.com | agent-browser | 1,316 | 25 | 3,631 | 6,550 | 120 |
| news.ycombinator.com | browser-use | 1,153 | 86 | 3,251 | – | 126 |
| en.wikipedia.org | suitecut | 1,721 | 49 | 5,169 | 19,938 | 183 |
| en.wikipedia.org | agent-browser | 606 | 59 | 5,050 | 21,264 | 251 |
| en.wikipedia.org | browser-use | 871 | 195 | 2,869 | – | 177 |
| developer.mozilla.org | suitecut | 343 | 24 | 1,804 | 7,469 | 147 |
| developer.mozilla.org | agent-browser | 286 | 21 | 1,653 | 6,124 | 195 |
| developer.mozilla.org | browser-use | 1,448 | 101 | 1,764 | – | 117 |
| www.bbc.com | suitecut | 2,199 | 39 | 3,361 | 6,682 | 802 |
| www.bbc.com | agent-browser | 2,016 | 34 | 6,179 | 9,146 | 226 |
| www.bbc.com | browser-use | 1,899 | 96 | 1,475 | – | 137 |
| github.com | suitecut | 2,287 | 51 | 3,522 | 13,023 | 145 |
| github.com | agent-browser | 1,103 | 44 | 5,921 | 11,062 | 208 |
| github.com | browser-use | 6,989 | 223 | 5,547 | – | 141 |
| docs.python.org | suitecut | 365 | 19 | 1,691 | 4,404 | 120 |
| docs.python.org | agent-browser | 998 | 20 | 1,668 | 4,223 | 170 |
| docs.python.org | browser-use | 488 | 48 | 1,175 | – | 92 |
| react.dev | suitecut | 504 | 86 | 1,488 | 7,900 | 130 |
| react.dev | agent-browser | 1,450 | 65 | 1,835 | 11,082 | 177 |
| react.dev | browser-use | 1,266 | 153 | 873 | – | 131 |

## Failures

- agent-browser wikipedia-hops: URL did not reach wikipedia\.org/wiki/Blink_\(browser_engine\); last https://en.wikipedia.org/wiki/Chromium_(web_browser)

## Retries

- suitecut github-hops: No link named /^Labels$/ in the snapshot
- agent-browser wikipedia-hops: No link named /^Chromium( \(web browser\))?$/ in the snapshot
- agent-browser python-docs-hops: No link named /^re — / in the snapshot
