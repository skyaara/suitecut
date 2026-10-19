import assert from 'node:assert/strict'
import { execFileSync, spawnSync } from 'node:child_process'
import { mkdtemp, mkdir, readFile, rm, symlink, writeFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import process from 'node:process'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

const script = fileURLToPath(new URL('./package-browser-runtime.mjs', import.meta.url))

await test('runtime packaging preserves execution, resources and originals, and guards output paths', async () => {
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'suitecut-runtime-package-'))
  try {
    const source = path.join(temporary, 'source')
    await mkdir(source)
    await mkdir(path.join(source, 'locales'))
    const code = path.join(temporary, 'probe.c')
    await writeFile(code, 'int main(void) { return 37; }\n')
    execFileSync('cc', ['-g', code, '-o', path.join(source, 'suitecut-browser')])
    execFileSync('cc', ['-g', '-shared', '-fPIC', code, '-o', path.join(source, 'libcef.so')])
    for (const name of [
      'resources.pak',
      'icudtl.dat',
      'CEF-LICENSE.txt',
      'locales/en-US.pak',
      'locales/ja.pak',
    ])
      await writeFile(path.join(source, name), `preserve ${name}\n`)
    await symlink('libcef.so', path.join(source, 'libcef-alias.so'))
    const original = await readFile(path.join(source, 'suitecut-browser'))
    /**
     * Invoke packaging with an isolated output directory.
     * @param {string} output - Output path.
     * @param {string} [strip] - Whether to strip ELF files.
     */
    const invoke = (output, strip = 'true') =>
      spawnSync(
        process.execPath,
        [script, '--source', source, '--output', output, '--strip', strip],
        { encoding: 'utf8' },
      )
    const output = path.join(temporary, 'production')
    const packaged = invoke(output)
    assert.equal(packaged.status, 0, packaged.stderr)
    /** @type {unknown} */
    const manifest = JSON.parse(await readFile(path.join(output, 'runtime-manifest.json'), 'utf8'))
    assert.ok(typeof manifest === 'object' && manifest !== null)
    assert.ok('packagedBytes' in manifest && typeof manifest.packagedBytes === 'number')
    assert.ok('originalBytes' in manifest && typeof manifest.originalBytes === 'number')
    assert.ok(
      'cefPin' in manifest && typeof manifest.cefPin === 'object' && manifest.cefPin !== null,
    )
    assert.ok('cefVersion' in manifest.cefPin)
    assert.ok(manifest.packagedBytes < manifest.originalBytes)
    assert.equal(manifest.cefPin.cefVersion, '156.0.7+g47dbc1d+chromium-156.0.8078.17')
    assert.equal(spawnSync(path.join(output, 'suitecut-browser')).status, 37)
    assert.deepEqual(await readFile(path.join(source, 'suitecut-browser')), original)
    assert.deepEqual(
      await readFile(path.join(output, 'locales/ja.pak')),
      await readFile(path.join(source, 'locales/ja.pak')),
    )
    assert.deepEqual(
      await readFile(path.join(output, 'libcef-alias.so')),
      await readFile(path.join(output, 'libcef.so')),
    )
    assert.notEqual(invoke(output).status, 0, 'Existing output must be refused')
    assert.notEqual(
      invoke(path.join(source, 'nested-output')).status,
      0,
      'Nested output must be refused',
    )
    const parentAlias = path.join(temporary, 'source-alias')
    await symlink(source, parentAlias)
    assert.notEqual(
      invoke(path.join(parentAlias, 'nested-output')).status,
      0,
      'Symlinked source parent must be refused',
    )
    const unstripped = path.join(temporary, 'unstripped')
    assert.equal(invoke(unstripped, 'false').status, 0)
    assert.deepEqual(await readFile(path.join(unstripped, 'suitecut-browser')), original)
    await symlink('/bin/true', path.join(source, 'external'))
    const escaping = invoke(path.join(temporary, 'escaping'))
    assert.notEqual(escaping.status, 0)
    assert.match(escaping.stderr, /outside source/)
  } finally {
    await rm(temporary, { recursive: true, force: true })
  }
})
