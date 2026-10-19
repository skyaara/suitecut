# Native report merging contract

`suitecut-merge-reports` merges SuiteCut native runner JSON version 1 into a
source-qualified JSON report, with optional standalone HTML and JUnit views.
It does not start a worker or execute a reporter callback.

```
suitecut-merge-reports --output merged.json --junit merged.xml --html merged.html \
  --expected-inputs 2 \
  --input shard-1 /results/shard-1/results.json \
  --input shard-2 /results/shard-2/results.json
```

Each `--input` has an explicit unique source ID. The identity of a merged test is
`(sourceId, original testId)`. Generations, worker indices, PIDs, steps, projects,
files and artifacts stay local to their original source. Equal project names,
titles, test IDs, PIDs or generations in different sources are allowed. A retry
from one source is never attached to a test in another source.

JSON `sources[].report` contains the complete original JSON document, copied
verbatim, including original escaped UTF16, errors, binary worker streams,
lifecycle receipts, project metadata, file/group evidence and any shard metadata.
`tests[]` is a normalized terminal view with source ID, original test ID, title,
outcome and indices into that source's original attempt array. Aggregate summary
uses the existing native terminal policy, including serial replay, skipped and
unrun declarations. Unrun declarations have no invented attempts. This format
has its own `kind`; it is not passed off as an ordinary native execution report.

`summary.runFailed`, `summary.failedSources` and each source's
`runFailureReasons` expose unexpected tests, unrun declarations, global errors,
interrupted attempts, nonzero/signaled worker exits, unknown worker exits and failed/interrupted/blocked/unrun terminal projects or groups.
Expected failure tests and successful ordinary retries retain native outcome
semantics. `--fail-on-run-failure` returns 1 after publishing a valid merge with
failed source evidence. By default valid merges return 0. Invalid options,
missing/corrupt/incompatible inputs and publication failures return 2. A source
failure is not a merge parsing failure.

Inputs must be distinct regular files, with distinct source IDs. Duplicate paths,
symlink/hardlink aliases, duplicate JSON keys (including escaped aliases), invalid
UTF8/JSON, missing required native fields, incompatible versions, misleading
summary counts/duration, invalid retries/generations/workers/byte streams,
foreign core ownership and invalid committed attachment records are rejected.
The merger revalidates terminal native DTOs and core ownership; it preserves rich
lifecycle evidence without replaying or claiming a new runtime validation of
all original physical operations. Optional lifecycle sections are structurally
checked and remain intact. It cannot establish authenticity of an input report.
Limits are 128 inputs, 128 MiB combined input, 500,000 JSON values per source,
64 levels of JSON nesting and the native atomic writer's 128 MiB output limit
per file. `--expected-inputs N` detects omitted inputs against the supplied count;
it does not infer a shard plan or detect a shard that was never declared.

Committed attachments remain their original absolute references; no artifact is
copied, relocated, deleted or relabeled as freshly committed. By default each
reference gets merge-time `artifactAvailability` metadata:
`matchesRecordedIdentity`, `unavailable`, `identityMismatch` or `unreadable`.
Inaccessible foreign path ancestors and symlink loops remain `unreadable`
metadata in default reference mode. Input/output path canonicalization failures
still reject the merge, and available path/inode collision checks still protect
committed artifacts. Matching means a current regular file has the recorded device/inode/length; it
is not a content hash or a new writer receipt. Missing files from another machine
remain explicit and do not turn otherwise valid execution evidence into a failed
run. `--artifacts require` rejects every unavailable or mismatched reference.
Declared references without a committed attachment keep only their original
metadata; they never acquire file ownership or availability receipts.

JUnit produces one suite per source and one case per native terminal test,
retains every actual attempt as a lossless original JSON property, and includes
original rich evidence and binary worker streams in suite properties. The
`native.originalReportJSON` suite property retains the complete original source
JSON as text, including schema extensions the merger does not interpret; its
existing XML-invalid scalar hex fallback retains exact original bytes. Source
attachment view selections/history remain unchanged. The HTML artifact section
labels all committed artifacts as history, independently of current view selection.
Source run failure evidence gets a clearly labeled synthetic reporting error case; its text
states that it is not an executed test or lifecycle callback. Final test status
uses native serial/retry policy. XML display sanitization has explicit counts;
JSON properties preserve escaped UTF16. If literal XML-invalid UTF8 scalars require display replacement, an additional `originalPropertyJSONUTF8Hex` property keeps the exact serialized property bytes; hex decoding then JSON decoding recovers the original value. HTML escapes all authored
content, includes expandable original attempts and complete source documents,
and has no scripts, remote resources or active artifact links.

Each output uses the existing owned atomic file writer. Existing outputs survive
validation and pre-commit write failures, and only owned temporaries are removed.
Output paths may not overlap one another, any input report or a committed
artifact. Multiple output files commit individually; a later output IO failure
can leave earlier requested formats published, with an explicit exit 2.

The independent decoder proof `scripts/check-native-report-merge.py` has 41 control
groups. It uses native
report DTO fixtures and a genuine native attachment writer/child exit. It checks
UTF16/raw bytes and annotation hex spelling, actual artifact preservation, duplicate
identities, ordinary retry versus serial interruption classification, native serial
terminal policy, source/project/shard metadata, XML counts and attempts, explicit
absent/replaced artifacts, failure statuses, corrupt inputs, path alias protection
and actual EACCES/ELOOP reference controls, hardlink collision protection and
an actual directory publication fault. Fixtures are labeled as controlled DTO
input; no body execution is claimed. Each proof run keeps a fresh directory and
receipt instead of deleting earlier proof files.

The native repeat coordinator can provide an immutable `NativeRepeatSelection`
to the directory report. Its selected slices, rather than a blanket declaration
mask, define `(actual file, global project, repeatEachIndex, original local ID)`
coverage. Each source maps to exactly one selected slice. Actual source attempts
and retry numbers remain unchanged, and every source outcome, including unrun
rows, must belong to that slice. A different repeat cannot satisfy its coverage.
The directory declaration IDs remain the unique union of selected declarations;
they are not the number of logical repeated executions.

Repeat projections retain `declaredRepeatEach`, `effectiveRepeatEach`, and actual
`executions` separately: an admitted raw count of zero still schedules one
execution at index zero. The directory `repeatSelection` lists native logical
IDs and selected slices; source and normalized test rows expose their repeat
context. JUnit properties and HTML expose the same mapping without changing
original source JSON, attachment ownership, outputs, or retry classification.

Selected executions without a source outcome contribute to merged tests and
`didNotRun`. Their normalized rows explicitly have `sourceId: null`,
`nativeSelectionOnly: true`, and no attempt indices. JUnit labels these as skipped
native selections and HTML labels the missing outcome; these projections do not
invent sources, attempts, worker PIDs or execution receipts. Listing exposes
`selected` logical rows and creates no execution outcomes. Reports without a
repeat selection retain their previous JSON, JUnit and HTML representation.

An explicit native repeat declaration or override can also produce optional
`repeatConfiguration` metadata in an ordinary source report. It preserves raw
root/project/override values and native resolved declared/effective/execution
counts, including explicit zero with one actual execution. Absence retains the
previous report bytes. This metadata is not an actor or execution receipt.
The merger decodes and validates its typed counts; for repeated directory
sources it additionally checks the actual collected local project and admitted
slice counts/role. JUnit and HTML retain the optional configuration facts.

When native repeated execution has a selected group partition, directory
`repeatGroups` records its exact native group index, slice, mode, original suite
ID and selected local IDs once. Segment, source and normalized test rows carry
only compact `repeatGroup` context. Their logical test IDs remain unchanged.
Multiple sources can share a repeat slice only through distinct groups in that
same immutable partition. Every source outcome, including unrun rows, must stay
inside its exact group, and group coverage must cover each selected slice once.
Unstarted groups retain only native selection metadata and missing outcomes;
they receive no source, worker, attempt or retirement receipt.
