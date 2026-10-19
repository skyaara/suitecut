# Media tool downloads

SuiteCut source is licensed under MIT. FFmpeg and FFprobe are separate programs
with their own licenses; they are downloaded during explicit setup and are not
included in the SuiteCut npm tarball.

The optional native **full-runtime** archive does include separate FFmpeg and
ffprobe executables. Its independent pin is
`native/client/tools/full-runtime-media.json`; the full archive retains their
upstream license, documentation, checksums and provenance. This local candidate
is held from public redistribution until complete corresponding source for the
GPL media build and applicable notices are supplied. SDK-only archives and npm
tarballs continue to exclude those media executables.

The pinned FFmpeg 9.0.1 builds enable GPL components, including libx264. Preserve
the applicable upstream licenses and fulfill their corresponding-source obligations
if you redistribute these binaries in your own application or container image.

- FFmpeg license information: https://ffmpeg.org/legal.html
- FFmpeg 9.0.1 source: https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz
- macOS build provider: https://ffmpeg.martin-riedl.de/
- Linux build recipes and source information: https://github.com/BtbN/FFmpeg-Builds
- Windows build provider and source information: https://www.gyan.dev/ffmpeg/builds/
- Pinned archive URLs and SHA-256 hashes: src/ffmpeg-builds.ts

The installed cache retains upstream archive notices and an installation.json file
identifying the exact archives used. Third-party libraries in those builds may
carry additional notices; consult the build providers' source and license information.

# Cursor artwork

The macOS arrow, pointing hand, and text cursor artwork is from
[ful1e5/apple_cursor](https://github.com/ful1e5/apple_cursor), by Abdulkaiz Khatri
and contributors, under GPL-3.0. The original SVG sources, full license,
pinned revision, and adaptation details are included in assets/cursors.
The artwork retains its upstream license.

# Optional native browser

The optional CEF backend is built separately from the SuiteCut npm package. CEF
and Chromium binaries retain their upstream licenses and third-party notices.
The native CMake build copies the SDK LICENSE.txt and CREDITS.html into the output
as CEF-LICENSE.txt and CEF-CREDITS.html. Preserve both when redistributing native
artifacts. The tested SDK version and archive checksum are recorded in
native/browser/cef-build.json. No CEF binaries are included in the npm tarball.

- CEF project and license: https://github.com/chromiumembedded/cef
- CEF binary SDKs: https://cef-builds.spotifycdn.com/index.html
- Native build and API documentation: native/browser/README.md

The optional test-only CEF upstream harness downloads unchanged parser/string
tests from the pinned CEF revision and GoogleTest 1.17.0 under BSD-3-Clause.
It retains CEF-LICENSE.txt and GoogleTest's LICENSE in its ignored run directory,
with source URLs and hashes in manifest.json. These test dependencies and
upstream test files are not included in the package's runtime.

The native I420 path statically links libyuv (BSD-3-Clause). Its revision and
archive SHA-256 are pinned in native/browser/libyuv-build.json. The native build
copies its LICENSE into the bundle as libyuv-LICENSE.txt; retain it in releases.
Upstream: https://chromium.googlesource.com/libyuv/libyuv/

Native HTTP links libcurl, under the curl license. It uses the platform TLS,
compression and networking dependencies supplied by the host's libcurl build.
Retain the license and notices for libcurl and any dependencies when distributing
those libraries with a native binary.
Upstream: https://curl.se/docs/copyright.html

Native HTTP content validation directly links zlib (zlib license) and the Brotli
decoder (MIT). Native component tests additionally link Brotli's encoder; it is
not used by the browser. Preserve their upstream notices when distributing those
libraries with native artifacts.
Upstream: https://zlib.net/zlib_license.html and https://github.com/google/brotli

# YAML parsing

The SDK uses yaml 2.9.0 for general-purpose parsing under the ISC license.
Its upstream license remains included in the installed dependency.
Upstream: https://github.com/eemeli/yaml
