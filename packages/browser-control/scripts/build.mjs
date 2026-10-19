// SPDX-License-Identifier: MIT
/* eslint jsdoc/no-types: "off", jsdoc/check-tag-names: ["error", { "typed": false }] -- JavaScript build scripts use JSDoc for type checking. */
import { spawnSync } from 'node:child_process'
import { readFileSync, readdirSync, rmSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, join, relative, resolve } from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

import * as ts from 'typescript'

const packageDir = fileURLToPath(new URL('../', import.meta.url))
const dist = join(packageDir, 'dist')
const require = createRequire(import.meta.url)
rmSync(dist, { recursive: true, force: true })
const compilation = spawnSync(process.execPath, [require.resolve('typescript/bin/tsc'), '-p', 'tsconfig.json'], {
  cwd: packageDir,
  stdio: 'inherit',
})
if (compilation.status !== 0) process.exit(compilation.status ?? 1)

// TypeScript emits JavaScript even for modules reached only through types.
// Keep the runtime closure of every public entry, including dynamic imports;
// retain all declarations so their transitive type imports continue to resolve.
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- This is the trusted local package manifest.
const manifest = /** @type {{main: string, exports: object, bin: object}} */ (JSON.parse(readFileSync(join(packageDir, 'package.json'), 'utf8')))
/**
 * Collect public entry paths.
 * @param {string | object} value
 * @returns {string[]}
 */
function strings(value) {
  return typeof value === 'string' ? [value] : Object.values(value).flatMap(strings)
}
const entries = strings([manifest.main, manifest.exports, manifest.bin])
  .filter((entry) => entry.endsWith('.js'))
/** @type {Set<string>} */
const reachable = new Set()
/**
 * Follow emitted local runtime imports.
 * @param {string} file
 */
function visit(file) {
  if (reachable.has(file)) return
  if (!relative(dist, file).startsWith('..')) reachable.add(file)
  else throw new Error(`Runtime import outside dist: ${file}`)
  const source = readFileSync(file, 'utf8')
  const parsed = ts.createSourceFile(file, source, ts.ScriptTarget.ESNext, true, ts.ScriptKind.JS)
  /**
   * Reject imports whose target cannot be determined while packaging.
   * @param {import('typescript').Node} node
   */
  function checkDynamic(node) {
    if (ts.isCallExpression(node) &&
      (node.expression.kind === ts.SyntaxKind.ImportKeyword ||
        (ts.isIdentifier(node.expression) && node.expression.text === 'require')) &&
      (!node.arguments[0] || !ts.isStringLiteralLike(node.arguments[0]))) {
      throw new Error(`Cannot safely package a computed runtime import in ${file}`)
    }
    ts.forEachChild(node, checkDynamic)
  }
  checkDynamic(parsed)
  for (const { fileName } of ts.preProcessFile(source, true, true).importedFiles) {
    if (fileName.startsWith('.')) visit(resolve(dirname(file), fileName))
  }
}
for (const entry of entries) visit(resolve(packageDir, entry))
for (const file of readdirSync(dist, { recursive: true, encoding: 'utf8' })) {
  if (file.endsWith('.js') && !reachable.has(join(dist, file))) rmSync(join(dist, file))
}
