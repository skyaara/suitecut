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
