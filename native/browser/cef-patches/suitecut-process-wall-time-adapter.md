# Isolated renderer-process wall-time adapter candidate

**Status: unapplied native adapter candidate.** This patch is separate from the
Chromium/CEF vendor bundle. It does not change the production browser, public
SDK, or `fullClockControl` capability. It does not deliver a page/context clock.

The [adapter patch](suitecut-process-wall-time-adapter.patch) applies to the
three exact native source files recorded in its
[manifest](suitecut-process-wall-time-adapter.json). Its backend dependency is
[the process wall-time extension](chromium156-process-wall-time.md), which still
needs a compiled, verified custom CEF/Chromium library. No public SDK methods are
added. The stock source files remain untouched.

## Concrete adapter behavior

The patch adds native FIFO actions for the actual backend's fixed wall time,
running wall-time rebase, and override clear. It uses the same `NativeVirtualTime`
physical-owner registry as installation and timer budgets. Each action requires
an installed native clock, preserves the original real deadline and generation,
and waits behind earlier physical work. It does not dispatch JavaScript or
replace Date, Temporal, performance, or timer functions.

Every action first sends the read-only
`Emulation.suiteCutClockCapabilitiesV1` request and requires actual revision `1`
and scope `rendererProcess` in the reply. A missing method, unexpected revision,
or scope mismatch rejects before a mutating request is sent. A successful probe
is deliberately not cached across calls or target lifetimes. Set/clear replies
must also acknowledge the same exact scope.

The candidate's private wire actions are:

- `setRendererProcessFixedTime`, with finite `timeMs` in the ECMAScript epoch
  range;
- `setRendererProcessSystemTime`, with the same epoch validation;
- `clearRendererProcessWallTime`, which restores the original virtual wall-time
  origin rather than host real time.

Each requires the explicit boolean `acknowledgeRendererProcessScope: true` and a
build compiled with `SUITECUT_VERIFIED_CEF156_PROCESS_WALL_TIME`. That macro is
absent from all production build recipes. These names intentionally distinguish
process controls from the unsupported public page/context clock operations.

Cancellation during the read-only probe retains its physical callback and
retires without dispatching a setter. Cancellation or deadline expiry after a
setter is submitted retains that callback, waits for its actual acknowledgement,
then sends a real native clear request. The next queued owner cannot start until
that clear is physically acknowledged. A missing cleanup acknowledgement keeps
the owner and fails the target closed after its existing one-second cleanup
limit. A failed mutating command is treated conservatively as a physical failure.
A clear resets the process override; it does not reconstruct a prior caller's
fixed/rebased value.

## Activation is still blocked

A renderer process can contain unrelated pages or contexts. Neither an explicit
scope acknowledgement nor a successful protocol probe creates isolation.
Automatic reset on navigation, inspector detach, crash, and all Source-close
paths still needs a real process-level lifecycle owner. The current Source
close fallback cannot prove that a still-live shared process was reset. Do not
enable the macro until those ownership and teardown rules are implemented and
verified with the custom library. The candidate removes the basic native
protocol/FIFO/physical-cancellation adapter work; it does not remove that
process-lifecycle blocker.

Full clock work also still needs context/frame/worker inheritance before author
execution, cross-process barriers, realtime-paced native scheduling, precise
at-most-once fast-forward, and controlled animation/idle callback delivery.
Unbounded `Emulation.setVirtualTimePolicy(policy: advance)` is not realtime
resume, and zero budget is not a due-callback flush. This candidate makes neither
substitution.

## Focused verification

The exact patch passed source applicability checks. Its modified native clock
controller compiled with `-std=c++20 -Wall -Wextra -Werror -O2` and passed actual
focused checks for all three dispatch paths, missing-protocol rejection,
stale acknowledgements, original generations, cancellation before/after setter
submission, cleanup acknowledgement ownership, deadline cleanup, cleanup
failure, and invalid epoch input. Source adapter syntax is checked with the
activation macro against the pinned stock CEF headers; this checks C++ adapter
syntax only. Vendor compilation, generated protocol compilation, real Date/
Temporal observations, and browser E2E/fuzz verification remain unrun.
