import { execFile } from 'node:child_process'
import { access, copyFile, mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const packageRoot = resolve(import.meta.dirname, '..')
const npmExecutable = process.platform === 'win32' ? 'npm.cmd' : 'npm'
const audioPackages = [
  { name: '@suitecut/audio-sherpa-core', directory: 'sherpa-core' },
  { name: '@suitecut/audio-vits', directory: 'vits' },
  { name: '@suitecut/audio-matcha', directory: 'matcha' },
  { name: '@suitecut/audio-kokoro-sherpa', directory: 'kokoro-sherpa' },
  { name: '@suitecut/audio-kitten', directory: 'kitten' },
  { name: '@suitecut/audio-zipvoice', directory: 'zipvoice' },
  { name: '@suitecut/audio-pocket', directory: 'pocket' },
  { name: '@suitecut/audio-supertonic', directory: 'supertonic' },
]

const PackageJsonSchema = z.object({
  name: z.string(),
  version: z.string(),
  license: z.string(),
  repository: z.object({ url: z.string().min(1) }),
})

const PackResultSchema = z.object({
  id: z.string(),
  filename: z.string(),
  entryCount: z.number().int().nonnegative(),
  size: z.number().int().nonnegative(),
  unpackedSize: z.number().int().nonnegative(),
  files: z.array(z.object({ path: z.string() })),
})

/**
 * Runs a child command and captures its text output.
 * @param {string} executable
 * @param {string[]} args
 * @param {string} cwd
 * @returns {Promise<{ stdout: string, stderr: string }>}
 */
function run(executable, args, cwd) {
  return new Promise((resolvePromise, reject) => {
    execFile(
      executable,
      args,
      {
        cwd,
        env: { ...process.env, npm_config_dry_run: 'false' },
        maxBuffer: 20 * 1024 * 1024,
      },
      (error, stdout, stderr) => {
        if (error === null) {
          resolvePromise({ stdout, stderr })
          return
        }
        reject(
          new Error(
            `${executable} ${args.join(' ')} failed\n${stderr.trim() || stdout.trim() || error.message}`,
            { cause: error },
          ),
        )
      },
    )
  })
}

/**
 * Throws when a release invariant is not satisfied.
 * @param {unknown} condition
 * @param {string} message
 */
function assert(condition, message) {
  if (!condition) throw new Error(message)
}

/**
 * Reads whether a path exists.
 * @param {string} path
 * @returns {Promise<boolean>}
 */
async function pathExists(path) {
  return access(path).then(
    () => true,
    () => false,
  )
}

/**
 * Packs and validates one split audio package.
 * @param {{ name: string, directory: string }} audioPackage
 * @param {string} destination
 */
async function packAudioPackage(audioPackage, destination) {
  const result = await run(
    npmExecutable,
    [
      'pack',
      '--ignore-scripts',
      '--json',
      '--pack-destination',
      destination,
      '--workspace',
      audioPackage.name,
    ],
    packageRoot,
  )
  const packagePackResult = z.array(PackResultSchema).parse(JSON.parse(result.stdout))[0]
  if (packagePackResult === undefined) {
    throw new Error(`npm pack did not return a result for ${audioPackage.name}`)
  }
  const paths = new Set(packagePackResult.files.map((file) => file.path))
  for (const requiredPath of ['README.md', 'dist/index.d.ts', 'dist/index.js', 'package.json']) {
    assert(paths.has(requiredPath), `${audioPackage.name} is missing ${requiredPath}`)
  }
  assert(
    ![...paths].some(
      (path) => path.startsWith('src/') || path.endsWith('.onnx') || path.endsWith('.wasm'),
    ),
    `${audioPackage.name} contains source or model files`,
  )
  assert(packagePackResult.size < 100 * 1024, `${audioPackage.name} tarball exceeds 100 KB`)
  return { name: audioPackage.name, result: packagePackResult }
}

const temporaryRoot = await mkdtemp(join(tmpdir(), 'suitecut-package-'))

try {
  const packageJson = PackageJsonSchema.parse(
    JSON.parse(await readFile(join(packageRoot, 'package.json'), 'utf8')),
  )
  assert(packageJson.name === 'suitecut', 'package.json must keep the suitecut package name')
  assert(packageJson.version !== '0.0.0', 'package.json must use a release version')
  assert(packageJson.license === 'MIT', 'package.json license must match LICENSE')
  assert(packageJson.repository?.url, 'package.json must identify its source repository')

  for (const audioPackage of audioPackages) {
    const metadata = PackageJsonSchema.parse(
      JSON.parse(
        await readFile(
          join(packageRoot, 'plugins', audioPackage.directory, 'package.json'),
          'utf8',
        ),
      ),
    )
    assert(metadata.name === audioPackage.name, `${audioPackage.directory} package name changed`)
    assert(
      metadata.version === packageJson.version,
      `${audioPackage.name} version must match SuiteCut`,
    )
    assert(metadata.license === 'MIT', `${audioPackage.name} license must match SuiteCut`)
  }

  const packed = await run(
    npmExecutable,
    ['pack', '--ignore-scripts', '--json', '--pack-destination', temporaryRoot],
    packageRoot,
  )
  const packResults = z.array(PackResultSchema).parse(JSON.parse(packed.stdout))
  const packResult = packResults[0]
  if (packResult === undefined) throw new Error('npm pack did not return a package result')

  const packagedPaths = new Set(packResult.files.map((file) => file.path))
  for (const requiredPath of [
    'LICENSE',
    'README.md',
    'CHANGELOG.md',
    'CODE_OF_CONDUCT.md',
    'CONTRIBUTING.md',
    'SECURITY.md',
    'assets/kokoro/LICENSE',
    'assets/kokoro/NOTICE.md',
    'assets/kokoro/af_heart.bin',
    'assets/kokoro/model_quantized.onnx',
    'assets/kokoro/tokenizer.json',
    'dist/audio-plugin.d.ts',
    'dist/audio-plugin.js',
    'dist/cli.js',
    'dist/index.d.ts',
    'dist/index.js',
    'dist/playwright.d.ts',
    'dist/playwright.js',
    'dist/render.d.ts',
    'dist/render.js',
    'dist/reporter.d.ts',
    'dist/reporter.js',
    'dist/test.d.ts',
    'dist/test.js',
    'package.json',
  ]) {
    assert(packagedPaths.has(requiredPath), `npm package is missing ${requiredPath}`)
  }

  for (const forbiddenPrefix of [
    '.github/',
    'benchmarks/',
    'examples/',
    'plugins/',
    'site/',
    'src/',
    'tests/',
  ]) {
    assert(
      ![...packagedPaths].some((path) => path.startsWith(forbiddenPrefix)),
      `npm package contains development files under ${forbiddenPrefix}`,
    )
  }

  assert(packResult.size < 80 * 1024 * 1024, 'packed tarball exceeds the 80 MB release budget')
  assert(
    packResult.unpackedSize < 110 * 1024 * 1024,
    'installed package exceeds the 110 MB release budget',
  )

  const packedAudio = await Promise.all(
    audioPackages.map((audioPackage) => packAudioPackage(audioPackage, temporaryRoot)),
  )

  const corePackResult = packedAudio.find(
    (audioPackage) => audioPackage.name === '@suitecut/audio-sherpa-core',
  )?.result
  const vitsPackResult = packedAudio.find(
    (audioPackage) => audioPackage.name === '@suitecut/audio-vits',
  )?.result
  if (corePackResult === undefined || vitsPackResult === undefined) {
    throw new Error('Sherpa core or VITS package result is missing')
  }

  const consumerDirectory = join(temporaryRoot, 'consumer')
  await mkdir(consumerDirectory)
  await writeFile(
    join(consumerDirectory, 'package.json'),
    '{"name":"suitecut-package-smoke","private":true,"type":"module"}\n',
    'utf8',
  )
  const tarballPath = join(temporaryRoot, packResult.filename)
  const consumerTarballPath = join(consumerDirectory, packResult.filename)
  const coreTarballPath = join(consumerDirectory, corePackResult.filename)
  const vitsTarballPath = join(consumerDirectory, vitsPackResult.filename)
  await copyFile(tarballPath, consumerTarballPath)
  await copyFile(join(temporaryRoot, corePackResult.filename), coreTarballPath)
  await copyFile(join(temporaryRoot, vitsPackResult.filename), vitsTarballPath)
  await run(
    npmExecutable,
    [
      'install',
      '--ignore-scripts',
      '--no-audit',
      '--no-fund',
      consumerTarballPath,
      coreTarballPath,
      vitsTarballPath,
      '@playwright/test@^1.62.1',
    ],
    consumerDirectory,
  )

  await writeFile(
    join(consumerDirectory, 'smoke.mjs'),
    `import { defineSuiteCut, record, renderSuiteCut } from 'suitecut'
import { defineSuiteCutAudioPlugin, encodePcm16Wav } from 'suitecut/audio-plugin'
import Reporter from 'suitecut/reporter'
import { defineSuiteCut as defineFromAlias, record as recordFromAlias } from 'suitecut/playwright'
import { renderSuiteCut as renderFromSubpath } from 'suitecut/render'
import { expect, test } from 'suitecut/test'
import { SUITECUT_EVENT_ATTACHMENT, SUITECUT_MANIFEST_SCHEMA_VERSION } from 'suitecut/types'
import vitsPlugin from '@suitecut/audio-vits'

if (typeof test !== 'function') throw new Error('suitecut test export is unavailable')
if (typeof expect !== 'function') throw new Error('suitecut expect export is unavailable')
if (typeof renderSuiteCut !== 'function' || renderSuiteCut !== renderFromSubpath) {
  throw new Error('suitecut render exports do not match')
}
if (typeof Reporter !== 'function') throw new Error('suitecut reporter export is unavailable')
if (typeof defineSuiteCut !== 'function' || typeof record !== 'function') {
  throw new Error('suitecut recorder export is unavailable')
}
if (defineSuiteCut !== defineFromAlias || record !== recordFromAlias) {
  throw new Error('suitecut/playwright is not a compatibility alias')
}
if (typeof defineSuiteCutAudioPlugin !== 'function' || typeof encodePcm16Wav !== 'function') {
  throw new Error('suitecut audio plugin export is unavailable')
}
if (typeof vitsPlugin.synthesize !== 'function') {
  throw new Error('VITS audio plugin export is unavailable')
}
if (SUITECUT_EVENT_ATTACHMENT !== 'suitecut-events.json') {
  throw new Error('suitecut types export is unavailable')
}
if (SUITECUT_MANIFEST_SCHEMA_VERSION !== 1) {
  throw new Error('suitecut manifest schema version export is unavailable')
}
`,
    'utf8',
  )
  await run(process.execPath, ['smoke.mjs'], consumerDirectory)

  for (const audioPackage of audioPackages.slice(2)) {
    assert(
      !(await pathExists(join(consumerDirectory, 'node_modules', ...audioPackage.name.split('/')))),
      `VITS-only consumer unexpectedly installed ${audioPackage.name}`,
    )
  }

  const rawConsumerDirectory = join(temporaryRoot, 'raw-consumer')
  await mkdir(rawConsumerDirectory)
  await writeFile(
    join(rawConsumerDirectory, 'package.json'),
    '{"name":"suitecut-raw-package-smoke","private":true,"type":"module"}\n',
    'utf8',
  )
  const rawConsumerTarballPath = join(rawConsumerDirectory, packResult.filename)
  await copyFile(tarballPath, rawConsumerTarballPath)
  await run(
    npmExecutable,
    ['install', '--ignore-scripts', '--no-audit', '--no-fund', rawConsumerTarballPath],
    rawConsumerDirectory,
  )
  await writeFile(
    join(rawConsumerDirectory, 'smoke.mjs'),
    `import { defineSuiteCut, record } from 'suitecut'

if (typeof defineSuiteCut !== 'function' || typeof record !== 'function') {
  throw new Error('suitecut recorder exports are unavailable')
}
`,
    'utf8',
  )
  await run(process.execPath, ['smoke.mjs'], rawConsumerDirectory)
  assert(
    !(await pathExists(
      join(rawConsumerDirectory, 'node_modules', '@playwright', 'test', 'package.json'),
    )),
    'plain Playwright consumer unexpectedly installed @playwright/test',
  )

  const installedCli = join(
    consumerDirectory,
    'node_modules',
    '.bin',
    process.platform === 'win32' ? 'suitecut.cmd' : 'suitecut',
  )
  await access(installedCli)
  const cli =
    process.platform === 'win32'
      ? await run('cmd.exe', ['/d', '/s', '/c', installedCli, '--help'], consumerDirectory)
      : await run(installedCli, ['--help'], consumerDirectory)
  assert(cli.stdout.includes('suitecut render'), 'installed CLI did not print SuiteCut help')

  process.stdout.write(
    `Verified ${packResult.id} and ${String(audioPackages.length)} split audio packages. VITS-only install, Playwright Test, standalone Playwright, and public import smokes passed.\n`,
  )
} finally {
  await rm(temporaryRoot, { recursive: true, force: true })
}
