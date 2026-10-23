// SPDX-License-Identifier: MIT
// Refuse to pack Git LFS pointer files in place of the bundled Kokoro model.
import { createHash } from 'node:crypto'
import { readFile, stat } from 'node:fs/promises'

const assets = [
  {
    path: 'assets/kokoro/model_quantized.onnx',
    size: 92_361_210,
    sha256: '85f4d4457d42b525d5d4b12d4d4d14d3ba55ae09f2381726860698ab43e1db09',
  },
]

for (const asset of assets) {
  const url = new URL(`../${asset.path}`, import.meta.url)
  const { size } = await stat(url)
  if (size !== asset.size)
    throw new Error(
      `${asset.path} is ${size} bytes, expected ${asset.size}. ` +
        'Install Git LFS and run `git lfs pull` before packing.',
    )
  const digest = createHash('sha256')
    .update(await readFile(url))
    .digest('hex')
  if (digest !== asset.sha256)
    throw new Error(`${asset.path} has SHA-256 ${digest}, expected ${asset.sha256}`)
}
