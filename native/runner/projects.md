# Native serial project planning

`ProjectGraph` validates up to128 uniquely named declarations and4096 dependency
edges before callbacks run. Selection uses bounded whole-name `*` patterns with ASCII case-insensitive
matching; other UTF8 bytes match literally. Without selectors, only
`default_enabled` ordinary projects are roots; dependencies and associated
teardown projects are included even when their default flag is false. Cycles,
unknown/duplicate names and dependencies on teardown projects are rejected.
Teardown projects cannot themselves declare dependencies or another teardown in
this bounded slice.

`SerialProjectPlan` admits ready ordinary projects in declaration order. A failed
or interrupted dependency blocks dependent declarations without inventing test
attempts or worker PIDs. Teardown runs once after every selected ordinary project
has settled, including failed/blocked dependents. A global deadline, failure-limit
or interruption stop prevents any new project author admission, including teardown;
it does not create a fresh cleanup timeout.

Project completion and worker exit are separate native receipts. A logical result
retains its physical fence until the exact admitted project generation and PID
receive `WorkerExited`. Retries can replace a worker only after its actual exit.
Stale generations and different PIDs cannot release that fence.

The current component is validated independently with optimized and
ASan/UBSan/LSan controls, including reverse declaration-order failure propagation,
shared teardown, non-default dependency selection, worker replacement and global
stop. The component alone does not establish public project support; the adapter
below connects it to actual callbacks and reports. Browser/device
presets, locks, Unicode case folding and full project parity remain open. Native
project worker parallelism is described below.

Behavioral references are the pinned public1.64 project contract (`dependencies`,
`default`, `teardown`, `metadata`, `name`) and PW-34 in
[the parity specification](../../docs/playwright-feature-parity.md). No external
runner implementation is used.

## Public serial declarations

The following adapter connects the planner to the native runner. For example:

```js
import { defineNativeSuite } from 'suitecut/native'

export default defineNativeSuite({
  projects: [
    {
      name: 'setup', default: false, teardown: 'cleanup',
      tests: [{ title: 'prepare', run: async () => { /* author setup */ } }],
    },
    {
      name: 'smoke', dependencies: ['setup'], metadata: { environment: 'local' },
      tests: [{ title: 'check', run: async (_, info) => {
        console.log(info.project.name, info.project.metadata.environment)
      } }],
    },
    {
      name: 'cleanup', default: false,
      tests: [{ title: 'release', run: async () => { /* author cleanup */ } }],
    },
  ],
})
```

`--project smoke` selects the named root plus setup and cleanup. Repeated
`--project PATTERN` selectors form a union; duplicate/overlapping selectors are
idempotent. An unmatched literal fails with the available names. Unmatched
patterns are ignored if another selector matched; an entirely unmatched union
fails. Declaration order determines which
ready project runs first. A project can contain ordered `entries` and hooks in
place of flat tests. Suite-level fixture declarations are shared definitions;
their actual worker/test values belong to each project's fresh OS worker.
`info.project` is present on project bodies, hooks and fixture setup callbacks.
Its name and JSON metadata are snapshots of the native-admitted context.

The host emits project declarations and callback IDs only. C++ validates the full
graph, selects memberships and sends a project context before any callback.
Metadata and dependency declaration bytes remain pinned across replacement
workers. The same `NativeSuiteDeadline` spans the entire run. Remaining failure
capacity and actual attempt generations carry across project plans; retries keep
the existing physical worker exit fence. New project admission requires confirmed
worker, descendant and artifact-writer retirement.

The JSON report adds `projects` with native IDs, names, original metadata,
dependency/teardown IDs, original declaration IDs, selected declaration IDs,
primary/dependency/teardown selection roles, `dependenciesIgnored`, actual worker
indices and terminal
states. A blocked or otherwise unadmitted test has an `unrun` declaration, never a
fabricated attempt, generation or PID. Test title paths keep their actual declared
suite ancestry; the project identity is separate.

Bounds are128 projects,4096 total test declarations/dependency edges,256 UTF8
bytes per project name and16KiB JSON object metadata (native nesting depth16,
up to4096 metadata values). Project selectors are bounded to128 patterns of256
UTF8 bytes. A bitset automaton owns wildcard matching in C++; regex punctuation
other than `*` is literal. Non-ASCII case folding, duplicate configured names,
per-project fixture overrides, browser/device presets and parallel tests within one project
remain unsupported.

## Primary filtering and required support

Native project selection composes original `--test-id` identities, `.only` and
balanced contiguous `--shard CURRENT/TOTAL` partitions. First identify directly
selected primary declarations; then shard their original declaration order.
Required dependency and teardown projects run all their declarations for every
nonempty shard, including declarations excluded by primary IDs or focus. A setup
project which is itself directly selected and has no selected dependent remains
a primary project and is filtered normally. Empty primary shards and filtered-away
roots admit no orphan setup or teardown work. Empty dependency projects remain
valid. An explicitly selected empty root preserves its existing unfiltered hook
behavior, but an empty focused suite selects no tests and no orphan support.
Focus in excluded projects cannot filter the selected primary projects, including
focus on an empty suite. `--forbid-only` still rejects focus anywhere in the
catalog before callback admission.

`--no-deps` (SDK `noDeps`) disables dependency/teardown expansion, ordering and
failure gates. Implicit default roots continue to exclude configured teardown projects; an
explicitly selected teardown name is a direct primary project. Selected project
tests still obey IDs/focus/shards; each selected
project has its own actual worker. Declared dependencies/teardown remain recorded
in JSON beside `dependenciesIgnored: true`. The complete graph is still validated
before callbacks. The native suite deadline, max-failure policy, retry nonce and
physical worker/artifact fence are shared across every project without reset.
This uses the native runner's existing flat-test shard contract; external
file/group sharding is not implemented.

The additional project-selector public checker proves selector union and case,
full setup/teardown tests on both primary shards, test-ID-before-shard order,
empty/excluded roots, no-deps expansion and gates, project focus isolation, and
actual original IDs/generations/PIDs. Optimized and sanitizer component controls
also compare the bounded wildcard automaton against an independent small-language
recognizer. Pinned public oracles are `config.spec.ts`, `deps.spec.ts`, and public
CLI/global-setup docs at606ecd8b34bac100db32561b6278ffd76e5eb3cd; they are read-only
behavioral references, not copied runner implementations or unchanged-suite proof.

`--workers N` (SDK `workers`) selects a native pool of1..8 project workers. Tests
within a project remain serial. One native coordinator retains the original
suite deadline, failure policy and global identity/quotas. The first actual
catalog worker is reused; exact host/raw callback/file and conservative shared
descendant batch fences precede dependency/teardown advancement. See
[the native parallel contract](project_parallel_plan.md) for physical ownership
and scoped actual overlap/stop proofs.

The authored public CLI checker passes11 groups with real process exit and JSON
receipts: dependency/default/teardown selection, fresh fixture workers and
metadata (including nested Unicode/isolated UTF16), project/global/nested hook
snapshots and exact title ancestry, native steps and committed
artifact writer exits, non-default selection, stable order, failed/expected-failure
dependencies, retry replacement, suite-wide failure limits, the original global
stop, cycle rejection and actual metadata drift. This is scoped native evidence,
not an unchanged external runner suite or full project parity.
