# Current-document script inventory

`session.scripts(cursor?, options)` reads metadata from actual
`Debugger.scriptParsed` receipts in the explicit debugger's current root
execution context. It returns at most 64 records and 512KiB, including its native
cursor envelope. URLs, optional hashes/languages and isolated UTF-16 code units
are preserved. No script source bytes are cloned; `scriptSource(actualId)` still
performs its own actual source request.

Each enumeration pins the exact native debugger session, Source epoch, unique
root context identity and actual context inventory revision. An actual metadata
change or native attestation change makes its cursors stale and requires a new
call without a cursor. Identical repeated receipts and unrelated contexts do not
change that revision. Starting another enumeration invalidates the preceding
one. Ordering follows the actual script IDs bytewise, rather than their URLs.

Cursor positions retain only native identity strings. The process permits 256
positions across at most 16 explicit sessions. Replaying a cursor reuses its
existing successor. Closing or invalidating the exact session frees these
logical positions without claiming that an Inspector receipt retired. Original
operation deadlines apply throughout record projection and publication.

`attestedNative` is read-only information: true requires the existing actual
native FunctionLocation attestation. False means ownership is unproven. Neither
value grants mutation authority; breakpoint installation independently checks
current native script identity and excludes genuinely attested native IDs.
Author sourceURL/hash matches never establish native ownership.

Controlled verification passes 10 cursor groups, 22 inventory groups and 30
integrated controller groups under optimized and ASan/UBSan/LSan builds. The SDK
passes 35 units, emit, full type checking and scoped lint. Prior failed fixtures
and the corrected output/input aliasing failure are retained in separate proof
directories.

The exact sealed six-group browser checker passes on b074f3e2, using its sealed
SDK/native binary without overlays. A fresh root that has never started coverage
lists 64 actual records, replays its cursor, and retrieves all 90 served author
scripts by exact ID. Actual author breakpoint mutation, native attestation
denial, fail-stale revision after a new real script, and an isolated UTF-16
sourceURL all pass. Cursor/session/physical/developer-receipt counters return to
zero; the real helper exits 0/null with clean stderr and no remaining exact
executable processes. All 2002 inputs and 230 assets remain unchanged.

Immutable receipt:
`/tmp/suitecut-artifacts-b074f3e2-inventory6-v1/runtime-index.json`, SHA256
`1b4d7265f5eb434a045e3ba877553c43bc5ce4633d68d1c12da42ff0330fb59b`.
The same epoch's independent paused-evaluation8 regression also passes. Neither
proof activates the following child-frame ownership primitives.
