# SuiteCut CEF 0.1.0 package verification

The standalone native CEF integration is prepared locally, not published.
It provides the C++ browser host, private stdio protocol v1, CDP automation,
raw BGRA/I420 frames and 48 kHz stereo PCM. No Python SDK was added.
Node and SuiteCut's external FFmpeg engine are excluded from the native package.
CEF's Chromium browser decoders remain present.

| Verification | Result |
| --- | --- |
| Standalone native clock/pixel tests | 2 passed |
| Direct process protocol tests | BGRA, I420 and malformed-command cases passed |
| Standalone real Playwright automation | 9 passed on the final binary |
| Full SuiteCut native regression | 8 groups passed |
| Full SuiteCut Playwright/media regression | 16 passed |
| Extracted standalone source archive | Configured successfully; encoder sources excluded |
| Protocol schema | 8 valid / 4 invalid envelopes checked |
| Package behavior | Deterministic source archive and overwrite refusal checked |

The direct process tests use only Python's standard library as black-box test
tooling. They verify readiness capabilities, separate native/CDP request IDs,
browser-context/page/session round trips, audible PCM, distinct rendered frames,
rejected media commands and graceful process shutdown. The automation suite
checks locators, keyboard/input, scrolling, iframes, screenshots, resizing,
network interception, context isolation, popups, dialogs, uploads/downloads,
CDP events, permissions, capacity recovery and repeated page/context shutdown.

The full media-enabled executable rebuilt from the changed source is
**byte-for-byte identical** to the sealed SuiteCut 2.1.0 executable:
`757a03fa49ed82dcd119c2888bdc0ebe1c355b4b9d9964bda38b99427a38f0b3`.
The existing release archives and benchmark evidence remain unchanged.

The standalone executable has no external FFmpeg libraries in its direct or
transitive `ldd` dependencies. Its rpath is `$ORIGIN`, removing CEF's default
current-directory search and avoiding a dependency on the build SDK path.
`binary-dependencies.txt`, `binary-dynamic-section.txt`, source hashes,
Playwright JSON/artifacts and detailed build/test logs accompany this report.

Linux x86-64 on this Debian 13 host is verified, with pinned CEF
152.0.6+g708dc14 / Chromium 152.0.7977.83, C++20, four-core affinity,
private Xvfb and PulseAudio. No 8 GB kernel limit or GPU capability is claimed
for these functional checks. macOS and Windows binary delivery are not verified.

The initial standalone link needed the already installed X11 dependency
sysroot; that failure and the successful retry are retained. One attempted build
was refused by the exclusive runtime lock; browser and build work subsequently
ran serially. Cached workspace metadata produces a pnpm warning without
changing dependencies; package runtime operation requires no pnpm installation.

This is a package and compatibility check, not a new performance benchmark,
whole-Chromium sanitizer audit or long-duration leak test. Raw capture can drop
under backpressure, complete Playwright compatibility is not established, and
prior audio quietness observations are unresolved. Recording/streaming remain
available through the separate full SuiteCut engine.
