/** Pinned FFmpeg 9.0.1 archives and upstream SHA-256 checksums. */
export const MEDIA_BUILD_ID = '9.0.1-r1'
export const MEDIA_BUILDS: Record<string, { url: string; sha256: string }[]> = {
  'darwin-arm64': [
    {
      url: 'https://ffmpeg.martin-riedl.de/download/macos/arm64/1787073674_9.0.1/ffmpeg.zip',
      sha256: '8287a1b2229e05eb41859f073e18e6c52c60a778f2f5e6881070fe51b79407fe',
    },
    {
      url: 'https://ffmpeg.martin-riedl.de/download/macos/arm64/1787073674_9.0.1/ffprobe.zip',
      sha256: '102a26b8940a053298d9929bfaae71e4b6ef65ba5f19a99a88c433108560741a',
    },
  ],
  'darwin-x64': [
    {
      url: 'https://ffmpeg.martin-riedl.de/download/macos/amd64/1787081194_9.0.1/ffmpeg.zip',
      sha256: '5bdead62ff504ab9b447cc72b212c4fb481e3f7de5877d427a51bee8136dda40',
    },
    {
      url: 'https://ffmpeg.martin-riedl.de/download/macos/amd64/1787081194_9.0.1/ffprobe.zip',
      sha256: '34511bbcf1988ad2886023bf5ace4f44cf62e6defeb3d194d6f7619e5b061f7f',
    },
  ],
  'linux-x64': [
    {
      url: 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-07-15-39/ffmpeg-n9.0.1-27-g9b0578816c-linux64-gpl-9.0.tar.xz',
      sha256: 'e414c137c7d6ed089c75d0887165f9a5fc1feb38bb6883f31d27fef0c00a03e4',
    },
  ],
  'linux-arm64': [
    {
      url: 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-07-15-39/ffmpeg-n9.0.1-27-g9b0578816c-linuxarm64-gpl-9.0.tar.xz',
      sha256: 'f23fed1372dc21b5034a450a81b63ea8bbd9af69c8a2392b73867f59050b8e9d',
    },
  ],
  'win32-x64': [
    {
      url: 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-essentials_build.zip',
      sha256: 'fec81ae03971d9dd4be3ebe02e263bd2ec1d789483f931bdba5f5715e65da2e9',
    },
  ],
}
