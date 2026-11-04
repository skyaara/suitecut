# Browser agent benchmark

2026-11-04T02:30:26.744Z, darwin 27.0.0, Apple M4 (10 cores, 16 GB). suitecut 2.1.0 (6f08324-dirty), agent-browser 0.37.1, browser-use 0.13.14 on Google Chrome 156.0.8078.80. 3 cold rounds per tool; 1-minute load average at round start 8–32.

Snapshot and screenshot times are medians of 3 calls per page, then of all pages and rounds. Tokens are characters / 4 of the text an agent reads: suitecut `ariaSnapshot({ mode: 'ai', interactive: true })`, agent-browser `snapshot -i`, browser-use `llm_representation()`.

## Summary

| Tool | Launch ms | Snapshot ms | Snapshot tokens (5 pages) | Screenshot ms | Tasks passed | Retries | Task ms | Task tokens (8 tasks) |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| suitecut | 1,507 | 140 | 16,145 | 80 | 23/24 | 4 | 1,354 | 66,567 |
| agent-browser | 2,147 | 75 | 22,498 | 79 | 24/24 | 1 | 1,183 | 103,376 |
| browser-use | 1,364 | 242 | 14,362 | 73 | 24/24 | 0 | 2,142 | 35,946 |

## Tasks

| Task | Goal | suitecut: passed / ms / tokens | agent-browser: passed / ms / tokens | browser-use: passed / ms / tokens |
|---|---|--:|--:|--:|
| hn-open-comments | Open the comments of the top Hacker News story | 3/3 / 725 / 2,167 | 3/3 / 881 / 3,410 | 3/3 / 1,290 / 3,258 |
| wikipedia-search | Search Wikipedia for "Web browser" and reach the article | 3/3 / 1,185 / 2,983 | 3/3 / 1,104 / 2,756 | 3/3 / 1,415 / 3,015 |
| mdn-open-reference | From the MDN HTML page, open the HTML elements reference | 3/3 / 705 / 1,708 | 3/3 / 539 / 1,515 | 3/3 / 1,174 / 1,497 |
| github-open-issues | Open the Issues tab of microsoft/playwright | 3/3 / 2,045 / 3,216 | 3/3 / 1,337 / 5,700 | 3/3 / 2,093 / 4,458 |
| bbc-open-sport | Open BBC Sport from the BBC News navigation | 3/3 / 2,678 / 7,124 | 3/3 / 2,272 / 6,767 | 3/3 / 2,742 / 972 |
| wikipedia-hops | Follow Wikipedia links from Web browser to Google Chrome, Chromium and Blink | 3/3 / 3,233 / 37,133 | 3/3 / 3,272 / 63,741 | 3/3 / 7,142 / 10,795 |
| mdn-hops | From MDN HTML, open the elements reference, then the <a> element | 3/3 / 1,284 / 6,436 | 3/3 / 858 / 13,762 | 3/3 / 2,330 / 3,194 |
| github-hops | Open the Issues tab of microsoft/playwright, then its Labels | 2/3 / 3,686 / 5,802 | 3/3 / 2,409 / 5,726 | 3/3 / 3,886 / 8,758 |

## Pages

| Page | Tool | Load ms | Snapshot ms | Snapshot tokens | Full snapshot tokens | Screenshot ms |
|---|---|--:|--:|--:|--:|--:|
| news.ycombinator.com | suitecut | 1,427 | 127 | 2,110 | 9,491 | 65 |
| news.ycombinator.com | agent-browser | 1,394 | 54 | 3,410 | 6,896 | 74 |
| news.ycombinator.com | browser-use | 1,176 | 164 | 3,259 | – | 80 |
| en.wikipedia.org | suitecut | 682 | 207 | 5,161 | 19,916 | 96 |
| en.wikipedia.org | agent-browser | 948 | 150 | 5,015 | 21,955 | 81 |
| en.wikipedia.org | browser-use | 852 | 508 | 2,777 | – | 85 |
| developer.mozilla.org | suitecut | 1,194 | 108 | 1,801 | 7,463 | 72 |
| developer.mozilla.org | agent-browser | 739 | 45 | 1,611 | 6,092 | 83 |
| developer.mozilla.org | browser-use | 368 | 166 | 1,756 | – | 65 |
| www.bbc.com | suitecut | 2,681 | 193 | 3,577 | 7,454 | 80 |
| www.bbc.com | agent-browser | 2,915 | 87 | 6,762 | 10,679 | 66 |
| www.bbc.com | browser-use | 2,259 | 256 | 1,185 | – | 81 |
| github.com | suitecut | 1,657 | 202 | 3,497 | 13,044 | 90 |
| github.com | agent-browser | 4,013 | 94 | 5,700 | 10,897 | 67 |
| github.com | browser-use | 1,039 | 297 | 5,386 | – | 50 |

## Failures

- suitecut github-hops: URL did not reach github\.com/microsoft/playwright/labels; last https://github.com/microsoft/playwright/issues

## Retries

- suitecut bbc-open-sport: The element this snapshot ref named is no longer in the page; take a new snapshot
- suitecut github-hops: click: Error: Element is not attached to the DOM
- agent-browser github-hops: agent-browser click @e89: 1 ✗ Element '@e89' is covered by <span#_R_tllld85_--label> at its click point, so the input would land on that element instead. Dismiss or interact with the covering element first (it is often a dialog, banner, or sticky header).
