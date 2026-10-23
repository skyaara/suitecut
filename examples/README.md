# Examples

| Example                            | What it shows                                                                                                                              | Run                       |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------- |
| [native.ts](native.ts)             | Record a narrated tour in the embedded CEF browser with page audio, then render the video                                                  | `pnpm example:native`     |
| [song-maker](song-maker/README.md) | A real web app automated through native input and recorded at 1080p60 with page audio, installed from packed tarballs like a real consumer | `pnpm example:song-maker` |

Both need the built CEF executable; set `SUITECUT_NATIVE_EXECUTABLE` to its
absolute path. See [build requirements](../docs/building.md).
