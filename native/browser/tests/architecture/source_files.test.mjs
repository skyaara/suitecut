// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdtemp, mkdir, rm, stat, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { test } from 'node:test'

import { nativeBrowserModuleFiles } from '../../../../scripts/native-browser-source-files.mjs'

test('fingerprints every current module source without the removed monolith', async () => {
  const files = await nativeBrowserModuleFiles()
  assert.equal(files.length, new Set(files).size)
  assert.equal(files.filter((path) => path.endsWith('.cc')).length, 24)
  assert(!files.some((path) => path.endsWith('/browser.cc')))
  for (const path of files) assert((await stat(path)).isFile())
})

test('discovers nested implementations and headers, excluding binary output', async () => {
  const root = await mkdtemp(join(tmpdir(), 'suitecut-source-inventory-'))
  try {
    for (const directory of ['protocol', 'runtime', 'page/detail', 'process', 'cmake'])
      await mkdir(join(root, directory), { recursive: true })
    for (const path of [
      'CMakeLists.txt',
      'cmake/RuntimeModules.cmake',
      'page/detail/new.cc',
      'page/detail/new.h',
      'page/old.o',
    ])
      await writeFile(join(root, path), '')
    const files = await nativeBrowserModuleFiles(root)
    assert(files.includes(join(root, 'page/detail/new.cc')))
    assert(files.includes(join(root, 'page/detail/new.h')))
    assert(!files.includes(join(root, 'page/old.o')))
  } finally {
    await rm(root, { recursive: true, force: true })
  }
})
