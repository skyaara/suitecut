# SuiteCut 2.1.0 release evidence

- `report.html` and `report.md`: readable verification and limits.
- `summary.json`: final check outcomes and fresh fuzz statistics.
- `fuzz/`: both evolving corpora, empty failure-artifact directories and outcomes.
- `logs/`: release/ASan/TSan tests, fuzz campaigns, full prepublish gates and installed-browser checks; earlier failed environment attempt preserved.
- `release-attempts.json`: complete release-check attempt history.
- `tested-binaries/` and `tested-dependencies/`: actual tested native executables and instrumented FFmpeg libraries; use the saved build/runtime library setup, not these files as a standalone browser distribution.
- `toolchain/`, `host-and-scope.json`, `native-source*.json`: flags, versions, scope and source identity.
- `source-snapshot.tar.gz`: SDK/native source, authoring assets, tests and build files. Model weights are supplied in the separate npm tarball; CEF/runtime dependencies follow their pins.
- `packages.json`: tested package file inventories and digests.
- `engine-e2e/`, `installed-native/`, `external-browser/`: fresh browser verification evidence.
- `sealed-baseline-reuse.json`, `earlier-memory-attribution.json`, and memory charts: relationship to the earlier unchanged native benchmark build, with descriptive process memory trends.
- `MANIFEST.sha256`: all evidence payload hashes; the manifest itself is excluded.

Earlier sealed raw benchmark archives remain separate and unchanged. No publication, push, tag or GitHub release was performed.
