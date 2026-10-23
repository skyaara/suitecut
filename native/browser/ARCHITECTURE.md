# CEF runtime architecture

The CEF runtime is a separate native application. The standalone C/C++ client
SDK remains under `../client`; it does not include this private embedder API or
require CEF headers. The runtime uses one binary, one page registry and the same
physical shutdown barrier across all its implementation modules.

## Compiled modules

| Directory / object target                 | Responsibility                                                                                       |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `protocol/` / `suitecut-browser-protocol` | Packet values, bounded output queues, reply credits, writer thread and publication receipts          |
| `runtime/` / `suitecut-browser-runtime`   | Engine ownership, contexts, startup, command routing, deadlines, accepted work and physical shutdown |
| `page/` / `suitecut-browser-page`         | CEF page adapter, callbacks and page-scoped command implementations                                  |
| `process/` / `suitecut-browser-process`   | CEF application configuration, platform-neutral process dispatch and shutdown sequence               |
| `controllers/`                            | Private controller fragments, organized by domain and shared with focused regression fixtures        |
| `cmake/RuntimeModules.cmake`              | Explicit source inventories, compiler policy and optional private precompiled header                 |

`page/source.h` declares `Source`, the page lifetime owner. Its method bodies
live in ordinary `.cc` files: lifecycle, bootstrap, capture, navigation, DevTools,
events, frames, handles, input, operations, DOM, network and command dispatch.
`runtime/engine.h` declares the engine owner; implementation is divided into
initialization, lifecycle, commands, accessors and accepted work. The headers and
object targets are private implementation boundaries, not supported CEF-facing
SDKs or separately versioned shared libraries.

The older standalone controllers (network, media pipelines, storage, tracing,
contexts and other services) retain their existing `.h`/`.cc` implementations.
The extracted adapter modules call those controllers; they do not introduce a
parallel service registry or a second set of lifecycle state.

## Ownership and execution

- The engine owns the Source registry and context/page lifecycle. A Source holds
  a reference to that engine. Engine methods that need a complete Source type
  are implemented out of line; headers use forward declarations where possible.
- CEF callback and control state remains on the UI thread. Existing explicit
  physical continuations, generation checks, deadlines and completion receipts
  retain their original owners.
- Audio stream errors can originate on the capture thread. Their Source-retaining
  task decides ownership on UI: active failures remain fatal, a cancelable
  beforeunload prompt is still active, and committed page close retires late
  capture errors. The engine lifetime guard precedes access to its state.
- The writer owns its existing single I/O thread and bounded queues. Splitting
  files does not create workers or introduce per-command threads.
- Consume stored one-shot callbacks before invocation, including through
  reentrant teardown. Use `std::exchange(callback, {})`; a moved-from small
  `std::function` can remain callable with libc++, so a move alone cannot serve
  as the completion flag.
- The Source lifetime receipt remains its first field so it retires after every
  other field. Its constructor, member order, teardown and callback guards are
  preserved.
- The engine still waits for an empty Source registry and physical work to
  drain. The deferred shutdown fence commits one quit request after callbacks
  unwind. `ReleaseSource`, `CefShutdown` and writer retirement keep their order.
- The Linux buffer-free subprocess dispatch and browser-main stack-protector
  separation remain in `process/main.cc`. Platform entry points remain in
  `main_linux.cc` and `main_mac.mm`.

## Controller fragments

The private `.inc` files are canonical adapters into their enclosing owner's
state, mostly in-class or command-body fragments. The audio-failure callback
also shares its out-of-line definition with a controlled scheduling fixture.
Some focused tests instantiate these same fragments with controlled protocol
receipts and callback schedules. They are grouped under `controllers/` by
domain rather than copied into separate production and test implementations.
Each fragment documents its enclosing owner. They are not independently
compiled modules or installed headers.

Prefer a normal `.h`/`.cc` controller for new independently owned behavior.
Use a fragment only when it needs the enclosing owner's private state and a
focused test deliberately exercises that integration seam. Keep ownership in
the existing engine/Source until a separate lifecycle change is designed and
tested; moving code alone is not a reason to split physical ownership.

## Building and checking changes

Build from the pinned CEF and libyuv distributions as described in the native
build/release instructions. Keep compiler jobs bounded:

```sh
cmake --build /absolute/engine-build --target suitecut-browser --parallel 1
python3 native/browser/tools/check-layout.py
python3 native/browser/tools/check-without-pch.py --build /absolute/engine-build --output /absolute/new/report
```

The page, runtime and process object targets reuse one private precompiled
header to avoid repeatedly parsing CEF and the controller adapters. The protocol
module builds with its own direct includes. Correctness must not depend on the
precompiled header: `SUITECUT_BROWSER_PRECOMPILED_HEADERS=OFF` disables it, and
the syntax checker compiles every module source without any forced PCH includes.
Enable `CMAKE_EXPORT_COMPILE_COMMANDS` for that checker.

Run the native component checks relevant to the change and the complete
[CEF regression package](../../tests/cef/README.md). The full suite must retain
its strict report contracts, deadlines, process cleanup and source/runtime
hashes. Record failures; do not retry them into a passing qualification.
This layout does not enable CI or change the fixed macOS signing policy.

Sealed, unapplied candidates under `cef-patches/` retain their historical base
paths and hashes. This split does not activate or rebase those candidates.
Rebase and requalify a candidate against the current controller paths before
enabling a custom CEF feature.
