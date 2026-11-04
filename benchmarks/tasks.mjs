// SPDX-License-Identifier: MIT
// What every tool is measured on. Sites are public pages that need no sign-in;
// tasks are short read-only flows an agent does, driven by role and name from
// each tool's own snapshot, so no tool gets selectors the others lack.

/** Pages for launch, load, snapshot and screenshot timing. */
export const sites = [
  'https://news.ycombinator.com/',
  'https://en.wikipedia.org/wiki/Web_browser',
  'https://developer.mozilla.org/en-US/docs/Web/HTML',
  'https://www.bbc.com/news',
  'https://github.com/microsoft/playwright',
  'https://docs.python.org/3/library/asyncio.html',
  'https://react.dev/learn',
]

/**
 * A step names its target by ARIA role and a case-insensitive name pattern.
 * `click` and `fill` act on the first match in the snapshot; `press` sends a key
 * to the focused element. A task is done when the page URL matches `done`.
 */
export const tasks = [
  {
    name: 'hn-open-comments',
    goal: 'Open the comments of the top Hacker News story',
    url: 'https://news.ycombinator.com/',
    steps: [{ click: { role: 'link', name: '^\\d+\\s*comments?$' } }],
    done: 'news\\.ycombinator\\.com/item\\?id=\\d+',
  },
  {
    name: 'wikipedia-search',
    goal: 'Search Wikipedia for "Web browser" and reach the article',
    url: 'https://en.wikipedia.org/wiki/Main_Page',
    steps: [
      { fill: { role: 'searchbox|combobox|textbox', name: 'search' }, text: 'Web browser' },
      { press: 'Enter' },
    ],
    done: 'wikipedia\\.org/wiki/Web_browser',
  },
  {
    name: 'mdn-open-reference',
    goal: 'From the MDN HTML page, open the HTML elements reference',
    url: 'https://developer.mozilla.org/en-US/docs/Web/HTML',
    steps: [{ click: { role: 'link', name: '^(HTML )?elements( reference)?$' } }],
    done: 'developer\\.mozilla\\.org/en-US/docs/Web/HTML/(Reference/)?Element',
  },
  {
    name: 'github-open-issues',
    goal: 'Open the Issues tab of microsoft/playwright',
    url: 'https://github.com/microsoft/playwright',
    steps: [{ click: { role: 'link', name: '^Issues' } }],
    done: 'github\\.com/microsoft/playwright/issues',
  },
  {
    name: 'bbc-open-sport',
    goal: 'Open BBC Sport from the BBC News navigation',
    url: 'https://www.bbc.com/news',
    steps: [{ click: { role: 'link', name: '^Sport$' } }],
    done: 'bbc\\.(com|co\\.uk)/sport',
  },
  // Longer flows: three pages, a fresh snapshot on each, as an agent browses.
  {
    name: 'wikipedia-hops',
    goal: 'Follow Wikipedia links from Web browser to Google Chrome, Chromium and Blink',
    url: 'https://en.wikipedia.org/wiki/Web_browser',
    steps: [
      { click: { role: 'link', name: '^Google Chrome$' }, until: 'wiki/Google_Chrome' },
      { click: { role: 'link', name: '^Chromium( \\(web browser\\))?$' }, until: 'wiki/Chromium' },
      { click: { role: 'link', name: '^Blink$' } },
    ],
    done: 'wikipedia\\.org/wiki/Blink_\\(browser_engine\\)',
  },
  {
    name: 'mdn-hops',
    goal: 'From MDN HTML, open the elements reference, then the <a> element',
    url: 'https://developer.mozilla.org/en-US/docs/Web/HTML',
    steps: [
      { click: { role: 'link', name: '^(HTML )?elements( reference)?$' }, until: 'Elements?$' },
      { click: { role: 'link', name: '^<a>$' } },
    ],
    done: 'developer\\.mozilla\\.org/en-US/docs/Web/HTML/(Reference/)?Elements?/a$',
  },
  {
    name: 'github-hops',
    goal: 'Open the Issues tab of microsoft/playwright, then its Labels',
    url: 'https://github.com/microsoft/playwright',
    steps: [
      { click: { role: 'link', name: '^Issues' }, until: 'playwright/issues' },
      { click: { role: 'link', name: '^Labels$' } },
    ],
    done: 'github\\.com/microsoft/playwright/labels',
  },
  {
    name: 'python-docs-hops',
    goal: 'From the Python docs, open the library reference, then asyncio',
    url: 'https://docs.python.org/3/',
    steps: [
      { click: { role: 'link', name: '^Library reference$' }, until: 'library/(index\\.html)?$' },
      { click: { role: 'link', name: '^asyncio — ' } },
    ],
    done: 'docs\\.python\\.org/3/library/asyncio\\.html',
  },
  // react.dev and GitHub navigate in the page, without loading a new document.
  {
    name: 'react-hops',
    goal: 'From the React tutorial, open Thinking in React, then Adding Interactivity',
    url: 'https://react.dev/learn',
    steps: [
      { click: { role: 'link', name: '^Thinking in React$' }, until: 'learn/thinking-in-react' },
      { click: { role: 'link', name: '^Adding Interactivity$' } },
    ],
    done: 'react\\.dev/learn/adding-interactivity',
  },
  {
    name: 'github-open-file',
    goal: 'Open package.json in the microsoft/playwright file list',
    url: 'https://github.com/microsoft/playwright',
    steps: [{ click: { role: 'link', name: '^package\\.json(, \\(File\\))?$' } }],
    done: 'github\\.com/microsoft/playwright/blob/[^/]+/package\\.json$',
  },
]
