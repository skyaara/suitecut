/** The current release, as CHANGELOG.md describes it. Keep the two in step. */
export const release = {
  version: '0.1.0',
  name: '0.1.0 alpha',
  platform: 'macOS ARM64 (Apple silicon)',
  /** False until the archives are attached to a public release. */
  published: false,
} as const

export interface Archive {
  name: string
  holds: string
  needs: string
  /** Whether the archive is cleared for public release, or held. */
  publicRelease: 'Yes' | 'Held'
}

export const archives: readonly Archive[] = [
  {
    name: `SuiteCutClient-${release.version}-macos-arm64-shared`,
    holds:
      'The C++20 SDK and C ABI as a shared library, with suitecut-native-control, the agent skill and CMake exports',
    needs: 'macOS 14',
    publicRelease: 'Yes',
  },
  {
    name: `SuiteCutClient-${release.version}-macos-arm64-static`,
    holds: 'The same as a static library',
    needs: 'macOS 14; the program that links it supplies OpenSSL 3.5 or newer',
    publicRelease: 'Yes',
  },
  {
    name: `suitecut-embed-kit-${release.version}-macos-arm64`,
    holds:
      'The signed embed kit: suitecut-browser.app on official CEF 156.0.30, without proprietary codecs, and suitecut-session',
    needs: 'macOS 14',
    publicRelease: 'Yes',
  },
  {
    name: `SuiteCutRuntime-${release.version}-macos-arm64`,
    holds:
      'Everything in one folder: the SDK, the command line with a suitecut launcher, the codec-enabled engine and FFmpeg for recording',
    needs: 'macOS 15',
    publicRelease: 'Held',
  },
]

/** Results of the 0.1.0 release recording checks on macOS ARM64. */
export const recordingChecks = {
  mp4: '12 of 12 cases passed, up to 3840×2160 at 120 FPS with 120 distinct frames per second',
  webm: '10 of 12 cases passed; the two with audio failed on the opening silence, and software VP9 fell short of 120 FPS above 1920×1080',
} as const
