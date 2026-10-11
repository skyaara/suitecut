import { execFile } from 'node:child_process'
import console from 'node:console'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { promisify } from 'node:util'

import * as z from 'zod'

const release = process.argv.includes('--release')
const packed = z
  .object({ version: z.string(), tarball: z.string() })
  .parse(
    JSON.parse(
      await readFile(
        release ? '.suitecut/release/latest.json' : '.suitecut/beta-packages/latest.json',
        'utf8',
      ),
    ),
  )
if (!process.env.SUITECUT_NATIVE_EXECUTABLE)
  throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to verify the installed package')
const consumer = await mkdtemp(join(tmpdir(), 'suitecut-beta-consumer-'))
try {
  await writeFile(join(consumer, 'package.json'), JSON.stringify({ private: true, type: 'module' }))
  const execute = promisify(execFile)
  await execute(
    'npm',
    ['install', '--ignore-scripts', '--no-audit', '--no-fund', resolve(packed.tarball)],
    { cwd: consumer, maxBuffer: 8 * 1024 * 1024 },
  )
  await writeFile(
    join(consumer, 'smoke.mjs'),
    `
import assert from 'node:assert/strict'
import { DEFAULT_BACKEND, record, renderSuiteCut } from 'suitecut'
import { record as betaRecord } from 'suitecut/beta'
import { record as stableRecord } from 'suitecut/stable'
import { record as playwrightRecord } from 'suitecut/playwright'
import metadata from 'suitecut/package.json' with { type: 'json' }
assert.equal(DEFAULT_BACKEND, 'native')
assert.equal(record, betaRecord)
assert.equal(stableRecord, playwrightRecord)
assert.notEqual(record, stableRecord)
assert.equal(metadata.version, ${JSON.stringify(packed.version)})
assert.equal(metadata.publishConfig.tag ?? 'latest', ${JSON.stringify(release ? 'latest' : 'beta')})
assert.equal(typeof renderSuiteCut, 'function')
let owned
const result = await record('Installed native release', async ({ source, suitecut }) => {
  owned = source
  await suitecut.hold(200)
}, { native: { width: 640, height: 360, framesPerSecond: 30 }, capture: { audio: true } })
assert.equal(result.backend, 'native')
assert.equal(result.manifest.tests[0].projectName, 'native-chromium')
assert.equal(result.manifest.tests[0].attempts[0].status, 'passed')
const attempt = result.manifest.tests[0].attempts[0]
const sourceArtifact = attempt.artifacts.find((artifact) => artifact.role === 'source-video')
assert(sourceArtifact)
assert(attempt.media.find((media) => media.artifactId === sourceArtifact.id).streams.some((stream) => stream.kind === 'audio'))
assert.equal(owned.state, 'closed')
`,
  )
  await execute(process.execPath, ['smoke.mjs'], {
    cwd: consumer,
    timeout: 120000,
    maxBuffer: 8 * 1024 * 1024,
  })
  console.log(
    `Verified installed ${packed.version}: native root default, explicit stable fallback, CEF recording audio, and owned browser cleanup.`,
  )
} finally {
  await rm(consumer, { recursive: true, force: true })
}
