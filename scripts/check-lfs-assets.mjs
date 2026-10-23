// SPDX-License-Identifier: MIT
// Refuse to pack Git LFS pointer files in place of the bundled Kokoro model.
import { createHash } from 'node:crypto'
import { createReadStream } from 'node:fs'
import { stat } from 'node:fs/promises'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

/** Bundled files stored in Git LFS, pinned by exact size and SHA-256. */
export const lfsAssets = [
  {
    path: 'assets/kokoro/model_quantized.onnx',
    size: 92_361_210,
    sha256: '85f4d4457d42b525d5d4b12d4d4d14d3ba55ae09f2381726860698ab43e1db09',
  },
]

/**
 * Verifies every pinned LFS asset in a checkout.
 * @param {URL} root The repository root, ending in a slash.
 */
export async function checkLfsAssets(root) {
  for (const asset of lfsAssets) {
    const url = new URL(asset.path, root)
    const { size } = await stat(url)
    if (size !== asset.size)
      throw new Error(
        `${asset.path} is ${size} bytes, expected ${asset.size}. ` +
          'Install Git LFS and run `git lfs pull` before packing.',
      )
    const hash = createHash('sha256')
    for await (const chunk of /** @type {AsyncIterable<Buffer>} */ (createReadStream(url)))
      hash.update(chunk)
    const digest = hash.digest('hex')
    if (digest !== asset.sha256)
      throw new Error(`${asset.path} has SHA-256 ${digest}, expected ${asset.sha256}`)
  }
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await checkLfsAssets(new URL('../', import.meta.url))
}
