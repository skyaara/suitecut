import { execFile } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { promisify } from 'node:util'

import * as z from 'zod'

const execute = promisify(execFile)
const root = resolve('.')
const directory = resolve('.suitecut/release')
const MetadataSchema = z.object({ name: z.string(), version: z.string() })
const metadata = MetadataSchema.parse(JSON.parse(await readFile('package.json', 'utf8')))
if (!/^\d+\.\d+\.\d+$/.test(metadata.version))
  throw new Error('Stable release packing requires a stable semantic version')
await mkdir(directory, { recursive: true })
const audioPackages = [
  'sherpa-core',
  'vits',
  'matcha',
  'kokoro-sherpa',
  'kitten',
  'zipvoice',
  'pocket',
  'supertonic',
]
const packages = []
for (const path of ['.', ...audioPackages.map((name) => `plugins/${name}`)]) {
  const current = MetadataSchema.parse(
    JSON.parse(await readFile(join(path, 'package.json'), 'utf8')),
  )
  if (current.version !== metadata.version) throw new Error(`${current.name} version mismatch`)
  const { stdout } = await execute(
    'npm',
    ['pack', '--ignore-scripts', '--json', '--pack-destination', directory],
    { cwd: resolve(root, path), maxBuffer: 8 * 1024 * 1024 },
  )
  const packed = z
    .array(
      z.object({
        name: z.string(),
        version: z.string(),
        filename: z.string(),
        size: z.number(),
        unpackedSize: z.number(),
        integrity: z.string(),
        files: z.array(z.object({ path: z.string(), size: z.number() })),
      }),
    )
    .parse(JSON.parse(stdout))[0]
  if (!packed) throw new Error(`npm pack produced no artifact for ${current.name}`)
  if (packed.name !== current.name || packed.version !== current.version)
    throw new Error(`npm pack returned another package for ${current.name}`)
  const tarball = join(directory, packed.filename)
  const sha256 = createHash('sha256')
    .update(await readFile(tarball))
    .digest('hex')
  packages.push({ ...current, ...packed, tarball, sha256 })
}
const primary = packages.find((item) => item.name === 'suitecut')
if (!primary) throw new Error('SuiteCut artifact missing')
await writeFile(
  join(directory, 'SHA256SUMS'),
  packages.map((item) => `${item.sha256}  ${item.filename}\n`).join(''),
)
const report = {
  version: metadata.version,
  defaultBackend: 'native',
  published: false,
  tarball: primary.tarball,
  packages,
}
await writeFile(join(directory, 'latest.json'), `${JSON.stringify(report, null, 2)}\n`)
console.log(`Prepared ${metadata.version}: ${packages.length} unpublished packages in ${directory}`)
