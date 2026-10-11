import { randomUUID } from 'node:crypto'
import { mkdir, rename, rm, writeFile } from 'node:fs/promises'
import { basename, dirname, join } from 'node:path'

import { type UntrustedInput } from './untrusted.js'

/** Replaces a file through a temporary file in the same directory. */
export async function writeFileAtomic(path: string, contents: string | Uint8Array): Promise<void> {
  const directory = dirname(path)
  const temporaryPath = join(directory, `.${basename(path)}.${process.pid}.${randomUUID()}.tmp`)
  await mkdir(directory, { recursive: true })
  try {
    await writeFile(temporaryPath, contents)
    await rename(temporaryPath, path)
  } catch (error) {
    await rm(temporaryPath, { force: true }).catch(() => undefined)
    throw error
  }
}

/** Replaces a UTF-8 text file atomically. */
export function writeTextFileAtomic(path: string, contents: string): Promise<void> {
  return writeFileAtomic(path, contents)
}

/** Serializes JSON with stable indentation and a trailing newline. */
export function formattedJson(value: UntrustedInput): string {
  return `${JSON.stringify(value, null, 2)}\n`
}
