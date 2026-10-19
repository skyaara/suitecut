# Native tracing and coverage

SuiteCut records browser actions and captures in C++. The Node API supplies typed
options, caller locations and output paths. Browser contexts and API request
contexts own separate trace sessions.

```ts
import { createNativePage, launchNativeBrowser } from 'suitecut/native'

const source = await launchNativeBrowser({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
  tracesDir: '/absolute/path/to/live-journals',
})
try {
  const page = createNativePage(source)
  await page.context().tracing.start({
    name: 'checkout',
    screenshots: true,
    snapshots: { dom: true, screen: true, aria: true },
    sources: true,
  })
  await page.goto('https://example.com')
  await page.context().tracing.stop({ path: '/absolute/path/to/checkout.zip' })
} finally {
  await source.close()
}
```

`screenshots` captures native screencast frames. `snapshots: true` records DOM and
network snapshots; it does not request action PNGs or accessibility forests.
Use the snapshot object to select `dom`, `screen` and `aria` independently.
`sources` includes the actual files observed in caller stacks.

`startChunk` starts another chunk within the current session. `stopChunk` exports
or ends that chunk while keeping the session. Groups accept an optional caller
location and appear as actual before/after records. The object returned by
`start` or `startChunk` supports asynchronous disposal of that exact session or
chunk; disposing an older object cannot discard its replacement.

For API requests, use the independent `context.request.tracing` object (or the
tracing object on a standalone native request context). Browser tracing does not
silently include requests made by an independent API context.

## Live files and finalized archives

When `tracesDir` is configured, native code publishes `<name>.trace`,
`<name>.network` and `<name>.stacks` while a chunk is active. The first change
schedules a flush after 100 ms. Later changes coalesce behind that deadline;
console and network events also schedule flushing when no browser action follows.
Background work and filesystem latency can delay publication beyond that window.
Busy writers retain one pending dirty state rather than a queue of snapshots.

Each chunk keeps one resource directory. Exact captured resource bytes publish
before any journal references them, and the stack file publishes before the
action journal. Each file replacement is atomic; the group of files is not a
filesystem transaction. `stop` and `stopChunk` wait for accepted physical work and
the final writer within the caller's original native deadline. A requested ZIP
is a closed archive with its own resource names and exact captured bytes.

Cancellation retires logical ownership immediately and retains accepted physical
work until it actually completes. Already published live files remain on disk.
An actual filesystem failure is reported at finalization. It does not create an
unbounded stream of retries. Trace byte and writer limits can reject further
capture; bounded traces are not guaranteed complete session histories.

## Current-document coverage

`page.coverage.startJSCoverage()` / `stopJSCoverage()` return genuine Chromium
script sources and measured UTF-16 ranges. `startCSSCoverage()` /
`stopCSSCoverage()` return actual stylesheet sources and used ranges. Native code
owns domain ACKs, source reads, deadlines and cleanup. JavaScript supports
`reportAnonymousScripts` and `useBlockCoverage`.

Coverage includes author contexts in the main document, same-process frames and
actual out-of-process iframe targets. New iframe targets acknowledge their
native coverage domains before author execution resumes. JavaScript coverage
includes document-owned dedicated workers when `includeWorkers: true` is set;
the default remains document/frame coverage. Existing workers and future workers
use their actual Inspector sessions, and future worker domain ACKs precede author
execution. This includes dedicated workers created by OOPIFs and nested workers.
Shared/service workers belong to browser contexts and are outside this page
coverage scope; worker CSS coverage is unsupported. Script IDs are genuine
target-local V8 IDs and can repeat between targets. Entries retain their actual
URLs and sources; SDK evaluation wrapper scripts can also appear in results.

The supported slice resets at main-document navigation. `resetOnNavigation: false`
rejects explicitly because old-document measurements cannot yet be retained
reliably through renderer teardown. An iframe target that disappears before its
engine counters can be collected fails the recording with an explicit error.
Navigation during collection also fails instead of combining partial documents.
Actual renderer recovery is verified with new main/iframe sources and counts.

Capture admits at most 32 actual root/iframe/opted-in worker targets, with a shared 48 MiB budget
for retained sources, measurements and output. Results are bounded to 2 MiB and
2048 entries. Native command queues retain their original deadlines. A canceled
start retains actual pending acknowledgements and domain cleanup; a subsequent
start is admitted once that cleanup has genuinely finished. Capture limits do
not impose the same target limit on ordinary page activity after coverage stops.
