# SDK release candidates — 2026-10-19

Linux release candidates are prepared for the native C++20 SDK, ABI-v1 C API,
native CLI, `suitecut`, `@suitecut/browser-control`, and the eight audio packages.
No package, Git tag, or release has been published. The exact archives and
SHA-256 manifest are in `.suitecut/release-readiness-20261019/`.

## Validation

| Gate | Result |
| --- | --- |
| JavaScript | 1,604 tests across 199 files pass on Node 22.23.3 and 24.19.0; type checking, lint and formatting pass |
| Native client | 16 Release and 16 sanitizer targets pass |
| Native JSON/packet fuzz | 200,000 sanitizer iterations pass |
| Native observation/diff fuzz | 40,000 forests / 1,050,139 nodes pass |
| Native session IPC fuzz | 10,000 cases pass; stable 12 descriptors and 2 threads |
| Engine component fuzz | 1,000,000 framing/credit cases, 1,000,000 scheduler iterations, 30,000 memory-boundary iterations pass |
| Each TypeScript SDK | 200,000 lifecycle operations, 100,000 value cases, routing and packet campaigns pass |
| Installed native consumers | Static/shared C++ and C ABI; C-only shared consumer; real CEF, Chrome and Firefox pass |
| Installed native CLI | Release and sanitizer workflows on all three browsers pass |
| npm artifacts | Ten tarballs pass fresh consumer imports/CLI and audio package contracts |
| Installed browser-control | Trusted input, resizing, PNG decode, and CEF viewer takeover/resolutions pass |
| Installed SuiteCut | Display-free native recording with page audio passes |
| Dependencies | Frozen lockfile accepted; production and development audit: zero advisories |

Fuzz campaigns use fixed seeds and bounded resources. Sanitizer coverage applies
to the client and selected engine components, not the prebuilt CEF/Chrome/Firefox
libraries. Audio packages were built and their contracts tested; synthesis with
every external model was not exercised. This run does not claim exhaustive input
coverage or whole-browser leak freedom.

The process-lifetime negative test intentionally kills a SIGTERM-resistant
orphan. Its PID/start identity and SIGKILL are checked against the supervisor
receipt. Firefox utility-process SIGTERM exits during normal shutdown are also
recorded. These are explicit lifecycle outcomes, not silently discarded failures;
all owned descendants are retired and unexpected signals remain failures.

## Fixes made before packaging

- Retained the receiver for optional routed WebSocket payload cleanup, preserving
  the original append failure and issuing one staging abort; regression tests
  cover custom cleanup and the default cleanup path.
- Enabled explicit leak checks for sanitizer session workers that exit with
  `_exit` after releasing browser resources.
- Added observation/diff and malformed session IPC fuzz coverage.
- Updated Sharp and vulnerable transitive runtime/development dependencies.
- Repaired test type errors, generated-output lint scope, and formatting issues;
  regenerated canonical native formatter headers from their authored source.
- Moved runner contract data into its source distribution so npm packages remain
  self-contained without publishing the development test directory.
- Made the installed recording gate explicitly headless and able to select an
  exact release manifest without replacing older release pointers.
- Included license texts in every npm tarball and added a package gate for them.
- Added CI gates for Node 22/24 and native static/shared/sanitizer builds. The
  workflow has not been dispatched remotely; the results above are local proofs.

## Distribution limits

The prebuilt native candidates require Linux x86_64, glibc >=2.38 and
GLIBCXX >=3.4.32 for the CLI (the shared library requires >=3.4.31), plus
OpenSSL 3 and ZLIB. Browser runtimes remain separate.
The source archive supports building with another compatible Linux toolchain.
Node packages declare Node >=22; the full unit suite and installed three-browser
package smoke pass on Node 22.23.3 and 24.19.0. Heavy JavaScript fuzz campaigns
used Node 24.19.0 on Linux.

Windows/macOS release binaries, other architectures, and the custom CEF
patch/codec library need their respective build environments and qualification.
Existing backend capability limits remain documented; these SDK artifacts do
not imply every CEF extension or codec is present. The working tree has not
been committed as part of this task; the artifact hashes identify the exact
candidate bytes. Follow `RELEASING.md` for maintainer publication and tagging.

## Additional native full-runtime candidate

The thirteen SDK/npm archives above remain unchanged. A separate fourteenth
archive now combines the installed static native SDK/C ABI/CLI, native recorder,
SuiteCut engine, CEF 156 beta and resources, and pinned FFmpeg/ffprobe:

`.suitecut/full-runtime-20261019/package/SuiteCutRuntime-0.1.0-linux-x86_64.tar.gz`

The archive is 306,580,934 bytes (about 293 MiB), with SHA-256
`0746b858c3a75363d4475ac9bea69e6f877af3278e420e0f82cd65977ce7a79f`.
It uses the separate build recipe in [native full runtime](../native-full-runtime.md).
This candidate preserves the smaller SDK archives and their earlier receipts.

After a native rebuild and all 16 client tests passed, verification extracted the
archive, moved its installation to a path containing spaces, checked its file
hashes, and used a restricted PATH containing no Node or media executables:

- Bundled CEF named sessions launched, observed controls, filled/read a form,
  and closed successfully.
- CEF recorded 35 decoded video frames at 29.97 FPS with changing pixels and
  actual audible page audio (decoded RMS 0.0822).
- External Chromium recorded 133 decoded video frames at 120 output FPS with
  changing pixels. Output cadence is resampling, not 120 distinct captured frames.
- Both paths used the included FFmpeg 9.0.2/ffprobe binaries. Recording and probe
  outputs are in `.suitecut/full-runtime-20261019/verification-final/results.json`.
- Build, packaging and real-browser supervisor receipts all report exit 0,
  `cleanupFailed:false`, and all owned descendants retired.

This is a Linux x86_64 local candidate, still requiring normal host system
libraries. It adds no custom CEF patches or proprietary playback codecs. Public
redistribution of the GPL media executables remains held until their complete
corresponding source and applicable notices are supplied. Nothing was published.
