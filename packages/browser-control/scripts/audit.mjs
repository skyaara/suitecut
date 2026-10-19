// SPDX-License-Identifier: MIT
/* eslint jsdoc/no-types: "off", jsdoc/check-tag-names: ["error", { "typed": false }] -- JavaScript audit scripts use JSDoc for type checking. */
// Run after building. Measures this checkout; does not install or fetch packages.
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, realpathSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, isAbsolute, join, relative, resolve } from 'node:path'
import process from 'node:process'
import { fileURLToPath, pathToFileURL } from 'node:url'

import * as ts from 'typescript'

const packageDir = fileURLToPath(new URL('../', import.meta.url))
const repositoryDir = resolve(packageDir, '../..')
/** @typedef {{name: string, version: string, dependencies?: Record<string, string>, optionalDependencies?: Record<string, string>}} Manifest */
/** @typedef {{bytes: number, diskBytes: number, files: number}} Footprint */
/** @typedef {{ms: number, rss: number, heapUsed: number}} ImportSample */
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- This is the trusted local package manifest.
const manifest = /** @type {Manifest} */ (JSON.parse(readFileSync(join(packageDir, 'package.json'), 'utf8')))
const output = resolve(process.argv[2] ?? join(repositoryDir, '.suitecut/action-performance/package-audit.json'))

/**
 * Run a local measurement command.
 * @param {[string, ...string[]]} args
 * @param {string} cwd
 */
function command(args, cwd = packageDir) {
  const result = spawnSync(args[0], args.slice(1), { cwd, encoding: 'utf8' })
  if (result.status !== 0) throw new Error(`${args.join(' ')} failed: ${result.stderr}`)
  return result.stdout
}

/**
 * Count regular package files without following symlinks.
 * @param {string} directory
 * @returns {Footprint}
 */
function footprint(directory) {
  let bytes = 0
  let diskBytes = 0
  let files = 0
  for (const name of readdirSync(directory)) {
    if (name === 'node_modules') continue
    const file = join(directory, name)
    const stat = lstatSync(file)
    if (stat.isDirectory()) {
      const child = footprint(file)
      bytes += child.bytes
      diskBytes += child.diskBytes
      files += child.files
    } else if (stat.isFile()) {
      bytes += stat.size
      diskBytes += stat.blocks * 512
      files++
    }
  }
  return { bytes, diskBytes, files }
}

/**
 * Resolve an installed package from its requesting package.
 * @param {string} name
 * @param {string} from
 */
function installedPackage(name, from) {
  const require = createRequire(join(from, 'package.json'))
  for (const directory of require.resolve.paths(name) ?? []) {
    const candidate = join(directory, name)
    if (existsSync(join(candidate, 'package.json'))) return realpathSync(candidate)
  }
  return undefined
}

/** @type {Map<string, Footprint & {name: string, version: string, directory: string}>} */
const installed = new Map()
/**
 * Count each real installed dependency directory once.
 * @param {string} name
 * @param {string} from
 */
function dependencyClosure(name, from) {
  const directory = installedPackage(name, from)
  if (!directory) throw new Error(`Required dependency ${name} is not installed from ${from}`)
  if (installed.has(directory)) return
  // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Inspect installed package metadata from the local package manager.
  const dependency = /** @type {Manifest} */ (JSON.parse(readFileSync(join(directory, 'package.json'), 'utf8')))
  installed.set(directory, {
    name: dependency.name,
    version: dependency.version,
    directory: relative(repositoryDir, directory),
    ...footprint(directory),
  })
  for (const child of Object.keys(dependency.dependencies ?? {})) dependencyClosure(child, directory)
  for (const child of Object.keys(dependency.optionalDependencies ?? {})) {
    if (installedPackage(child, directory)) dependencyClosure(child, directory)
  }
}
for (const name of Object.keys(manifest.dependencies ?? {})) dependencyClosure(name, packageDir)

/**
 * Measure static imports from one compiled public entry.
 * @param {string} entry
 */
function staticGraph(entry) {
  /** @type {Set<string>} */
  const visited = new Set()
  /** @type {Set<string>} */
  const externals = new Set()
  /**
   * Follow one local module.
   * @param {string} file
   */
  function visit(file) {
    if (visited.has(file)) return
    visited.add(file)
    const source = ts.createSourceFile(file, readFileSync(file, 'utf8'), ts.ScriptTarget.ESNext, true, ts.ScriptKind.JS)
    for (const statement of source.statements) {
      if (!ts.isImportDeclaration(statement) && !ts.isExportDeclaration(statement)) continue
      const specifier = statement.moduleSpecifier
      if (!specifier || !ts.isStringLiteral(specifier)) continue
      if (specifier.text.startsWith('.')) visit(resolve(dirname(file), specifier.text))
      else externals.add(specifier.text)
    }
  }
  visit(join(packageDir, 'dist', entry))
  return {
    modules: [...visited].map((file) => relative(packageDir, file)).sort(),
    externalImports: [...externals].sort(),
    bytes: [...visited].reduce((sum, file) => sum + lstatSync(file).size, 0),
  }
}

/** @type {ImportSample[]} */
const samples = []
const entryUrl = pathToFileURL(join(packageDir, 'dist/browser.js')).href
for (let index = 0; index < 9; index++) {
  // eslint-disable-next-line @typescript-eslint/no-unsafe-argument -- Parse the fixed measurement program immediately above.
  samples.push(/** @type {ImportSample} */ (JSON.parse(command([
    process.execPath, '--input-type=module', '-e',
    `const start=performance.now();await import(${JSON.stringify(entryUrl)});console.log(JSON.stringify({ms:performance.now()-start,rss:process.memoryUsage().rss,heapUsed:process.memoryUsage().heapUsed}))`,
  ]))))
}
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- npm pack JSON is trusted local tool output.
const [packed] = /** @type {{size: number, unpackedSize: number, entryCount: number, files: {path: string}[]}[]} */ (JSON.parse(command(['npm', 'pack', '--ignore-scripts', '--dry-run', '--json'])))
if (!packed) throw new Error('npm pack returned no package')
const dependencyPackages = [...installed.values()].sort((a, b) => a.name.localeCompare(b.name))
const audit = {
  measuredAt: new Date().toISOString(),
  environment: { node: process.version, platform: process.platform, architecture: process.arch },
  package: {
    name: manifest.name, version: manifest.version, compressedBytes: packed.size,
    unpackedBytes: packed.unpackedSize, fileCount: packed.entryCount,
    nativeRuntimeBundled: false,
    includesNativeBinary: packed.files.some(({ path }) => /\.(so(?:\.|$)|dll$|dylib$|pak$)/u.test(path)),
  },
  productionDependencies: {
    measurement: 'Installed package contents, excluding symlinks and nested node_modules; each real pnpm package directory counted once. Platform-specific optional packages count only when installed. This is not registry tarball size or total repository node_modules.',
    packages: dependencyPackages,
    bytes: dependencyPackages.reduce((sum, dependency) => sum + dependency.bytes, 0),
    diskBytes: dependencyPackages.reduce((sum, dependency) => sum + dependency.diskBytes, 0),
  },
  import: {
    measurement: 'Nine fresh Node processes; import duration excludes process startup. Filesystem caches are warm; samples are not a launchBrowser benchmark.',
    medianMs: samples.map(({ ms }) => ms).sort((a, b) => a - b)[4],
    samples,
    staticGraph: staticGraph('browser.js'),
    cliStaticGraph: staticGraph('browser-control-cli.js'),
  },
  dependencyReasons: {
    sharp: 'Required by the live viewer JPEG encoder, dynamically loaded when createBrowserViewer is called; retained as a dependency so viewer features work after a normal install.',
    yaml: 'Native accessibility snapshot YAML parsing, including existing snapshot APIs.',
    zod: 'Protocol and public input validation throughout the SDK.',
  },
}
mkdirSync(dirname(output), { recursive: true })
writeFileSync(output, `${JSON.stringify(audit, null, 2)}\n`)
console.log(JSON.stringify({ output: isAbsolute(output) ? output : resolve(output), package: audit.package, dependencyBytes: audit.productionDependencies.bytes, importMedianMs: audit.import.medianMs }, null, 2))
