# SuiteCut CEF native integration

`suitecut-cef` 0.1.0 is the standalone C++ browser host extracted from SuiteCut.
It is an executable with a versioned, private stdio protocol. SDKs in any
language can own that process, send browser commands, receive CDP events, and
optionally subscribe to rendered video frames and browser audio.

No Node, Python SDK, Playwright installation, SuiteCut narration layer, or external
FFmpeg libraries are required to run the host. CEF itself includes Chromium's
browser media decoders; removing SuiteCut's output encoder does not remove web
video playback. Recording, streaming, mixing, and encoding are responsibilities
of the consuming application or the separate full SuiteCut engine.

The integration boundary is a **native process**, not a stable in-process C++ ABI.
Process ownership isolates CEF initialization, its message loop and subprocesses
from a caller's language runtime. `PROTOCOL.md` is the SDK contract;
`protocol.schema.json` describes its envelopes. The host opens no debugging port.

## Source build

Linux x86-64 is verified on Debian 13. Use CMake 3.21+, Ninja, C++20, a display
server, and working Chromium runtime/audio dependencies. The source package
contains SuiteCut code and pins, not a pre-downloaded Chromium SDK.

From the extracted source package:

```sh
python3 scripts/install-cef-codecs.py --destination "$PWD/deps/cef"
git init deps/libyuv
git -C deps/libyuv fetch --depth=1 \
  https://chromium.googlesource.com/libyuv/libyuv \
  7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C deps/libyuv checkout --detach FETCH_HEAD
cmake -S . -B build -G Ninja \
  -DCEF_ROOT="$PWD/deps/cef" -DLIBYUV_ROOT="$PWD/deps/libyuv" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The executable and its CEF resources are in `build/native/browser/`. Keep the
complete runtime directory together. In a repository checkout, configure with
`-S native/cef` instead. This entry point always disables SuiteCut media; the
original `native/browser` entry point still defaults to the full media engine.
No FFmpeg headers or link libraries are searched for in the standalone build.

Linux's pinned third-party CEF distribution is
152.0.6+g708dc14 / Chromium 152.0.7977.83 with H.264/AAC decoding. The installer
verifies archive and library digests; CMake verifies the library and version.
It has not been independently rebuilt here. The installer is build tooling,
not a Python SDK. The macOS ARM64 source path uses its separate stock CEF pin
and has not been verified for this standalone package. Windows is unsupported.

## Runtime bundle

The Linux binary archive includes the executable, CEF library/resources, pinned
dependency manifests, protocol documentation, and third-party license files.
Extract it and use `runtime/suitecut-browser`. It is a Debian 13 x86-64 build,
not a universal Linux binary: inspect `verification/binary-dependencies.txt`
and provide compatible system libraries, a display server, and audio service.
The browser sandbox remains enabled. A deployment must permit Chromium's
sandbox; do not add `--no-sandbox` to work around a failing deployment.

The source archive and binary archive have independent SHA-256 manifests.
Version 0.1.0 identifies this native package; it does not replace the sealed
SuiteCut 2.1.0 SDK release or its earlier benchmark results.

## SDK implementation

1. Create a private temporary profile and spawn the executable without a shell.
2. Pass validated launch arguments from `PROTOCOL.md`. Continuously drain
   stdout **and stderr**; wait for the `ready` packet before sending commands.
3. Keep native request IDs and CDP IDs in separate maps. A `cdp` response only
   acknowledges dispatch; its eventual CDP response arrives as an `event` packet.
4. Apply packet size checks before allocation. Process control events promptly;
   consumers of frames/audio must not block the packet reader.
5. On EOF, malformed output, terminal error, or process failure, reject pending
   operations and release resources. Close stdin for graceful shutdown, wait
   for the process, then remove its profile. Apply a bounded kill fallback.

The CDP bridge supplies browser targets, isolated contexts, flattened sessions,
popups and page DevTools commands needed by SuiteCut's tested Playwright flows.
An SDK can forward CDP into Playwright's supported transport, or expose its own
browser API. Clients that require a WebSocket endpoint must supply their own
authenticated loopback relay. There is no WebSocket relay in this package.
Compatibility is bounded by the tested flows, not complete Playwright or
Chromium product-feature conformance.

Raw frame subscriptions replace stale queued video; the raw audio queue also
has a finite capacity. These taps are not a lossless recording pipeline. SDKs
must inspect drops and decide how to handle backpressure and synchronization.
This extraction does not fix the quiet audio intervals observed in prior
SuiteCut/OBS tests or establish whole-Chromium memory-leak freedom.

## Packaging from the repository

```sh
python3 scripts/pack-cef.py --output /absolute/path/to/packages \
  --binary-dir /absolute/path/to/build/native/browser \
  --verification /absolute/path/to/verification
```

The packer uses an explicit source allowlist, excludes all SuiteCut media-engine
sources and SDKs, includes required notices, refuses to overwrite a package,
and records checksums. It does not publish, install, tag, or upload anything.

SuiteCut sources are MIT licensed. Retain CEF's license and Chromium credits
and libyuv's BSD license when distributing binaries.
