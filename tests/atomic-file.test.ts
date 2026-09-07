import { mkdtemp, readFile, readdir, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { afterEach, describe, expect, it } from 'vitest'

import { writeTextFileAtomic } from '../src/atomic-file.js'

const directories: string[] = []

afterEach(async () => {
  await Promise.all(directories.splice(0).map((directory) => rm(directory, { recursive: true })))
})

describe('atomic text files', () => {
  it('replaces a complete file without leaving its temporary file behind', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-atomic-test-'))
    directories.push(directory)
    const path = join(directory, 'manifest.json')

    await writeTextFileAtomic(path, 'first\n')
    await writeTextFileAtomic(path, 'second\n')

    expect(await readFile(path, 'utf8')).toBe('second\n')
    expect(await readdir(directory)).toEqual(['manifest.json'])
  })
})
