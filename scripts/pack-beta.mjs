import { execFile } from 'node:child_process'
import console from 'node:console'
import { cp, mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { promisify } from 'node:util'

import * as z from 'zod'

const root = resolve('.')
const metadata = z
  .object({
    name: z.string(),
    version: z.string(),
    files: z.array(z.string()),
    exports: z.record(z.string(), z.json()),
    publishConfig: z.record(z.string(), z.json()).optional(),
  })
  .passthrough()
  .parse(JSON.parse(await readFile('package.json', 'utf8')))
const [major, minor] = metadata.version.split('.').map(Number)
if (!Number.isInteger(major) || !Number.isInteger(minor))
  throw new Error('Expected a semantic package version')
const version = `${major}.${minor + 1}.0-beta.0`
const directory = resolve('.suitecut/beta-packages')
await mkdir(directory, { recursive: true })
const staging = await mkdtemp(join(directory, 'staging-'))
try {
  for (const file of metadata.files)
    await cp(join(root, file), join(staging, file), { recursive: true })
  const beta = {
    ...metadata,
    version,
    description: 'Native-default SuiteCut beta for recording and streaming evaluation.',
    main: './dist/beta.js',
    types: './dist/beta.d.ts',
    exports: {
      ...metadata.exports,
      '.': metadata.exports['./beta'],
      './stable': metadata.exports['.'],
    },
    publishConfig: { ...metadata.publishConfig, tag: 'beta' },
  }
  delete beta.scripts
  delete beta.workspaces
  await writeFile(join(staging, 'package.json'), `${JSON.stringify(beta, null, 2)}\n`)
  const { stdout } = await promisify(execFile)(
    'npm',
    ['pack', '--ignore-scripts', '--json', '--pack-destination', directory],
    { cwd: staging, maxBuffer: 8 * 1024 * 1024 },
  )
  const packed = z.array(z.object({ filename: z.string() })).parse(JSON.parse(stdout))[0]
  if (!packed) throw new Error('npm pack did not produce an artifact')
  const report = {
    version,
    defaultBackend: 'native',
    published: false,
    tarball: join(directory, packed.filename),
  }
  await writeFile(join(directory, 'latest.json'), `${JSON.stringify(report, null, 2)}\n`)
  console.log(report)
} finally {
  await rm(staging, { recursive: true, force: true })
}
