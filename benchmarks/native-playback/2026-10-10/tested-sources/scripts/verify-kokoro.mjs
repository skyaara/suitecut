import { mkdtemp, readFile, rm, stat } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import process from 'node:process'

import { synthesizeKokoro } from '../dist/kokoro.js'

const directory = await mkdtemp(join(tmpdir(), 'suitecut-kokoro-verify-'))
const outputPath = join(directory, 'kokoro.wav')
const startedAt = Date.now()
let previousStage

try {
  const synthesis = await synthesizeKokoro(
    'SuiteCut narration works.',
    'af_heart',
    1,
    outputPath,
    (progress) => {
      if (progress.stage === previousStage) return
      previousStage = progress.stage
      process.stderr.write(
        `[Kokoro] ${progress.stage} (${String(progress.completedChunks)}/${String(progress.totalChunks)} chunks)\n`,
      )
    },
  )

  const [bytes, metadata] = await Promise.all([readFile(outputPath), stat(outputPath)])
  if (metadata.size <= 44) throw new Error('Kokoro produced an empty WAV file')
  if (bytes.toString('ascii', 0, 4) !== 'RIFF' || bytes.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('Kokoro output is not a RIFF WAVE file')
  }
  if (bytes.readUInt16LE(22) !== 1) throw new Error('Kokoro output must be mono')
  if (bytes.readUInt32LE(24) !== 24_000) throw new Error('Kokoro output must use 24 kHz audio')
  if (bytes.readUInt16LE(34) !== 16) throw new Error('Kokoro output must use 16-bit PCM')
  const durationSeconds = bytes.readUInt32LE(40) / 2 / 24_000
  if (!Number.isFinite(durationSeconds) || durationSeconds <= 0) {
    throw new Error('Kokoro output has no audio duration')
  }
  if (synthesis.text !== 'SuiteCut narration works.') {
    throw new Error('Kokoro timing text does not match the synthesized text')
  }
  if (synthesis.words.map((word) => word.text).join(' ') !== 'SuiteCut narration works') {
    throw new Error('Kokoro did not return one timing for each spoken word')
  }
  for (const [index, word] of synthesis.words.entries()) {
    const previous = synthesis.words[index - 1]
    if (synthesis.text.slice(word.startOffset, word.endOffset) !== word.text) {
      throw new Error(`Kokoro word ${word.text} does not match its text offsets`)
    }
    if (word.startMs < 0 || word.endMs <= word.startMs || word.endMs > synthesis.durationMs) {
      throw new Error(`Kokoro word ${word.text} has invalid relative timing`)
    }
    if (previous !== undefined && word.startMs < previous.endMs) {
      throw new Error(`Kokoro word ${word.text} overlaps the previous word`)
    }
  }

  process.stdout.write(
    `Kokoro synthesized ${durationSeconds.toFixed(2)} seconds of audio with ${String(synthesis.words.length)} word timings in ${(
      (Date.now() - startedAt) /
      1_000
    ).toFixed(2)} seconds.\n`,
  )
} finally {
  await rm(directory, { recursive: true, force: true })
}
