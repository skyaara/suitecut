import { homedir } from 'node:os'
import { join, resolve } from 'node:path'

import { MEDIA_BUILD_ID } from './ffmpeg-builds.js'

export function mediaToolDirectory(): string {
  const cache =
    process.env.SUITECUT_MEDIA_CACHE ??
    (process.platform === 'darwin'
      ? join(homedir(), 'Library', 'Caches', 'suitecut')
      : process.platform === 'win32'
        ? join(process.env.LOCALAPPDATA ?? join(homedir(), 'AppData', 'Local'), 'suitecut')
        : join(process.env.XDG_CACHE_HOME ?? join(homedir(), '.cache'), 'suitecut'))
  return resolve(cache, 'media', `${MEDIA_BUILD_ID}-${process.platform}-${process.arch}`)
}

export function mediaToolPaths(directory = mediaToolDirectory()): {
  ffmpeg: string
  ffprobe: string
} {
  const suffix = process.platform === 'win32' ? '.exe' : ''
  return {
    ffmpeg: join(directory, `ffmpeg${suffix}`),
    ffprobe: join(directory, `ffprobe${suffix}`),
  }
}
