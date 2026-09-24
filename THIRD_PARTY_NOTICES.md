# Media tool downloads

SuiteCut source is licensed under MIT. FFmpeg and FFprobe are separate programs
with their own licenses; they are downloaded during explicit setup and are not
included in the SuiteCut npm tarball.

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

The native I420 path statically links libyuv (BSD-3-Clause). Its revision and
archive SHA-256 are pinned in native/browser/libyuv-build.json. The native build
copies its LICENSE into the bundle as libyuv-LICENSE.txt; retain it in releases.
Upstream: https://chromium.googlesource.com/libyuv/libyuv/
