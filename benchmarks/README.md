# Benchmarks

suitecut against [agent-browser](https://github.com/vercel-labs/agent-browser) and
[browser-use](https://github.com/browser-use/browser-use), on the same public pages and the
same tasks, with one command. Every script is here; nothing is measured anywhere else.

```bash
node benchmarks/run.mjs --rounds 3
```

It writes `benchmarks/results/results.json` (every run, every call), `results.md` (the
tables) and `summary.json` (the medians the website shows).

## What is measured

Each round starts every tool cold: a new browser and a new, empty profile. The order of the
tools rotates each round, so none always runs first. Every tool runs headless at 1280×720 and
a device scale factor of 1.

**Pages** ([`tasks.mjs`](tasks.mjs)): Hacker News, Wikipedia "Web browser", MDN HTML, BBC
News, GitHub `microsoft/playwright`, the Python `asyncio` docs and the React tutorial. For
each page the tool loads it (waiting for the
`load` event), waits 1.5 s, then takes 3 snapshots and 3 viewport screenshots. The tables
use the median call.

- **Launch**: from starting the tool to a page that answers.
- **Snapshot**: the text an agent reads to choose what to do. Tokens are characters / 4.
  - suitecut: `page.ariaSnapshot({ mode: 'ai', interactive: true })`; the full
    `ariaSnapshot({ mode: 'ai' })` is measured too.
  - agent-browser: `snapshot -i`, which its documentation recommends to agents; the full
    `snapshot` is measured too.
  - browser-use: `get_browser_state_summary()` and `dom_state.llm_representation()`, the
    text it sends its model. By default it leaves out elements more than 1000 px outside the
    viewport, so it is not a whole-page snapshot.
- **Screenshot**: a viewport PNG.

**Tasks** ([`tasks.mjs`](tasks.mjs)): eleven read-only flows. Six are one or two steps, such
as opening the comments of the top Hacker News story, searching Wikipedia and reaching the
article, or opening `package.json` from a GitHub file list. Five cross several pages:

- Wikipedia from Web browser through Google Chrome and Chromium to Blink
- MDN from HTML through the elements reference to `<a>`
- GitHub from microsoft/playwright through Issues to Labels
- the Python docs through the library reference to `asyncio`
- the React tutorial through Thinking in React to Adding Interactivity

GitHub and react.dev change pages without loading a new document, as many apps do. A step
that loads another page waits for its URL (`until`) before the next snapshot. Before each step
the adapter reads a snapshot and picks the first element whose ARIA role and name match the
step, then clicks or fills it by the tool's own ref. Every tool gets 5 s per action
(`ACTION_MS` in [`adapters/common.mjs`](adapters/common.mjs)); a step whose action fails
takes a new snapshot and tries once more, as an agent would, and counts as a retry. A task
passes when the page URL reaches the goal within 20 s. Task time runs from navigation to the goal; task tokens add up every
snapshot read along the way.

**CPU and memory**: while a run goes, [`run.mjs`](run.mjs) samples every process it started
every 0.5 s with `ps`: the adapter, the browser and all its helper processes, and
agent-browser's daemon, which detaches from the command that starts it. CPU is the sum of
each process's CPU time; memory is the peak of their summed resident sizes, which counts
pages that processes share more than once, for every tool alike.

No LLM is involved: a model would add its own latency, cost and randomness, and the same
choices are made for every tool. What is measured is what the browser layer costs an agent.

## Fairness

- Every tool gets the same pages, tasks, viewport and timeouts, and only the role and name it
  shows in its own snapshot.
- browser-use runs with its default extensions off, one of which hides cookie banners, so all
  tools load the same page.
- agent-browser times include starting its CLI for each command, as an agent calls it.
- Real sites change from minute to minute and network time varies. Compare tools within one
  run and look at `load1` (the machine's load average) in `results.json`.

## Running it

Build suitecut (`pnpm build`, and the native browser as in
[docs/building.md](../docs/building.md)), then install the others:

```bash
npm install -g agent-browser && agent-browser install
```

```bash
python3 -m venv .venv-bench && .venv-bench/bin/pip install browser-use
```

Environment variables:

| Variable             | Default                                                                                     |
| -------------------- | ------------------------------------------------------------------------------------------- |
| `SUITECUT_BROWSER`   | `.suitecut/chrome-ui-2026-10-31/build/suitecut-browser.app/Contents/MacOS/suitecut-browser` |
| `AGENT_BROWSER`      | `agent-browser` on `PATH`                                                                   |
| `BROWSER_USE_PYTHON` | `python3` (use the venv's `python`)                                                         |
| `CHROME`             | `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome` (for browser-use)            |

`--tools suitecut,agent-browser` runs a subset, and `--out` writes somewhere else. The run
stops if less than 2 GB of disk is free.

## Files

- [`run.mjs`](run.mjs) runs every round and writes `results.json`.
- [`tasks.mjs`](tasks.mjs) lists the pages and tasks.
- [`schema.mjs`](schema.mjs) defines what adapters receive and print.
- [`adapters/`](adapters) has one adapter per tool. Each runs one cold session and prints
  one JSON line.
- [`report.mjs`](report.mjs) turns `results.json` into `results.md` and `summary.json`.
