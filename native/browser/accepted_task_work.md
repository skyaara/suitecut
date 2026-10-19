# Native posted task object receipts

This bounded process-local C++ component covers SuiteCut-owned task objects. It
is not installed into production CEF tasks in this commit and grants no Engine
idle, child exit or browser reuse. It does not count renderer timers/listeners
or opaque CEF scheduling as pending automation merely because they exist.

Native task construction retains a receipt. Execute retains the actual callback
stack. A returned callback remains charged while CEF still owns its task and
captured state. Declare the receipt member BEFORE the captured callback so the
callback/resource destructors finish before its receipt releases. Actual task
object destruction without execution is distinct from successful execution.
There is no cancellation/timeout method that can discharge a living task.

A future barrier may exclude only its exact executing task of the explicit
BarrierCoordinator kind. Ordinary work, queued/foreign/stale barrier tickets,
other callbacks and returned-but-retained tasks do not qualify. Callback unwind,
capacity exhaustion and unsupported bookkeeping deny authority. Exact instance
and monotonic task identity prevent old tickets resolving a replacement owner.

The 20 native controls prove C++ ownership/stack/captured-destructor policy under
optimized and executed ASan/UBSan/LSan builds; they do not execute CEF or prove
Source hooks. Nine concrete production task classes are identified for following
Source integration: Task, WorkerBrokerTask, UtilityTask, ContextPolicyCloseTask,
SuiteCutInitDeadlineTask, HTTPTask, DownloadTask, TraceTask and StorageTask.
The screenshot browser-probe's separate process must be joined through its actual
parent-owned physical child receipts, not this process-local counter.

Actual Source bindings and physical controller registries remain separate work.
Combine their attested owner receipts with reader/UI, raw Inspector and original
stdout publication cuts; never reinterpret this component alone as global idle.
