// SPDX-License-Identifier: MIT
import { readdir } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

// The old browser.cc fingerprint is now the aggregate of its compiled modules.
// Controller-specific proof inputs remain explicitly named by each caller.
/**
 * Enumerate the compiled browser modules for source provenance checks.
 * @param {string} [root] Native browser source directory.
 * @returns {Promise<string[]>} Module implementation and build input paths.
 */
export async function nativeBrowserModuleFiles(root = resolve('native/browser')) {
  const files = [
    join(root, 'CMakeLists.txt'),
    join(root, 'cmake/RuntimeModules.cmake'),
    fileURLToPath(import.meta.url),
  ]
  /**
   * Walk a module directory without following source symlinks.
   * @param {string} directory Directory to inspect.
   */
  async function visit(directory) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      const path = join(directory, entry.name)
      if (entry.isSymbolicLink()) throw new Error(`Unexpected module source symlink: ${path}`)
      if (entry.isDirectory()) await visit(path)
      else if (entry.isFile() && /\.(?:cc|h)$/u.test(entry.name)) files.push(path)
    }
  }
  for (const module of ['protocol', 'runtime', 'page', 'process']) await visit(join(root, module))
  return files.sort()
}
