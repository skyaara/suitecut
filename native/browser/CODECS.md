# Native browser playback codecs

On macOS arm64 the pinned SDK is a codec-enabled build of CEF 156.0.30 (below) and plays H.264, AAC and HEVC. The official Linux CEF156 runtime still lacks H.264/AAC playback; the build profile here prepares a replacement Linux library and does not claim one has been compiled or installed. Recording and publishing use a separate FFmpeg encoder and do not add browser decoders.

## Build common web codecs

[`cef-codecs.json`](cef-codecs.json) pins a Linux x86-64 profile for the pinned CEF **156.0.30+g6028375+chromium-156.0.8078.96**, its Chromium/FFmpeg source revisions, relevant source hashes and GN options. The essential decoder options are:

```gn
proprietary_codecs = true
ffmpeg_branding = "Chrome"
```

The profile retains software VP8/VP9, AV1 and common audio paths, and includes HLS/MPEG2TS parsers. Native HLS behavior still needs separate playback acceptance. It does not force platform-only codec flags. This codec build is independent of the experimental vendor-fix bundle under `cef-patches/`; those patches are not applied.

On a prepared Linux Chromium build host, with this repository, dependencies, standalone SDK output and pinned libyuv available:

```bash
python3 scripts/build-cef-codecs.py \
  --build-root /mnt/suitecut-cef156-web-codecs --stage plan

python3 scripts/build-cef-codecs.py \
  --build-root /mnt/suitecut-cef156-web-codecs --stage all \
  --report .suitecut/cef-web-codec-build-receipt.json
```

The first command prints the pinned profile and observed host resources without downloading sources. The second downloads and checksum-checks the official automate script, checks out exact sources, checks source hashes, generates and validates `args.gn`, compiles CEF, creates a minimal distribution, builds SuiteCut in a separate directory and runs actual playback acceptance. Compilation and distribution commands follow the existing pinned CEF build recipe; the full source build has not been executed in this workspace.

Stages `checkout`, `compile`, `runtime` and `verify` permit deliberate resumption. `runtime --cef-sdk /absolute/path/to/sdk` can consume an exact-version prebuilt distribution if one becomes available; CMake still enforces the version and playback acceptance is still required. Runtime construction and verification can run without the large Chromium source-build budget. Source checkout/compilation require the configured conservative **500 GiB free disk / 64 GiB RAM** budget; this is a provisioning policy, not a measured minimum. The current workspace has approximately 22 GiB free disk and 17 GiB RAM, so its build attempt stops before source download or compilation.

Use a dedicated empty build root. Subsequent stages require its matching ownership marker. Logs, generated arguments and partial outputs remain available on failure. Reports use new paths and are never silently replaced. `--jobs` controls compiler parallelism; SuiteCut helper compilation is capped at four jobs. The script does not provision a machine, install OS dependencies, start remote CI, replace the stock SDK or publish artifacts.

## Acceptance and packaging

```bash
node scripts/verify-browser-codec-runtime.mjs \
  --executable /mnt/suitecut-cef156-web-codecs/native/suitecut-browser \
  --output .suitecut/cef-web-codec-acceptance
```

The default `web` gate requires 14 real playback cases: H.264-only MP4, H.264/AAC MP4 and fragmented-MP4 MediaSource, AAC-only audio, VP8/VP9/AV1, VP9/Opus MediaSource and common audio samples. It checks trusted controls, pause/resume/seek as applicable, decoded audio bytes, decoded/rendered video frames, advancing time and completion. Missing encoders, skipped required fixtures, unsupported media, failed controls or runtime/SDK changes fail acceptance. Runtime and SDK hashes are captured before and after. HTML capability advertising alone cannot pass.

Stock CEF was checked with this gate: **ten required open-codec cases passed; all four required H.264/AAC cases failed**, producing exit code 1 and `FAIL_CODEC_REQUIREMENTS`. This negative proof demonstrates the gate detects the current gap. On Linux a positive replacement-runtime proof remains pending; macOS has one (below).

## macOS arm64: third-party CEF 156.0.30 prebuilt

On 2026-11-19 the [OpenArch `cef-156.0.30-codecs`](https://github.com/dpfaffenbauer/OpenArch/releases/tag/cef-156.0.30-codecs) minimal distribution was checked: `cef_binary_156.0.30+g6028375+chromium-156.0.8078.96_macosarm64_minimal.tar.bz2`, 135,597,207 bytes, SHA-256 `d9bcb24c64284be28dfd5ed99961fd2aa562308bad2d0ad79217fdec18542dcc`. It is built on GitHub Actions with `proprietary_codecs=true` and `ffmpeg_branding="Chrome"`; HEVC decodes through VideoToolbox. It is not an official CEF build and carries no codec patent licence.

It is the pinned macOS arm64 SDK (`macosarm64` in [`cef-build.json`](cef-build.json)) since 2026-11-19. SuiteCut was built against it and run through `verify-browser-codec-runtime.mjs --profile web-hevc` beside the stock 156.0.7 build:

| Case | Stock 156.0.7 | OpenArch 156.0.30 |
| ---- | ------------- | ----------------- |
| H.264-only MP4 | unsupported | plays |
| H.264/AAC MP4 | unsupported | plays |
| H.264/AAC MediaSource | unsupported | plays |
| AAC-only | unsupported | plays |
| HEVC-only MP4 | unsupported | plays |
| VP8/Opus, VP9/Opus (file and MediaSource), MP3, WAV, Ogg/Opus, FLAC | play | play |
| VP8/Vorbis, AV1/Opus, Ogg/Vorbis | not generated: local ffmpeg lacks the encoders | not generated |

Every generated case passed on the codec build (12/12; stock 7/12). The gate still reports `FAIL_CODEC_REQUIREMENTS` on both builds only because of the three ungenerated fixtures.
After a replacement passes, package it separately:

```bash
node scripts/package-browser-runtime.mjs \
  --source /mnt/suitecut-cef156-web-codecs/native \
  --output /mnt/suitecut-cef156-web-codecs/runtime --strip false
```

Preserve its acceptance receipt, build receipt and resulting runtime manifest. Any stripping or subsequent binary change needs a new playback proof against those delivered bytes. Finally rerun the actual Twitch probe using the replacement runtime; compile success or a local MP4 pass does not prove a particular service works:

```bash
SUITECUT_NATIVE_EXECUTABLE=/mnt/suitecut-cef156-web-codecs/runtime/suitecut-browser \
SUITECUT_TWITCH_BACKENDS=cef \
SUITECUT_TWITCH_OUTPUT=.suitecut/new-cef-twitch-proof \
  node scripts/verify-browser-twitch.mjs
```

## Prebuilt distributions and limits

The official [CEF build index](https://cef-builds.spotifycdn.com/index.html) offers standard/minimal/client/tools variants, without a separate proprietary-codec selection. There is no runtime command that turns missing decoder code on. [Third-party codec-enabled prebuilts exist](https://github.com/ashwardy/loopy-cef-builds/releases), but the checked Linux artifacts use older CEF154/152 versions. For macOS arm64, CEF156 codec-enabled prebuilts exist as of November 2026: OpenArch 156.0.30 (checked above) and amenophis/cef-codecs `cef-156.0.7-codecs`, which matches the current pin but was built on the maintainer's own machine without public logs (not checked). No CEF156 codec-enabled Linux or Windows artifact was found. Changing the version or mixing libraries is not part of this build profile.

This is deliberately a **common web codec profile**, not “all codecs.” On this Chromium pin, the software FFmpeg video adapter and security allowlist accept H.264 only; VP8/VP9 and AV1 have separate implementations. HEVC, Dolby/DTS, ALAC and various legacy formats require additional adapter/demuxer/decoder integration or an actual platform decoder. Setting their platform flags can advertise support without supplying a working Linux decoder. `--profile web-hevc` makes the acceptance gate additionally require actual HEVC playback; it does not add a decoder.

Widevine/DRM components and licenses are separate from codec flags. This profile does not provide a CDM, certify HDR/profile combinations or establish hardware decode throughput. See the [real-world playback matrix](../../docs/internals/browser-real-world-coverage.md) for measured current runtime behavior.

Local audit/proof evidence: `.suitecut/codec-enable/source-audit/FINDINGS.md`, `.suitecut/codec-enable/prebuilt-audit.md`, `.suitecut/codec-runtime-acceptance/stock-negative/acceptance.json` and `.suitecut/codec-enable/build-attempt.json`.
