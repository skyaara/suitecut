# Browser agent benchmark

2026-11-03T23:56:27.635Z, darwin 27.0.0, Apple M4 (10 cores, 16 GB). suitecut 2.1.0 (5b52e1f-dirty), agent-browser 0.37.1, browser-use 0.13.14 on Google Chrome 156.0.8078.80. 3 cold rounds per tool; 1-minute load average at round start 5–42.

Snapshot and screenshot times are medians of 3 calls per page, then of all pages and rounds. Tokens are characters / 4 of the text an agent reads: suitecut `ariaSnapshot({ mode: 'ai', interactive: true })`, agent-browser `snapshot -i`, browser-use `llm_representation()`.

## Summary

| Tool | Launch ms | Snapshot ms | Snapshot tokens (5 pages) | Screenshot ms | Tasks passed | Retries | Task ms | Task tokens (5 tasks) |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| suitecut | 2,019 | 153 | 16,033 | 167 | 15/15 | 2 | 1,427 | 17,259 |
| agent-browser | 1,928 | 70 | 22,557 | 69 | 15/15 | 0 | 1,393 | 19,946 |
| browser-use | 1,784 | 341 | 14,226 | 83 | 15/15 | 0 | 1,901 | 13,215 |

## Tasks

| Task | Goal | suitecut: passed / ms / tokens | agent-browser: passed / ms / tokens | browser-use: passed / ms / tokens |
|---|---|--:|--:|--:|
| hn-open-comments | Open the comments of the top Hacker News story | 3/3 / 1,218 / 2,162 | 3/3 / 963 / 3,400 | 3/3 / 1,419 / 3,261 |
| wikipedia-search | Search Wikipedia for "Web browser" and reach the article | 3/3 / 1,927 / 2,982 | 3/3 / 1,955 / 2,485 | 3/3 / 1,273 / 3,043 |
| mdn-open-reference | From the MDN HTML page, open the HTML elements reference | 3/3 / 1,134 / 1,708 | 3/3 / 660 / 1,515 | 3/3 / 1,172 / 1,497 |
| github-open-issues | Open the Issues tab of microsoft/playwright | 3/3 / 3,412 / 3,216 | 3/3 / 2,651 / 5,695 | 3/3 / 3,132 / 4,456 |
| bbc-open-sport | Open BBC Sport from the BBC News navigation | 3/3 / 2,106 / 7,192 | 3/3 / 4,314 / 6,851 | 3/3 / 4,312 / 958 |

## Pages

| Page | Tool | Load ms | Snapshot ms | Snapshot tokens | Full snapshot tokens | Screenshot ms |
|---|---|--:|--:|--:|--:|--:|
| news.ycombinator.com | suitecut | 1,376 | 151 | 2,105 | 9,435 | 167 |
| news.ycombinator.com | agent-browser | 1,411 | 70 | 3,400 | 6,885 | 92 |
| news.ycombinator.com | browser-use | 1,361 | 221 | 3,261 | – | 108 |
| en.wikipedia.org | suitecut | 1,083 | 265 | 5,161 | 19,916 | 165 |
| en.wikipedia.org | agent-browser | 1,068 | 71 | 5,015 | 21,955 | 54 |
| en.wikipedia.org | browser-use | 1,367 | 360 | 2,776 | – | 83 |
| developer.mozilla.org | suitecut | 980 | 118 | 1,799 | 7,463 | 147 |
| developer.mozilla.org | agent-browser | 641 | 65 | 1,596 | 6,061 | 106 |
| developer.mozilla.org | browser-use | 572 | 218 | 1,755 | – | 94 |
| www.bbc.com | suitecut | 5,169 | 2,127 | 3,474 | 7,121 | 304 |
| www.bbc.com | agent-browser | 2,438 | 113 | 6,851 | 10,706 | 69 |
| www.bbc.com | browser-use | 3,385 | 368 | 1,048 | – | 74 |
| github.com | suitecut | 3,388 | 257 | 3,494 | 13,040 | 340 |
| github.com | agent-browser | 10,227 | 89 | 5,695 | 10,888 | 67 |
| github.com | browser-use | 2,353 | 527 | 5,386 | – | 72 |

## Failures

- suitecut: www.bbc.com: page.goto: Timeout 45000ms exceeded.
navigating to "https://www.bbc.com/news", waiting until "load"
