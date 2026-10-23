# Native repeated group process verification

`check-native-runner-repeat-groups.py` uses the import-safe
`native_repeat_process_proof.py`. It never imports or executes the existing
14-case repeat checker. The latter fixtures, assertions and budgets remain
unchanged. Matching native group activation and emitted SDK are required;
source preparation is not an actual execution or parity result.

The new registered `native-runner-repeat-groups-public` runs nine independent
native CLI invocations. Every invocation uses the production native supervisor
with its own45s execution,1s cleanup and10s physical bound. The aggregate
CTest guard is556s:9×56s+52s orchestration. Output directories are unique;
original inputs are hashed and never copied into a large snapshot. The
`--prepare-only` option only writes fixtures and input pins, launching no child.

1. Two declarations with fullyParallel and repeat2, shard1/2 select only A0/B0.
   Both real bodies wait for an external release. The observer checks two live
   Node PID/start identities and their actual native Runner parent before
   release. This proves concurrency within one repeat without assuming
   cross-repeat completion order. The observer always releases and joins the
   real supervisor before reporting a negative.
2. The unsharded equivalent runs all four logical tests in four real parallel
   groups/worker-fixture lifetimes. It imposes no cross-repeat completion order.
3. Default A/B groups share their actual worker fixture and PID within each
   repeat, with two separate group lifetimes.
4. A serial A/B group fails B on repeat0/retry0, then retries the whole group on
   a fresh native worker. Per-test logical IDs remain stable through retry and
   distinct across repeats. The checks require only within-repeat retry order.
5. Two real files share the original workers2 pool, with each file declaring the complete consistent setup/body/cleanup graph.
   Actual setup dependency and cleanup project barriers span both files. Native worker waitpid times must precede
   dependent host admission; the final shared physical budget must be zero.
6. A genuine failing file and held neighbor exercise one global maxFailures1.
   Cancellation must really reach the neighbor; later repeats remain native
   selection-only rows with no source or invented attempt.
7. Two held bodies consume one original global2000ms deadline. Remaining repeat
   groups do not get renewed clocks or fabricated execution receipts.
8. Named project declarations0/1/3 remain visible through the native-issued
   `PROJECT_REPEAT_CONTEXT`/`TestInfo.project.repeatEach`, while CLI repeat2
   affects primary execution counts only.

9. One direct count-one suite with configured root0 and named project declarations0/1
   proves the native declared-count sidecar independently of repeated actor markers.
   Both callbacks retain native repeat index0 and original local public IDs; exactly
   two project execution hosts/imports occur, with no promoted collection host.

For every repeated scope the checker verifies top-level `repeatGroups` partition coverage,
compact source/segment/test group references, original local attempt IDs, opaque
native logical-ID equality with real callback beacons, generation/output-ID
uniqueness, worker exit0 and stderr0, real output-writer retirement, and final
shared native budget zero. Selected unstarted tests remain `sourceId:null`,
`nativeSelectionOnly:true`, `didNotRun`, with no attempt indices.

`peakObservedHosts` measures actual host admission through waitpid capture. It
is not presented as the complete reservation interval through every descendant
or Source fence. Final native budget and physical receipts independently establish
retirement. A native parent check and PID/start observation are observational
proof inputs, not authority that can admit or retire native work.
