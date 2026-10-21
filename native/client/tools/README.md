# Native client real-browser verification

`tests/integration/real_browser.cpp` is a standalone C++ application linked only against the
native client library and ordinary C++/OS libraries. It launches the existing CEF
engine directly. Node is used only as a separate comparison application.

The shared `fixture.html` checks Unicode form values, genuine trusted pointer
input, checkbox and native select controls, shadow-root roles, engine-issued
observation references, actual PNG bytes, and context/page lifetimes. It includes
an input target that moves during hover and an overlay that must prevent a stale
pointer press. It checks observable page outcomes rather than successful command
acknowledgements alone.

Build the standalone client as documented in `native/client/README.md`, then run
from the repository root:

```sh
.suitecut/native-client/build/native-client-real-browser \
  --browser /absolute/path/to/suitecut-browser \
  --fixture native/client/tools/fixture.html \
  --iterations 30 --cycles 50 \
  --output .suitecut/native-client/cpp/results.json
```

Use the matched runner only when no other browser benchmarks or builds are
active. The existing supervisor independently checks descendant cleanup:

```sh
flock -n .suitecut/external-native-perf/measurement.lock \
  .suitecut/memory-deep/runtime/native-runner/proof-tools/suitecut-proof-supervisor \
  --report .suitecut/native-client/comparison-supervisor.json \
  --timeout-ms 180000 --cleanup-grace-ms 1000 --physical-ms 6000 -- \
  python3 native/client/tools/compare.py \
  --browser /absolute/path/to/suitecut-browser \
  --iterations 30 --cycles 50 --repetitions 2 \
  --output .suitecut/native-client/comparison
```

The order alternates C++, Node, Node, C++. Both applications drive the same CEF
binary at 1280×720, DPR 1, 60 capture FPS, without recording video. The operations
are small evaluation, verified 256 KiB evaluation, dense DOM query count,
role-based click, and input fill. Endurance creates, controls and explicitly
closes isolated pages and contexts. C++ consumes its public event queue between
cycles; Node receives events through its existing SDK dispatcher.

The runner hashes the client executable, shared fixture, current emitted Node
SDK modules, engine executable, CEF library, and native client sources before and
after execution. It saves every application report and stdout/stderr receipt.

Client CPU is the CPU of the application process, including its threads, and
excludes Chromium. Client PSS/RSS/FD samples are taken between workload phases;
Node explicitly collects garbage before these samples. Timing results include
independent result validation. Parent-observed executable-to-ready time includes
language runtime initialization and SDK imports; the separate launch-to-ready
measurement starts inside each application.

For a separate diagnostic resource pass, add `--sample-interval 0.25` and choose a
new output directory. This samples client and descendant PSS independently,
records PID/start identities, and labels action times as perturbed by profiling.
Zombie descendants are reported separately for the outer supervisor to reap.
Do not compare profiled times with the default unprofiled action measurements.
Bounded stable memory samples can identify retention trends; they do not prove
that all possible workloads are free of leaks.
