# CEF regression package

Runs the actual embedded browser through the JavaScript and C++ SDKs. This is a
local test package with standard-library Python orchestration and Node scenarios.
It is not a published npm package and needs no additional package installation.

```text
cef/
├── CMakeLists.txt       # Local CTest entry point
├── __main__.py          # python3 tests/cef
├── harness/             # Catalogue, contracts, scheduling, process ownership, reports
├── fixtures/            # Test-owned HTTP application and deterministic gates
├── scenarios/           # Application, frames, network, races, artifacts, capture, recording
├── manifests/           # Proof/C++ inventories, deadlines and exact evidence contracts
└── unit/                # Harness/signing tests and false-green negative controls
```

```sh
python3 tests/cef --list
python3 -m unittest discover -s tests/cef/unit -p 'test_*.py'
python3 tests/cef --engine /absolute/path/to/suitecut-browser --profile smoke
```

`scripts/run-cef-regressions.py` remains a compatibility CLI. Both commands invoke
the same implementation. Full runs require compiled C++ workflows and media tools;
see [the execution guide](../../docs/cef-regressions.md). The C++ workflows live
in `native/client/tests/integration`; the reusable fixture code is in `support`.

The runner requires each declared report, success predicate and minimum assertion
inventory. Exit zero alone cannot pass a test. Every repetition counts, cleanup
failures remain failures, and the execution plan must match completed identities.
Workers default to one and cannot exceed four. CI is not configured here.

See [CONTRIBUTING.md](CONTRIBUTING.md) for adding cases and evidence contracts.
