# Native macOS ARM64 runtime

The macOS recipe builds three local release candidates: a static C++20/C SDK,
a shared C++20/C SDK, and a full runtime containing the static SDK's native
tools, the SuiteCut application, CEF 156 beta, FFmpeg/ffprobe, and the complete
non-system dynamic-library dependency closure. Application dylibs live directly
in `Contents/Frameworks`, including their transitive dependencies; custom
subdirectories can fail during macOS code-signing clone launches. See
[Apple's code-signing layout guidance](https://developer.apple.com/library/archive/documentation/Security/Conceptual/CodeSigningGuide/Procedures/Procedures.html). Runtime paths are relative to
the installation. Node.js and a system FFmpeg installation are not required.

Local builds and packages share one signing policy in
`~/Library/Application Support/SuiteCut/macos-signing.json`, with optional
project override `.suitecut/macos-signing.json` (gitignored):
`{"identity":"<40-digit certificate fingerprint>"}`. The user-level setting
survives deleting build/cache directories.
The exact certificate must be installed and valid; a missing configured identity
fails the build instead of falling back to ad-hoc. `--signing-identity` on the
signing/packaging scripts or `SUITECUT_MACOS_SIGN_IDENTITY` explicitly overrides
the local policy. Without a configured identity, other development hosts retain
ad-hoc signing. CMake signs the rebuilt engine and all nested code using the
same policy as the runtime packager. The configured certificate is reused;
these scripts never create certificates or modify Keychain access controls.

Signature bytes change when code changes. A certificate-backed designated
requirement keeps the app identifier and signing team stable across rebuilds,
so an approved persistent-profile Keychain identity can remain approved. The
first authorization, a locked Keychain, or revoked access can still require
macOS interaction. Temporary automation profiles continue avoiding Keychain.
Certificate-backed builds include Apple's trusted signing timestamp and require
access to its timestamp service. They are not notarized or published. The October 19 qualification host is macOS 27.0.1 on
Apple Silicon. Bundled Homebrew dependencies can require newer macOS versions
than the engine's compilation target; do not infer macOS 13 compatibility from
that target. Intel and older macOS versions require their own dependency builds
and runtime qualification. Safari/WebKit is not a supported backend.

Disposable macOS automation profiles use Chromium's mock keychain and do not
query the user's Chromium Safe Storage item. This avoids repeated access prompts
when ad-hoc signed builds change identity. The mock key is unsuitable for durable
secret storage: explicitly persistent profiles keep real macOS Keychain-backed
encryption. Recording, rendering, and temporary browser sessions do not require
access to the user's Keychain.
An unattended persistent-profile test needs an unlocked test Keychain that
authorizes the exact signed app identity. Rebuilt ad-hoc apps can prompt again
and block navigation and shutdown until macOS settles that request.

## Build and package

Use the exact macOS ARM64 distribution of the CEF version pinned in
`native/browser/cef-build.json`, the pinned libyuv source, CMake, Ninja, an Apple
C++20 toolchain, OpenSSL 3 or newer, libpsl, Brotli, and ZLIB. Full API extra-CA
coverage requires curl built with the same OpenSSL major as the engine. Apple's
SecureTransport curl cannot provide that capability. Set `CURL_INCLUDE_DIR`,
`CURL_LIBRARY_RELEASE`, and `OPENSSL_ROOT_DIR` explicitly when reusing a build
cache that previously selected Apple's curl. The runtime packager bundles the
selected curl/OpenSSL dylibs; preserve their actual minimum macOS requirements.
Keep a few GB free for builds, archives, and an independent extraction.

```sh
cmake -S native/browser -B build/macos-engine -G Ninja \
  -DCEF_ROOT="$CEF_ROOT" -DLIBYUV_ROOT="$LIBYUV_ROOT" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCURL_INCLUDE_DIR=/opt/homebrew/opt/curl/include \
  -DCURL_LIBRARY_RELEASE=/opt/homebrew/opt/curl/lib/libcurl.dylib \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@4 \
  -DCMAKE_CXX_FLAGS=-D_LIBCPP_ENABLE_EXPERIMENTAL
cmake --build build/macos-engine --target suitecut-browser --parallel 1

cmake -S native/client -B build/macos-client-static -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/macos-client-static --parallel 2
ctest --test-dir build/macos-client-static --output-on-failure
cmake --install build/macos-client-static --prefix "$PWD/build/macos-sdk-static"

cmake -S native/client -B build/macos-client-shared -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_INSTALL_RPATH='@loader_path/../lib'
cmake --build build/macos-client-shared --parallel 2
ctest --test-dir build/macos-client-shared --output-on-failure
cmake --install build/macos-client-shared --prefix "$PWD/build/macos-sdk-shared"

python3 scripts/package-native-runtime-macos.py \
  --sdk build/macos-sdk-static --kind runtime \
  --app build/macos-engine/suitecut-browser.app \
  --ffmpeg /opt/homebrew/bin/ffmpeg --ffprobe /opt/homebrew/bin/ffprobe \
  --output build/macos-runtime-artifact
```

Package the two SDK prefixes with the same script, `--kind static` or
`--kind shared`, and a separate new output directory. The script refuses to
overwrite existing artifact directories. It copies inputs, relocates each
non-system Mach-O dependency, signs the copies, verifies nested application
signatures, and writes a manifest, archive, and `SHA256SUMS`. Original input
binaries remain unchanged. `@rpath` dependencies are resolved explicitly;
unresolved dependencies fail packaging. Dependencies loaded by the CEF app and
its helpers stay inside the outer `.app/Contents/Frameworks/SuiteCutLibraries`
directory so macOS code-signing clones can launch the app independently of the
sibling runtime `lib` directory. The archive verifier exercises this standalone
app layout with the real C++ browser workflow.

## Verify the archive and recording

```sh
python3 scripts/verify-native-runtime-macos.py \
  --archive build/macos-runtime-artifact/SuiteCutRuntime-0.1.0-macos-arm64.tar.gz \
  --client-build build/macos-client-static --output build/macos-runtime-proof

python3 scripts/verify-native-macos-recording.py \
  --installation 'build/macos-runtime-proof/relocated installation' \
  --output build/macos-recording-proof
```

The archive proof extracts and moves the installation to a path containing
spaces, verifies every manifest hash, validates code signatures, and tests
native CLI input and actual C++ browser outcomes. Runtime subprocesses have
neither Node nor Homebrew tools on `PATH`. The recording proof decodes every
video frame into an authored pixel counter and reports distinct frame rate,
repetition, encoded cadence, dimensions, and frame counts. Audio cases decode
PCM and verify the fixture's actual tone and audio/video duration agreement.
`--case 3840x2160@30` selects a specific workload; `:audio` enables audio.

The public cadence ceiling is 120 FPS. It does not guarantee 120 distinct
frames. Resolution, page content, capture cost, encoding, and machine load
affect throughput. Retain the per-case reports and media; short fixture runs do
not establish sustained performance for arbitrary websites or long recordings.

macOS uses CEF IOSurface paints and their native capture timestamps. Each
view/popup clock is calibrated before manual capture and remains fixed while
active capture, so callback scheduling delay is not treated as missing source time.
Calibration continues through hardware preparation. Capture-counter resets detect
a new stream even when its timestamp and the preceding blank stream are both
zero. Captured images never map into the future. Exact multiples of the known
integer-microsecond capture period are restored to the requested rational cadence
to prevent long-run CFR drift; arbitrary gaps retain their original duration.
Native timestamps bypass arrival-time reconstruction and use nearest-slot CFR
resampling. Matching native capture/output rates round genuine source intervals
within a one-slot phase bound, avoiding accumulated quantization collisions. A
real multi-slot source interval still inserts its CFR hold. Source images missing
from CEF remain visible as held output slots.

For matching even-size manual MP4 recordings without live pixel subscribers,
open popups or screenshot tracing, Metal converts the borrowed CEF surface to
an owned BT.709 limited-range NV12 surface before returning from its callback.
The media worker submits that surface directly to a hardware-only VideoToolbox
H.264 session. Pixel data does not return to the CPU or cross a raw-video pipe.
FFmpeg remains responsible for final audio encoding/container muxing and the
software VP9 path. Other geometries retain bounded worker-side fitting and
conversion. Live pixel subscribers, popup composition and screenshot tracing
use the genuine full-pixel path. A stale CPU view is never sent to a new frame
subscriber: it requests a fresh native paint first.

GPU surfaces are pooled within 64 MiB of actual IOSurface allocation, with at
most 32 idle buffers, and released when the recording physically settles. Only
one GPU command is in flight per capture callback. The hardware encoder admits
at most four in-flight frames. Pending input is independently capped at 200 ms
and 64 MiB; oldest input is retired on sustained overload. There is one existing
application media worker per recording, with no additional conversion worker.
VideoToolbox manages its completion callbacks behind the four-frame admission
bound. The native session requires and verifies hardware acceleration.

The private hardware preparation frame is discarded before writing H.264 data.
Actual frame counts therefore need no stream-copy warm-up trim. Static frames
advance during capture with a bounded 500 ms playout margin; stop does not defer
an entire static recording's encoding until finalization. The native encoder
reports admission/drop errors explicitly. The software and resized-output
fallbacks retain their existing native subprocess ownership and quotas.

The bitmap fallback (`--suitecut-shared-texture=0`) fits/converts snapshots on
the same media worker. A bounded 100 ms / 64 MiB lookahead reconstructs source
time from subsequent genuine view arrivals and admitted capture sequence.
Correction is bounded, monotonic, and never before admission. It adds processing
latency without moving the audio clock. Sparse pages flush after 100 ms; stop
drains accepted input and lookahead. At matching capture/output rates,
sub-slot phase noise remains ambiguous until a complete missing source interval
is observed. Paused source sequence ranges are excluded. Queue loss keeps its
sequence holes. Popup/cache events do not claim the view's capture cadence.

`supersededFrames` counts queue retirement, `coalescedFrames` counts timestamp
resampling discards, and `heldFrames` counts repeated CFR slots. Successful
accounting requires input = queue drops + coalesced + output - held. Actual
video IDs and independently decoded counts must be checked as well.
`captureTimestampsExact`, `captureLookaheadMs`, `encodingBackend`,
`gpuInputFrames`, and `gpuNv12Frames` identify the path actually used.

Native backend selection follows the configured shared-texture mode, even if
recording starts before the first accelerated paint. GPU shader preparation runs
before hardware preparation. Native images older
than the hardware-ready capture fence are excluded from admission, so delayed
preparation surfaces never become an opening video/audio gap. The pinned CEF
passes audio TimeTicks despite its epoch-time API comment. On macOS, that clock
is calibrated directly from Mach absolute time; compositor timestamps have a
separate capture-relative origin. Both map to the recorder host clock without
anchoring the first PCM packet at receipt. Legacy epoch PTS maps through the
wall/monotonic clock pair; unknown clocks retain the source-delta fallback.
A bounded 200 ms
PCM reservoir spans preparation. Audio recordings wait for the first genuine
PCM packet before admitting video, bounded to one second for silent pages. The
first packet advances the capture fence and requests a fresh static-page paint;
the timeout also requests a snapshot, so silent documents cannot deadlock startup.
Accelerated static pages may produce no callback for `Invalidate`. Startup then
observes their latest owned genuine surface once at the explicit snapshot time,
only after 100 ms without a delivered paint. Animated sources keep waiting for
their capture-time frames. One pending UI snapshot check per source is retained.
That single retained surface shares the same 64 MiB GPU budget and is released
with the recording; subsequent static CFR repeats remain counted as holds.

Pause/resume maps native capture time through recent pause intervals, including
video and PCM delivered after a transition. PCM packets split at the capture
boundaries. Pause history is capped at 256 retained intervals and retired once
older than the 500 ms delivery window. Stop seals its original capture-time
boundary and accepts only already-captured video/PCM within it for at most
500 ms, bounded further by the caller's finalization deadline. Recording owners
remain alive until physical encoder cleanup. A repeated stop can tighten the
deadline without moving the original media boundary. CFR rounding may add one
terminal slot; audio padding includes that slot.

On macOS, manual `.mp4` output selects hardware H.264 through VideoToolbox and
AAC page audio. `.webm` retains software VP9/Opus. Hardware encoding is required
for MP4 and fails explicitly when unavailable; there is no silent software
fallback. Example:

```sh
suitecut-native-record --url https://example.com --output capture.mp4 \
  --width 1920 --height 1080 --fps 120 --audio --duration-ms 60000
```

Hardware preparation happens before `recording.start` acknowledges and before
the active capture clock begins. The direct native path discards one private
preparation packet and forces the first active frame to be independently
decodable. The subprocess fallback bounds preparation to 32 packets and removes
them during final muxing without re-encoding. Results
include `encoder`, `container`, `preparationMs`, `preparationFrames` (paints
received during preparation), and `preparationPackets`. Active input/output,
drop, and hold counters exclude preparation. Preparation remains bounded by
the original start deadline, and cancellation retires its owned processes.

Darwin video transport uses Unix sockets with bounded 4 MiB buffers (or a
smaller host-supported size) to avoid small-pipe scheduling overhead for raw
frames. The media helper dispatches before loading CEF. Active recording uses
a scoped macOS latency activity and user-initiated worker QoS; neither changes
global scheduler settings nor reserves CPUs from other applications. These
changes do not add H.264/AAC playback codecs to stock CEF.

Hardware encoding removes the software codec bottleneck, but cannot recover
images that CEF never delivered. A 120 FPS container can still contain repeated
images under CPU, GPU, or I/O contention. For a strict sustained check, require
120 decoded distinct FPS and zero recorder drops, and keep the load and host
samples with the result:

```sh
python3 scripts/verify-native-macos-recording.py \
  --installation 'build/macos-runtime-proof/relocated installation' \
  --output build/macos-loaded-recording-proof --format mp4 \
  --duration-ms 60000 --case 1920x1080@120:audio --load-workers 5 \
  --required-distinct-fps 120 --require-no-recorder-drops --require-native-capture \
  --require-source-continuity
```

The load workers run for the entire recorder command and are retired by their
owner. The default fixture also reallocates its canvas on every animation
frame; `--fixture-mode steady` measures drawing into a stable canvas instead.
These are different workloads and must be identified in performance claims.

Software VP9 uses the realtime speed preset (`cpu-used=8`), zero lookahead,
and row multithreading. Encoder threads are bounded by the native automatic CPU
budget divided by the active recording count when the encoder starts; filters
use one thread. This is a launch-time per-encoder budget, not a live global
reservation or a performance guarantee for simultaneous recordings. The faster
preset trades encoding efficiency for realtime throughput; CRF remains 18.
Adding unbounded workers would compete with CEF and increase memory pressure.

Audio is trimmed/padded to the complete encoded CFR timeline, which can extend
the requested stop by less than one frame. Muxing copies every video packet;
it does not apply a global wall-clock trim that can remove the final packet
after Opus/WebM timestamp offsets.

The Darwin recorder uses immediately unlinked private capture/PCM files,
inherited `/dev/fd` descriptors, and a kernel process-exit monitor that retires
FFmpeg when its browser parent dies. Final output is copied into an exclusive
private sibling and published atomically without replacing an existing target.
An abrupt kill during that short final publication step can leave its named
private staging file. Cancellation and normal shutdown remove owned staging.

CEF uses stock playback codecs. Bundling FFmpeg does not add H.264/AAC playback
to CEF. CEF WebSocket routing, directory upload, cross-navigation coverage, full
clock control, and offscreen out-of-process-frame full-page capture remain
outside this runtime's advertised capabilities. Embedded native streaming uses bounded FFmpeg subprocesses on Linux and macOS.
The macOS regression verifies actual publisher startup and physical child
retirement after renderer failure; it does not qualify every live destination. Firefox needs a separately supplied browser and separate
macOS validation. The full Node narration/editing workflow is not bundled.

Retain the bundled dependency notices and exact media provenance. GPL-enabled
FFmpeg builds need complete corresponding source and applicable notices before
public redistribution. These local archives retain that publication hold.
