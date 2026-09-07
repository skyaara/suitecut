import assert from 'node:assert/strict'
import { mkdtemp, readFile, rm, unlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const packageRoot = resolve(import.meta.dirname, '..')
const temporaryDirectory = await mkdtemp(join(tmpdir(), 'suitecut-audio-plugin-worker-'))
const pluginPath = join(temporaryDirectory, 'fixture-plugin.mjs')
const outputPath = join(temporaryDirectory, 'fixture.wav')
const disposedPath = join(temporaryDirectory, 'disposed.txt')
const audioPluginModule = pathToFileURL(resolve(packageRoot, 'dist/audio-plugin.js')).href
let worker

try {
  await writeFile(
    pluginPath,
    `import { writeFile } from 'node:fs/promises'
import { defineSuiteCutAudioPlugin, encodePcm16Wav } from ${JSON.stringify(audioPluginModule)}

export default defineSuiteCutAudioPlugin({
  async synthesize(request) {
    if (request.text !== 'Plugin audio is ready.') throw new Error('Text was not forwarded')
    if (request.voice !== 'fixture-voice') throw new Error('Voice was not forwarded')
    if (request.options?.marker !== 'fixture-options') throw new Error('Options were not forwarded')
    const samples = new Float32Array(800)
    await writeFile(request.outputPath, encodePcm16Wav(samples, 8_000))
  },
  async dispose() {
    await writeFile(${JSON.stringify(disposedPath)}, 'disposed')
  },
})
`,
    'utf8',
  )

  const { SuiteCutNarrationWorker } = await import('../dist/narration-worker-client.js')
  worker = new SuiteCutNarrationWorker()
  await worker.synthesize({
    jobId: '00000000-0000-4000-8000-000000000001',
    text: 'Plugin audio is ready.',
    provider: 'fixture-audio',
    voice: 'fixture-voice',
    speed: 1,
    outputPath,
    plugin: {
      provider: 'fixture-audio',
      module: pathToFileURL(pluginPath).href,
      options: { marker: 'fixture-options' },
    },
  })

  const wav = await readFile(outputPath)
  assert.equal(wav.subarray(0, 4).toString(), 'RIFF')
  assert.equal(wav.subarray(8, 12).toString(), 'WAVE')
  assert.ok(wav.byteLength > 44)
  await worker.close()
  worker = undefined
  assert.equal(await readFile(disposedPath, 'utf8'), 'disposed')

  await unlink(disposedPath)
  const controller = new AbortController()
  worker = new SuiteCutNarrationWorker(controller.signal)
  await worker.synthesize({
    jobId: '00000000-0000-4000-8000-000000000002',
    text: 'Plugin audio is ready.',
    provider: 'fixture-audio',
    voice: 'fixture-voice',
    speed: 1,
    outputPath,
    plugin: {
      provider: 'fixture-audio',
      module: pathToFileURL(pluginPath).href,
      options: { marker: 'fixture-options' },
    },
  })
  controller.abort()
  await worker.close()
  worker = undefined
  assert.equal(await readFile(disposedPath, 'utf8'), 'disposed')
  process.stdout.write(
    'Verified external audio plugin synthesis and disposal on normal and aborted worker shutdown.\n',
  )
} finally {
  await worker?.close()
  await rm(temporaryDirectory, { recursive: true, force: true })
}
