# Native renderer-process wall clock candidate

Status: **isolated, uncompiled pinned Chromium patch**. The stock CEF binary,
SuiteCut clock adapter and all runtime capability flags remain unchanged. This
candidate is not full browser-clock implementation or a delivered fixed-Date
feature. No generated protocol code, modified Chromium or rebuilt CEF library
has been compiled or run.

The [patch](chromium156-process-wall-time.patch) targets Chromium
`156.0.8078.17`, selected by CEF
`47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`. Its [manifest](chromium156-process-wall-time.json)
records exact source URLs, base/output SHA-256 values and patch hash. Only five
small pinned files were fetched. `git apply --check`, application and exact
output-hash comparison passed against those files; these checks establish patch
applicability, not correctness in a running engine.

The extension adds native wall-time control to the existing
`ProcessTimeOverrideCoordinator`. It changes `base::Time::Now()` while leaving
`base::TimeTicks`, virtual-time budgets, timer queues and native pause fences
unchanged. An atomic fixed value freezes wall time while timers may run through
native budgets. Running mode publishes an offset from the existing virtual wall
clock, so later monotonic advances increment wall time normally. This is not
real-time resume. Clear removes the offset/fixed value and restores the original
virtual-time wall origin, not the host clock.

Writers use the coordinator's existing mutex. Readers add atomic loads without
acquiring that mutex; the fixed-to-running transition publishes the offset before
clearing the fixed value. Native time values that saturate or hit the reserved
`base::Time()` null sentinel reject. A fresh process virtual-time installation
clears both extra fields. No JavaScript Date, timer or performance implementation
is replaced.

The versioned Inspector methods are defined in the actual Emulation PDL and
implemented in its pinned Blink agent:

- `Emulation.suiteCutClockCapabilitiesV1` returns revision 1 and scope
  `rendererProcess`.
- `Emulation.suiteCutSetWallTimeV1({timeMs, fixed})` requires that the calling
  agent has already installed native virtual time, validates the epoch value,
  and returns the same explicit process scope after the actual coordinator write.
- `Emulation.suiteCutClearWallTimeV1` clears only the wall-time adjustment after
  the same installed-clock check.

These are custom-library methods, not stock protocol capabilities. There is no
SuiteCut Source or SDK activation in this change. A separate
[unapplied native adapter candidate](suitecut-process-wall-time-adapter.md) now
provides explicit process-scoped protocol probing, native FIFO dispatch and
physical cancellation cleanup; its process-lifecycle activation blocker remains.

**Process scope is essential.** Pinned Chromium already coordinates virtual time
across participating schedulers in a renderer process. Other pages and contexts
sharing that process can observe the changed wall clock; this is not page or
context isolation. The candidate does not install clocks in workers or other
renderer processes, synchronize OOPIFs, or protect unrelated pages from a process
clock. The change persists until cleared or process teardown; agent detachment
alone is not a clear operation.

Per-context clock isolation and lifecycle ownership must be resolved before a
public adapter can expose this candidate. Remaining native clock work includes
worker/OOPIF installation before author execution, cross-target barriers,
real-time-paced advancement, navigation ownership, at-most-once fast-forward,
and controlled animation/idle callback delivery. Public `advance` without a
budget cannot substitute for real-time resume, and zero budget cannot substitute
for flushing due callbacks.

Build this only on the existing pinned custom CEF build host, alongside the
other isolated vendor patches. Before production activation, regenerate the
protocol bindings, compile Chromium/CEF, add the real native ownership adapter,
and prove wall-time changes against actual Date/Temporal observations while
monotonic time and timer callbacks keep their promised behavior. The current
workspace intentionally performs none of those large builds or end-to-end/fuzz
runs.
