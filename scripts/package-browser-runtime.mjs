#!/usr/bin/env node
// Package the installed Linux runtime without changing its developer build or SDK.
import { Buffer } from 'node:buffer'
import { execFileSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { constants, createReadStream } from 'node:fs'
import {
  chmod,
  copyFile,
  lstat,
  mkdir,
  open,
  readdir,
  readFile,
  readlink,
  realpath,
  stat,
  symlink,
  writeFile,
} from 'node:fs/promises'
import path from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const options = {
  source: path.join(root, '.suitecut/native-build'),
  output: path.join(root, '.suitecut/action-performance/runtime'),
  strip: true,
}
const usage =
  'Usage: node scripts/package-browser-runtime.mjs [--source DIR] [--output DIR] [--strip true|false]'
for (let index = 2; index < process.argv.length; ++index) {
  const key = process.argv[index]
  if (key === '--help' || key === '-h') {
    console.log(usage)
    process.exit(0)
  }
  if (
    !['--source', '--output', '--strip'].includes(key) ||
    !process.argv[index + 1] ||
    process.argv[index + 1].startsWith('--')
  )
    throw new Error(usage)
  const value = process.argv[++index]
  if (key === '--strip') {
    if (!['true', 'false'].includes(value)) throw new Error('--strip must be true or false')
    options.strip = value === 'true'
  } else options[key.slice(2)] = path.resolve(value)
}

/**
 * Check whether a path is contained in a directory.
 * @param {string} parent - Parent directory.
 * @param {string} child - Candidate path.
 */
function contains(parent, child) {
  const relative = path.relative(parent, child)
  return (
    relative === '' ||
    (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative))
  )
}

// Resolve existing ancestors as well: output parents may themselves be symlinks.
/**
 * Resolve a destination through its existing ancestors.
 * @param {string} destination - Destination path.
 * @returns {Promise<string>} Canonical destination path.
 */
async function canonicalDestination(destination) {
  try {
    return await realpath(destination)
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
    const parent = path.dirname(destination)
    if (parent === destination) throw error
    return path.join(await canonicalDestination(parent), path.basename(destination))
  }
}

/**
 * Hash a file without buffering its full contents.
 * @param {string} filename - Input file.
 */
async function sha256(filename) {
  const hash = createHash('sha256')
  for await (const chunk of createReadStream(filename)) {
    if (!Buffer.isBuffer(chunk)) throw new Error('Expected binary file stream')
    hash.update(chunk)
  }
  return hash.digest('hex')
}

/**
 * Identify an ELF binary by its magic bytes.
 * @param {string} filename - Input file.
 */
async function isElf(filename) {
  const handle = await open(filename, 'r')
  try {
    const signature = Buffer.alloc(4)
    await handle.read(signature, 0, 4, 0)
    return signature.equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46]))
  } finally {
    await handle.close()
  }
}

async function main() {
  if (process.platform !== 'linux')
    throw new Error('This runtime packager currently supports Linux ELF builds.')
  const source = await realpath(options.source)
  const output = await canonicalDestination(options.output)
  if (contains(source, output) || contains(output, source))
    throw new Error('Source and output must be disjoint directories.')
  try {
    await lstat(options.output)
    throw new Error(`Output already exists; choose a new directory: ${options.output}`)
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
  }
  for (const filename of [
    'suitecut-browser',
    'libcef.so',
    'resources.pak',
    'icudtl.dat',
    'CEF-LICENSE.txt',
  ]) {
    if (!(await stat(path.join(source, filename))).isFile())
      throw new Error(`Missing runtime file: ${filename}`)
  }
  if (!(await stat(path.join(source, 'locales'))).isDirectory())
    throw new Error('Missing runtime locales directory')
  /** @type {unknown} Repository-owned JSON is copied verbatim into provenance. */
  const cefPin = JSON.parse(
    await readFile(path.join(root, 'native/browser/cef-build.json'), 'utf8'),
  )
  execFileSync('readelf', ['--version'], { stdio: 'pipe' })
  const stripVersion = options.strip
    ? execFileSync('strip', ['--version'], { encoding: 'utf8' }).split('\n')[0]
    : null
  await mkdir(path.dirname(output), { recursive: true })
  // Exclusive creation; failures leave a partial directory for inspection, never delete paths.
  await mkdir(output)
  /** @type {Array<{path: string, type: 'file' | 'symlink', mode?: string, originalBytes?: number, packagedBytes?: number, originalSha256?: string, packagedSha256?: string, stripped?: boolean, sourceTarget?: string, target?: string}>} */
  const files = []
  async function copyDirectory(relative = '') {
    const directory = path.join(source, relative)
    const entries = (await readdir(directory, { withFileTypes: true })).sort((a, b) =>
      a.name.localeCompare(b.name),
    )
    for (const entry of entries) {
      const name = path.join(relative, entry.name)
      const from = path.join(source, name)
      const to = path.join(output, name)
      const before = await lstat(from)
      if (entry.isDirectory()) {
        await mkdir(to)
        await copyDirectory(name)
        await chmod(to, before.mode & 0o7777)
      } else if (entry.isSymbolicLink()) {
        const target = await realpath(from)
        if (!contains(source, target))
          throw new Error(`Runtime symlink points outside source: ${name}`)
        const originalTarget = await readlink(from)
        const packagedTarget = path.isAbsolute(originalTarget)
          ? path.relative(path.dirname(to), path.join(output, path.relative(source, target)))
          : originalTarget
        await symlink(packagedTarget, to)
        files.push({
          path: name,
          type: 'symlink',
          sourceTarget: originalTarget,
          target: packagedTarget,
        })
      } else if (entry.isFile()) {
        await copyFile(from, to, constants.COPYFILE_FICLONE | constants.COPYFILE_EXCL)
        const [originalSha256, copiedSha256] = await Promise.all([sha256(from), sha256(to)])
        if (originalSha256 !== copiedSha256)
          throw new Error(`Source changed while copying: ${name}`)
        const elf = await isElf(to)
        if (elf && options.strip)
          execFileSync('strip', ['--strip-unneeded', '--', to], { stdio: 'pipe' })
        await chmod(to, before.mode & 0o7777)
        const after = await stat(to)
        const packagedSha256 = elf && options.strip ? await sha256(to) : copiedSha256
        files.push({
          path: name,
          type: 'file',
          mode: (before.mode & 0o7777).toString(8),
          originalBytes: before.size,
          packagedBytes: after.size,
          originalSha256,
          packagedSha256,
          stripped: elf && options.strip,
        })
      } else throw new Error(`Unsupported runtime entry: ${name}`)
    }
  }
  await copyDirectory()
  /**
   * Sum file sizes from the manifest entries.
   * @param {'originalBytes' | 'packagedBytes'} key - Size field.
   */
  const total = (key) => files.reduce((sum, file) => sum + (file[key] ?? 0), 0)
  const manifest = {
    formatVersion: 1,
    createdAt: new Date().toISOString(),
    source,
    output,
    platform: process.platform,
    architecture: process.arch,
    cefPin,
    strip: { enabled: options.strip, tool: stripVersion, arguments: ['--strip-unneeded'] },
    originalBytes: total('originalBytes'),
    packagedBytes: total('packagedBytes'),
    // ELF load paths are inherited; system shared-library dependencies remain required.
    elfDynamicPaths: files
      .filter(
        (file) =>
          file.stripped === true ||
          (file.type === 'file' && ['suitecut-browser', 'libcef.so'].includes(file.path)),
      )
      .flatMap((file) => {
        const dynamic = execFileSync(
          'readelf',
          ['--dynamic', '--wide', path.join(output, file.path)],
          { encoding: 'utf8' },
        )
        return dynamic
          .split('\n')
          .filter((line) => /\((RPATH|RUNPATH)\)/.test(line))
          .map((value) => ({ path: file.path, value: value.trim() }))
      }),
    files,
  }
  await writeFile(
    path.join(output, 'runtime-manifest.json'),
    `${JSON.stringify(manifest, null, 2)}\n`,
    { flag: 'wx' },
  )
  console.log(
    JSON.stringify(
      {
        output,
        originalBytes: manifest.originalBytes,
        packagedBytes: manifest.packagedBytes,
        savedBytes: manifest.originalBytes - manifest.packagedBytes,
        manifest: path.join(output, 'runtime-manifest.json'),
      },
      null,
      2,
    ),
  )
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : String(error))
  process.exitCode = 1
})
