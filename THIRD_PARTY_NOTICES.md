# Media tools

suitecut source is licensed under MIT. FFmpeg and FFprobe are separate programs
with their own licenses. SDK-only archives do not include them.

The optional native **full-runtime** archive does include separate FFmpeg and
ffprobe executables. Its independent pin is
`native/client/tools/full-runtime-media.json`; the full archive retains their
upstream license, documentation, checksums and provenance. This local candidate
is held from public redistribution until complete corresponding source for the
GPL media build and applicable notices are supplied.

The pinned FFmpeg 9.0.1 builds enable GPL components, including libx264. Preserve
the applicable upstream licenses and fulfill their corresponding-source obligations
if you redistribute these binaries in your own application or container image.

- FFmpeg license information: https://ffmpeg.org/legal.html
- FFmpeg 9.0.1 source: https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz
- macOS build provider: https://ffmpeg.martin-riedl.de/
- Linux build recipes and source information: https://github.com/BtbN/FFmpeg-Builds
- Windows build provider and source information: https://www.gyan.dev/ffmpeg/builds/
- Pinned archive URLs and SHA-256 hashes: native/client/tools/full-runtime-media.json

Third-party libraries in those builds may carry additional notices; consult the
build providers' source and license information.

# Native browser

CEF and Chromium binaries retain their upstream licenses and third-party notices.
The native CMake build copies the SDK LICENSE.txt and CREDITS.html into the output
as CEF-LICENSE.txt and CEF-CREDITS.html. Preserve both when redistributing native
artifacts. The tested SDK version and archive checksum are recorded in
native/browser/cef-build.json.

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
