import { execFile } from 'node:child_process'
import { access, copyFile, mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { lfsAssets } from './check-lfs-assets.mjs'

const packageRoot = resolve(import.meta.dirname, '..')
const npmExecutable = process.platform === 'win32' ? 'npm.cmd' : 'npm'
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
  files: z.array(z.object({ path: z.string(), size: z.number().int().nonnegative() })),
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
        env: { ...process.env, NODE_PATH: '', npm_config_dry_run: 'false' },
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

const temporaryRoot = await mkdtemp(join(tmpdir(), 'suitecut-package-'))

try {
  const packageJson = PackageJsonSchema.parse(
    JSON.parse(await readFile(join(packageRoot, 'package.json'), 'utf8')),
  )
  assert(packageJson.name === 'suitecut', 'package.json must keep the suitecut package name')
  assert(packageJson.version !== '0.0.0', 'package.json must use a release version')
  assert(packageJson.license === 'MIT', 'package.json license must match LICENSE')
  assert(packageJson.repository?.url, 'package.json must identify its source repository')

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
    'THIRD_PARTY_NOTICES.md',
    'assets/kokoro/LICENSE',
    'assets/kokoro/NOTICE.md',
    'assets/kokoro/af_heart.bin',
    'assets/kokoro/model_quantized.onnx',
    'assets/kokoro/tokenizer.json',
    'dist/audio-plugin.d.ts',
    'dist/audio-plugin.js',
    'dist/cli.js',
    'dist/ffmpeg-builds.js',
    'dist/media-tools.js',
    'dist/media-tool-paths.js',
    'dist/index.d.ts',
    'dist/index.js',
    'dist/native-default.js',
    'dist/native-default.d.ts',
    'native/browser/cef-build.json',
    'native/browser/cmake/RuntimeModules.cmake',
    'native/browser/page/source.h',
    'native/shared/native_aria_formatter.h',
    'native/shared/native_aria_json.h',
    'scripts/sign-native-macos.py',
    'scripts/native_macos_signing.py',
    'native/tools/CMakeLists.txt',
    'native/tools/proof_supervisor.cc',
    'dist/render.d.ts',
    'dist/render.js',
    'package.json',
  ]) {
    assert(packagedPaths.has(requiredPath), `npm package is missing ${requiredPath}`)
  }

  for (const forbiddenPrefix of ['.github/', 'examples/', 'site/', 'src/', 'tests/']) {
    assert(
      ![...packagedPaths].some((path) => path.startsWith(forbiddenPrefix)),
      `npm package contains development files under ${forbiddenPrefix}`,
    )
  }

  // These files are stored in Git LFS; a checkout without LFS packs a 133-byte pointer.
  for (const asset of lfsAssets) {
    assert(
      packResult.files.find((file) => file.path === asset.path)?.size === asset.size,
      `npm package must contain the real ${asset.path}, not a Git LFS pointer`,
    )
  }

  assert(packResult.size < 80 * 1024 * 1024, 'packed tarball exceeds the 80 MB release budget')
  assert(
    packResult.unpackedSize < 110 * 1024 * 1024,
    'installed package exceeds the 110 MB release budget',
  )

  const consumerDirectory = join(temporaryRoot, 'consumer')
  await mkdir(consumerDirectory)
  await writeFile(
    join(consumerDirectory, 'package.json'),
    '{"name":"suitecut-package-smoke","private":true,"type":"module"}\n',
    'utf8',
  )
  const tarballPath = join(temporaryRoot, packResult.filename)
  const consumerTarballPath = join(consumerDirectory, packResult.filename)
  await copyFile(tarballPath, consumerTarballPath)

  // A split audio plugin resolves suitecut/audio-plugin from the consumer's installed core,
  // as the published @suitecut/audio-* packages do. This keeps the plugin contract covered
  // now that those packages live on the audio-plugins branch.
  const pluginDirectory = join(temporaryRoot, 'audio-plugin-fixture')
  await mkdir(pluginDirectory)
  await writeFile(
    join(pluginDirectory, 'package.json'),
    `${JSON.stringify({ name: 'suitecut-audio-plugin-fixture', version: '1.0.0', type: 'module', exports: './index.js', peerDependencies: { suitecut: packageJson.version } }, null, 2)}\n`,
    'utf8',
  )
  await writeFile(
    join(pluginDirectory, 'index.js'),
    `import { writeFile } from 'node:fs/promises'
import { defineSuiteCutAudioPlugin, encodePcm16Wav } from 'suitecut/audio-plugin'

export default defineSuiteCutAudioPlugin({
  async synthesize({ outputPath, speed }) {
    await writeFile(outputPath, encodePcm16Wav(new Float32Array(2400).fill(0.25 * speed), 24000))
  },
})
`,
    'utf8',
  )
  const pluginPacked = await run(
    npmExecutable,
    ['pack', '--ignore-scripts', '--json', '--pack-destination', consumerDirectory],
    pluginDirectory,
  )
  const pluginPackResult = z.array(PackResultSchema).parse(JSON.parse(pluginPacked.stdout))[0]
  if (pluginPackResult === undefined) throw new Error('npm pack did not return the plugin fixture')

  await run(
    npmExecutable,
    [
      'install',
      '--ignore-scripts',
      '--no-audit',
      '--no-fund',
      consumerTarballPath,
      join(consumerDirectory, pluginPackResult.filename),
    ],
    consumerDirectory,
  )

  await writeFile(
    join(consumerDirectory, 'smoke.mjs'),
    `import { defineSuiteCut, record, renderSuiteCut } from 'suitecut'
import { defineSuiteCutAudioPlugin, encodePcm16Wav } from 'suitecut/audio-plugin'
import { renderSuiteCut as renderFromSubpath } from 'suitecut/render'
import { launchNativeBrowser, createNativeBroadcast, recordNative, createNativePage } from 'suitecut/native'
import { DEFAULT_BACKEND, record as betaRecord } from 'suitecut/beta'
import { SUITECUT_EVENT_ATTACHMENT, SUITECUT_MANIFEST_SCHEMA_VERSION } from 'suitecut/types'
import { readFile } from 'node:fs/promises'
import fixturePlugin from 'suitecut-audio-plugin-fixture'

if (DEFAULT_BACKEND !== 'native' || typeof betaRecord !== 'function' || betaRecord !== record) throw new Error('suitecut beta default is incorrect')
if (typeof launchNativeBrowser !== 'function' || typeof createNativeBroadcast !== 'function' || typeof recordNative !== 'function' || typeof createNativePage !== 'function') {
  throw new Error('suitecut native exports are unavailable')
}
if (typeof renderSuiteCut !== 'function' || renderSuiteCut !== renderFromSubpath) {
  throw new Error('suitecut render exports do not match')
}
if (typeof defineSuiteCut !== 'function' || typeof record !== 'function') {
  throw new Error('suitecut recorder export is unavailable')
}
if (typeof defineSuiteCutAudioPlugin !== 'function' || typeof encodePcm16Wav !== 'function') {
  throw new Error('suitecut audio plugin export is unavailable')
}
if (SUITECUT_EVENT_ATTACHMENT !== 'suitecut-events.json') {
  throw new Error('suitecut types export is unavailable')
}
if (SUITECUT_MANIFEST_SCHEMA_VERSION !== 1) {
  throw new Error('suitecut manifest schema version export is unavailable')
}
await fixturePlugin.synthesize({ text: 'Hello', voice: 'fixture', speed: 1, outputPath: 'plugin.wav' })
const wav = await readFile('plugin.wav')
if (wav.toString('ascii', 0, 4) !== 'RIFF' || wav.toString('ascii', 8, 12) !== 'WAVE' || wav.length !== 44 + 2400 * 2) {
  throw new Error('split audio plugin did not write a PCM WAV through suitecut/audio-plugin')
}
`,
    'utf8',
  )
  await run(process.execPath, ['smoke.mjs'], consumerDirectory)

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
    `Verified ${packResult.id}. Native public imports, split audio plugin and CLI smoke checks passed.\n`,
  )
} finally {
  await rm(temporaryRoot, { recursive: true, force: true })
}
