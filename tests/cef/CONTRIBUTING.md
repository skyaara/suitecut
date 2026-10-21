# Adding CEF regressions

1. Put application assertions in the appropriate `scenarios/` domain module.
   Import shared actions from `scenarios/helpers.mjs`; shared application state
   belongs in `fixtures/`. Register a new domain explicitly in `scenarios/index.mjs`.
2. Give every case a stable, unique ID, nonempty tags and a positive hard deadline
   of at most 1,800 seconds. Declare `ffmpeg`/`ffprobe` when required. Case selectors
   must each match an existing ID; a misspelling cannot silently reduce coverage.
3. Use real public SDK actions and local servers. Observe admission/ownership
   barriers before triggering races. Specify allowed outcomes and forbidden side
   effects; arbitrary timeout acceptance is not an assertion.
4. Add C++ workflows to `native/client/tests/integration/` and explicitly list their
   sources in its `CMakeLists.txt`. Put browser-free checks in `unit/` and test-only
   helpers in `support/`. Keep public SDK headers in `include/suitecut/` and private
   implementation in `src/`. Test code must not become part of the installed SDK.
5. Register legacy proofs and C++ cases in `manifests/`. Every entry declares one
   confined report file and a nonempty set of field predicates. Review assertion
   counts against the script/binary; do not generate a baseline from a failing run.
6. Run harness unit checks and the actual affected browser cases. Run the full
   suite before qualifying the changed package. Preserve failing evidence. The
   previous release report is historical evidence, not validation of new sources.

## Evidence contract

```json
{
  "file": "artifacts/results.json",
  "rules": [
    { "path": "ok", "equals": true },
    { "path": "checks", "minItems": 7 },
    { "path": "scenarios", "allTrue": "ok" }
  ]
}
```

Fields use dotted object paths. `equals` preserves JSON types: `1` and `"true"`
do not satisfy `true`. `minItems` requires assertion receipts, `allTrue` requires
explicit success on every child scenario, and `nonEmptyString: true` requires a
nonempty completion receipt. `allowedItems` limits declared unobserved coverage while also
accepting a run that exercises all those paths. Missing fields, unknown predicates, duplicate JSON
keys, non-finite numbers, escaped/symlinked external reports, and oversized reports
fail. An arbitrary JSON artifact cannot replace the declared report.

Failure/cleanup arrays cannot coexist with a passing result. Expected authored
page errors belong in case-specific observations and must have explicit assertions;
they are not universally classified as harness failures. The unowned-debugger-pause
proof explicitly declares its existing unobserved ordering boundary. This suite
still makes no exhaustive race, public website, portability, or sustained-FPS claim.

Input inventories are hashed before and after execution, including all nested
harness/scenario/manifest sources, SDK files and selected runtime binaries. An
added, deleted or changed input fails suite integrity. `plan.json`, JSON, HTML,
JUnit, logs and failure artifacts are retained. Repetition never replaces a failure.
