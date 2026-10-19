# Atomic publication of frame reference groups

`SnapshotReferences::CommitBatch` publishes 1–32 installed frame captures as a
single serialized native transaction. Every ticket must be distinct, every frame
must be distinct, and every group must have the same original owner, generation
and deadline. Before changing existing references it validates every full native
document pin, installer receipt, latest ticket and final active group/node quota.

Only after that preflight does it retire all affected old publications and then
activate every new group. A valid parent/child quota swap therefore works in
either order. An invalid second ticket leaves both previous publications active.
Rejected staging groups retain real outstanding installer, borrow and release
ownership. Already-active tickets passed by mistake are never retired by the
rejection path. Original single-frame `Commit` behavior is unchanged.

The controlled test invokes the production registry directly. It covers atomic
replacement and rollback, empty-session main versus child-session destruction,
full document pin changes with unchanged numeric context IDs, quota swaps and
aggregate limits, duplicate/replayed tickets, held old borrows, and late actual
installer/release callbacks. It checks process-global accounting returns to zero.

This registry unit does not capture iframe contents or prove a browser API. The
following Source adapter must obtain each child frame ID from its actual iframe
backend node, capture under the same original operation deadline, validate and
format the whole merged observation, then call this transaction once.
