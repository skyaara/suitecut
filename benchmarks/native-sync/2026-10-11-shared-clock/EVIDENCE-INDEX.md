# Saved shared-clock evidence

Open `report.html` for the portable table/figures, or `report.md` for the complete narrative. `summary.csv` contains all valid decoded trials and labels final versus diagnostic/history cases; `summary.json` also includes failed harness attempts. Historical publication evidence is preserved separately under `benchmarks/publication/2026-10-10/`.

Final resolution trials are `runs/{720p60,1080p30,4k30}-{record,stream}`. `runs/soak-1080p30-stream` holds the full 30-minute stream and process/memory samples. `runs/controller-stall-traced-1080p30` is the additional healthy traced repeat; it does not erase the earlier severe interruption in the untraced controller test. Both recording and streaming are tested separately.

Each run retains its media, requested settings and actual output status (`result.json`), binary/resource budget (`runtime.json`), process samples (`resources.jsonl`), native/Node observations (`status.jsonl`), decoded marker and audio RMS arrays, probe metadata and analysis. Traced diagnostics additionally retain raw CEF and pre-encoder PCM, packet timestamps, and per-track encoder/mux timings. Old run names were preserved with explanatory suffixes; absolute paths in their original runtime records reflect the measured host, while media is present within each run directory.

`tests/` retains host/build/version/hash records, source-review files, the deliberately failing old-timing regression control, controller-stall observations and the CEF 152 candidate patch application check. The persistent-capture CEF patch is not compiled or enabled. `logs/` retains normal and sanitizer tests, build logs, full fuzz campaigns, decoder logs, reporting retries and failed/interrupted attempts. A reporting-only NumPy integer serialization error was corrected and affected trace analyses were rerun; the native binary was unchanged.

`fuzz/` retains corpora, failure-artifact directories, scope, results and summary. Each final campaign ran 600 seconds; earlier smoke/interrupted work is separate. Instrumentation covers SuiteCut native code, not all prebuilt Chromium/CEF or codec dependencies. A finite soak or empty sanitizer failure list is not a universal memory-safety certificate.

`source-final/` is the complete native/SDK source and relevant harness snapshot, including pins and build instructions. `executables/` retains measured/baseline/fuzz binaries for provenance, with dependency limits described in its README. The full CEF SDK and system/private runtime dependencies are not bundled. `tests/implementation-delta.patch` compares the preserved pre-change files with the final implementation.

The requested four-core CPU budget is enforced by quota/affinity. The host's kernel RAM limit is 16 GiB; the 8 GiB aggregate-RSS safeguard is sampled, not a strict kernel cap. GPU encoding and external-network/RTC behavior are outside this test. No new matched OBS performance comparison is claimed.

`MANIFEST.sha256` seals all regular payload files except itself. The archive and its checksum are saved under `/workspace/shared/` using the `suitecut-shared-clock-2026-10-11` prefix. Verify extracted payloads from this directory with `sha256sum -c MANIFEST.sha256`.
