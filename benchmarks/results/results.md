# Browser agent benchmark

2026-11-04T07:32:33.264Z, darwin 27.0.0, Apple M4 (10 cores, 16 GB). suitecut 2.1.0 (fd4c280-dirty), agent-browser 0.37.1, browser-use 0.13.14 on Google Chrome 156.0.8078.80. 3 cold rounds per tool; 1-minute load average at round start 5–16.

CPU and memory cover every process a run starts (browser, renderers, GPU, driver, and agent-browser's daemon), sampled every 0.5 s; CPU is the median run's total, memory the peak of the summed resident sizes, which counts shared pages more than once.

Snapshot and screenshot times are medians of 3 calls per page, then of all pages and rounds. Tokens are characters / 4 of the text an agent reads: suitecut `ariaSnapshot({ mode: 'ai', interactive: true })`, agent-browser `snapshot -i`, browser-use `llm_representation()`.

## Summary

| Tool | Launch ms | Snapshot ms | Snapshot tokens (7 pages) | Screenshot ms | Tasks passed | Retries | Task ms | Task tokens (11 tasks) | CPU s per run | Peak memory MB |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| suitecut | 899 | 79 | 19,861 | 53 | 33/33 | 4 | 1,129 | 100,654 | 56 | 2,816 |
| agent-browser | 1,466 | 61 | 26,683 | 64 | 33/33 | 0 | 1,362 | 123,772 | 57 | 3,633 |
| browser-use | 1,330 | 292 | 16,596 | 75 | 33/33 | 0 | 2,274 | 45,012 | 67 | 2,587 |

## Tasks

| Task | Goal | suitecut: passed / ms / tokens | agent-browser: passed / ms / tokens | browser-use: passed / ms / tokens |
|---|---|--:|--:|--:|
| hn-open-comments | Open the comments of the top Hacker News story | 3/3 / 691 / 2,184 | 3/3 / 895 / 3,449 | 3/3 / 1,837 / 3,290 |
| wikipedia-search | Search Wikipedia for "Web browser" and reach the article | 3/3 / 1,111 / 2,909 | 3/3 / 1,224 / 2,756 | 3/3 / 1,457 / 3,019 |
| mdn-open-reference | From the MDN HTML page, open the HTML elements reference | 3/3 / 365 / 1,708 | 3/3 / 674 / 1,515 | 3/3 / 1,186 / 1,498 |
| github-open-issues | Open the Issues tab of microsoft/playwright | 3/3 / 1,188 / 2,453 | 3/3 / 2,042 / 5,695 | 3/3 / 2,123 / 4,458 |
| bbc-open-sport | Open BBC Sport from the BBC News navigation | 3/3 / 2,651 / 8,146 | 3/3 / 2,700 / 7,529 | 3/3 / 3,022 / 1,110 |
| wikipedia-hops | Follow Wikipedia links from Web browser to Google Chrome, Chromium and Blink | 3/3 / 2,367 / 57,056 | 3/3 / 3,246 / 63,741 | 3/3 / 7,594 / 10,795 |
| mdn-hops | From MDN HTML, open the elements reference, then the <a> element | 3/3 / 1,017 / 6,436 | 3/3 / 853 / 13,768 | 3/3 / 2,291 / 3,195 |
| github-hops | Open the Issues tab of microsoft/playwright, then its Labels | 3/3 / 3,946 / 7,066 | 3/3 / 2,574 / 7,999 | 3/3 / 3,175 / 8,209 |
| python-docs-hops | From the Python docs, open the library reference, then the re module | 3/3 / 714 / 7,281 | 3/3 / 802 / 6,908 | 3/3 / 2,683 / 3,120 |
| react-hops | From the React tutorial, open Thinking in React, then Adding Interactivity | 3/3 / 1,155 / 2,907 | 3/3 / 1,362 / 4,718 | 3/3 / 2,779 / 1,861 |
| github-open-file | Open package.json in the microsoft/playwright file list | 3/3 / 2,008 / 2,510 | 3/3 / 1,715 / 5,695 | 3/3 / 1,588 / 4,459 |

## Pages

| Page | Tool | Load ms | Snapshot ms | Snapshot tokens | Full snapshot tokens | Screenshot ms |
|---|---|--:|--:|--:|--:|--:|
| news.ycombinator.com | suitecut | 2,231 | 74 | 2,127 | 9,529 | 50 |
| news.ycombinator.com | agent-browser | 1,779 | 49 | 3,449 | 6,977 | 76 |
| news.ycombinator.com | browser-use | 1,545 | 179 | 3,290 | – | 84 |
| en.wikipedia.org | suitecut | 1,507 | 150 | 5,161 | 19,916 | 79 |
| en.wikipedia.org | agent-browser | 1,753 | 101 | 5,015 | 21,955 | 66 |
| en.wikipedia.org | browser-use | 678 | 511 | 2,776 | – | 85 |
| developer.mozilla.org | suitecut | 436 | 38 | 1,792 | 7,455 | 51 |
| developer.mozilla.org | agent-browser | 1,723 | 25 | 1,603 | 6,075 | 57 |
| developer.mozilla.org | browser-use | 366 | 184 | 1,760 | – | 84 |
| www.bbc.com | suitecut | 2,611 | 62 | 4,110 | 8,356 | 103 |
| www.bbc.com | agent-browser | 6,550 | 64 | 7,524 | 11,719 | 75 |
| www.bbc.com | browser-use | 2,778 | 292 | 1,345 | – | 97 |
| github.com | suitecut | 1,289 | 113 | 3,493 | 13,041 | 65 |
| github.com | agent-browser | 1,423 | 111 | 5,696 | 10,888 | 66 |
| github.com | browser-use | 947 | 504 | 5,388 | – | 74 |
| docs.python.org | suitecut | 288 | 30 | 1,691 | 4,404 | 49 |
| docs.python.org | agent-browser | 634 | 41 | 1,599 | 4,198 | 61 |
| docs.python.org | browser-use | 296 | 131 | 1,175 | – | 66 |
| react.dev | suitecut | 656 | 148 | 1,488 | 7,866 | 53 |
| react.dev | agent-browser | 1,133 | 225 | 1,798 | 10,994 | 64 |
| react.dev | browser-use | 542 | 394 | 862 | – | 56 |

## Failures

None.

## Retries

- suitecut bbc-open-sport: The element this snapshot ref named is no longer in the page; take a new snapshot
- suitecut github-hops: The element this snapshot ref named is no longer in the page; take a new snapshot
