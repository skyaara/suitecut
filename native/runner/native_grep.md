# Native grep selection (work in progress)

Contract evidence: `/tmp/suitecut-native-grep-oracle-v1/contract.json`, SHA256
`10c3d34b5223e1e6fdf9fef2f021f8c14580f86d5563be178c6fd9dc7b50b30c`.
This records 52 genuine Playwright 1.64 public CLI observations and 33 checks,
with original public tests pinned to commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`. No external implementation was
inspected or copied. Each process ran under the actual native supervisor; every
observed process slot was absent after the batch, including zombie states.

The CLI compiles bare patterns with `gi`. Slash-delimited patterns recognize
only `g` and `i` suffixes; a recognized literal without flags is case-sensitive.
A suffix containing another letter remains part of the bare pattern, as in the
stock CLI. Repeated include and exclude options each use their last value.
Matching begins fresh for each title; `g` retains no selection state.

Stock composes project, relative file path, declared suite titles and test title
with spaces. Each declaring suite/test's tags follow that component immediately:
`Project One nested/alpha.spec.mjs outer suite @inherited @same test AA @leaf @same`.
Tags in public `titlePath()` are absent, so the path cannot be reconstructed from
that array alone; declaration components and tags must remain distinct metadata.

The native matcher is an authored Thompson automaton over UTF16 code units.
It supports literals, dot, anchors, classes/ranges and negation, alternation,
groups/noncapturing groups, word boundaries, `* + ? {m,n}` including lazy suffixes,
hex/UTF16 escapes and `d D s S w W` classes. Boolean matching makes capture values
and greedy order immaterial. Non-Unicode dot consumes one UTF16 unit and excludes
LF, CR, line separator and paragraph separator. Lone surrogate escapes remain
literal units. ASCII ignore-case follows ECMAScript non-Unicode behavior;
nonASCII case-insensitive literal/class patterns are explicitly rejected until
full legacy Unicode canonicalization is implemented. ASCII patterns can match
nonASCII titles without lossy conversion.

Lookaround, backreferences, named groups, legacy octal/identity/control escapes,
and unsupported syntax fail explicitly before test admission. This is a bounded
subset, not a claim of complete JavaScript RegExp or public config RegExp-array
compatibility. Pattern length is bounded at4096 UTF16 units, AST at4096 nodes,
group depth64, repeats256, automaton2048 states/16384 ranges, title32768 units.
Each actual NFA operation consumes a shared finite work budget. Exhaustion is a
selection failure, never a fabricated test result or asynchronous cancellation.

`--test-directory` / SDK `testDirectory` optionally supplies the real canonical
root for this one suite's relative file component. It defaults to the actual
suite parent. C++ canonicalizes both existing paths, requires a real directory,
and rejects a suite outside that root (including a symlink escaping it).
The existing SuiteFileOwner still owns and validates the actual file identity.
This option performs no multi-file discovery. Listing rows expose the exact
native-composed `grepTitle`; the original display title and declaration IDs stay
stable across list/run/filter changes.
