import { randomUUID } from 'node:crypto'
import { mkdir, readFile, rename, stat, unlink, writeFile } from 'node:fs/promises'
import { homedir } from 'node:os'
import { join } from 'node:path'

import * as ort from 'onnxruntime-web/wasm'
import { phonemize } from 'phonemizer'
import { z } from 'zod'

import { writeFileAtomic } from './atomic-file.js'
import { encodePcm16Wav } from './audio-plugin.js'
import { type SuiteCutWordTiming } from './types.js'

const SAMPLE_RATE = 24_000
const KOKORO_REVISION = '1939ad2a8e416c0acfeecc08a694d14ef25f2231'
const MODEL_BASE_URL = `https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/${KOKORO_REVISION}`
const ASSET_DOWNLOAD_TIMEOUT_MS = 30_000
const BUNDLED_MODEL_ASSET = new URL('../assets/kokoro/model_quantized.onnx', import.meta.url)
const BUNDLED_TOKENIZER_ASSET = new URL('../assets/kokoro/tokenizer.json', import.meta.url)
const BUNDLED_DEFAULT_VOICE_ASSET = new URL('../assets/kokoro/af_heart.bin', import.meta.url)

export const SUITECUT_KOKORO_VOICES = [
  'af_heart',
  'af_alloy',
  'af_aoede',
  'af_bella',
  'af_jessica',
  'af_kore',
  'af_nicole',
  'af_nova',
  'af_river',
  'af_sarah',
  'af_sky',
  'am_adam',
  'am_echo',
  'am_eric',
  'am_fenrir',
  'am_liam',
  'am_michael',
  'am_onyx',
  'am_puck',
  'am_santa',
  'bf_alice',
  'bf_emma',
  'bf_isabella',
  'bf_lily',
  'bm_daniel',
  'bm_fable',
  'bm_george',
  'bm_lewis',
] as const

const KOKORO_VOICE_SET = new Set<string>(SUITECUT_KOKORO_VOICES)
const VOICE_STYLE_WIDTH = 256
const VOICE_STYLE_COUNT = 510
const VOICE_PROFILE_FLOAT_COUNT = VOICE_STYLE_COUNT * VOICE_STYLE_WIDTH

export type SuiteCutKokoroProgressStage =
  'loading-model' | 'loading-voice' | 'synthesizing' | 'writing-audio'

export interface SuiteCutKokoroProgress {
  stage: SuiteCutKokoroProgressStage
  completedChunks: number
  totalChunks: number
}

export interface SuiteCutKokoroSynthesisResult {
  text: string
  durationMs: number
  words: SuiteCutWordTiming[]
}

interface KokoroTextWord {
  text: string
  startOffset: number
  endOffset: number
}

interface KokoroPhonemeRun {
  startToken: number
  endToken: number
}

interface KokoroAudioChunk {
  samples: Float32Array
  words: SuiteCutWordTiming[]
}

const KokoroTokenizerSchema = z.object({
  model: z.object({
    vocab: z.record(z.string(), z.number().int().nonnegative()),
  }),
})

const KokoroSynthesisRequestSchema = z
  .object({
    text: z.string().trim().min(1),
    voice: z.enum(SUITECUT_KOKORO_VOICES),
    speed: z.number().finite().min(0.5).max(2),
    outputPath: z.string().trim().min(1),
  })
  .strict()

let session: ort.InferenceSession | undefined
let vocabulary: Record<string, number> | undefined
const voices = new Map<string, Float32Array>()

function cacheDirectory(): string {
  const configured = process.env.SUITECUT_MODEL_CACHE
  if (configured !== undefined && configured.trim().length > 0) return configured
  if (process.platform === 'darwin')
    return join(homedir(), 'Library', 'Caches', 'SuiteCut', 'kokoro-82m-v1.0')
  const xdgCache = process.env.XDG_CACHE_HOME
  return join(
    xdgCache?.trim() ? xdgCache : join(homedir(), '.cache'),
    'suitecut',
    'kokoro-82m-v1.0',
  )
}

async function isUsableFile(path: string): Promise<boolean> {
  try {
    return (await stat(path)).size > 0
  } catch {
    return false
  }
}

async function cachedAsset(filename: string, remotePath: string): Promise<string> {
  const directory = cacheDirectory()
  const destination = join(directory, filename)
  if (await isUsableFile(destination)) return destination

  await mkdir(directory, { recursive: true })
  const response = await fetch(`${MODEL_BASE_URL}/${remotePath}`, {
    signal: AbortSignal.timeout(ASSET_DOWNLOAD_TIMEOUT_MS),
  })
  if (!response.ok) {
    throw new Error(`Kokoro asset download failed with HTTP ${response.status}: ${remotePath}`)
  }
  const bytes = new Uint8Array(await response.arrayBuffer())
  if (bytes.byteLength === 0) throw new Error(`Kokoro asset download was empty: ${remotePath}`)

  const temporary = join(directory, `.${filename}.${process.pid}.${randomUUID()}.tmp`)
  await writeFile(temporary, bytes)
  try {
    await rename(temporary, destination)
  } catch (error) {
    if (!(await isUsableFile(destination))) throw error
    await unlink(temporary).catch(() => undefined)
  }
  return destination
}

function alignedFloat32(bytes: Uint8Array): Float32Array {
  if (bytes.byteLength % Float32Array.BYTES_PER_ELEMENT !== 0) {
    throw new Error('Kokoro voice profile has an invalid byte length')
  }
  const aligned = new Uint8Array(bytes.byteLength)
  aligned.set(bytes)
  return new Float32Array(aligned.buffer)
}

async function prepareKokoro(): Promise<void> {
  if (session !== undefined && vocabulary !== undefined) return
  const [modelBytes, tokenizerBytes] = await Promise.all([
    readFile(BUNDLED_MODEL_ASSET),
    readFile(BUNDLED_TOKENIZER_ASSET, 'utf8'),
  ])
  const tokenizer = KokoroTokenizerSchema.parse(JSON.parse(tokenizerBytes))

  ort.env.wasm.numThreads = 1
  session = await ort.InferenceSession.create(new Uint8Array(modelBytes), {
    executionProviders: ['wasm'],
    executionMode: 'sequential',
    graphOptimizationLevel: 'all',
    enableCpuMemArena: true,
    enableMemPattern: true,
  })
  vocabulary = tokenizer.model.vocab
}

export function normalizeKokoroText(text: string): string {
  return text
    .replace(/[‘’]/gu, "'")
    .replace(/[“”]/gu, '"')
    .replace(/\bDr\.(?=\s+[A-Z])/gu, 'Doctor')
    .replace(/\bMr\.(?=\s+[A-Z])/gu, 'Mister')
    .replace(/\bMs\.(?=\s+[A-Z])/gu, 'Miss')
    .replace(/\bMrs\.(?=\s+[A-Z])/gu, 'Mrs')
    .replace(/(?<=\d),(?=\d)/gu, '')
    .replace(/(?<=\d)-(?=\d)/gu, ' to ')
    .replace(/\s+/gu, ' ')
    .trim()
}

export function splitKokoroText(text: string, maximumLength = 360): string[] {
  if (text.length <= maximumLength) return [text]
  const clauses = text.match(/[^,;:!?—]+(?:[,;:!?—]+|$)/gu) ?? [text]
  const chunks: string[] = []
  let current = ''

  const pushWords = (clause: string): void => {
    for (const word of clause.trim().split(/\s+/u)) {
      if (current.length > 0 && current.length + word.length + 1 > maximumLength) {
        chunks.push(current.trim())
        current = word
      } else {
        current += `${current.length > 0 ? ' ' : ''}${word}`
      }
    }
  }

  for (const clause of clauses) {
    if (clause.length > maximumLength) {
      if (current.length > 0) chunks.push(current.trim())
      current = ''
      pushWords(clause)
    } else if (current.length > 0 && current.length + clause.length + 1 > maximumLength) {
      chunks.push(current.trim())
      current = clause.trim()
    } else {
      current += `${current.length > 0 ? ' ' : ''}${clause.trim()}`
    }
  }
  if (current.length > 0) chunks.push(current.trim())
  return chunks.filter((chunk) => chunk.length > 0)
}

async function phonemizeText(text: string, language: 'en-us' | 'en-gb'): Promise<string> {
  const punctuation = /(\s*[;:,.!?¡¿—…"«»“”()[\]{}]+\s*)+/gu
  const sections: { punctuation: boolean; text: string }[] = []
  let lastIndex = 0
  for (const match of text.matchAll(punctuation)) {
    const index = match.index ?? 0
    if (lastIndex < index) sections.push({ punctuation: false, text: text.slice(lastIndex, index) })
    sections.push({ punctuation: true, text: match[0] })
    lastIndex = index + match[0].length
  }
  if (lastIndex < text.length) sections.push({ punctuation: false, text: text.slice(lastIndex) })

  const parts = await Promise.all(
    sections.map(async (section) => {
      if (section.punctuation) return section.text
      return (await phonemize(section.text, language)).join(' ')
    }),
  )
  return parts.join('').replace(/r/gu, 'ɹ').replace(/x/gu, 'k').replace(/ɬ/gu, 'l').trim()
}

async function voiceData(voice: string): Promise<Float32Array> {
  const existing = voices.get(voice)
  if (existing !== undefined) return existing
  const voiceAsset =
    voice === 'af_heart'
      ? BUNDLED_DEFAULT_VOICE_ASSET
      : await cachedAsset(`${voice}.bin`, `voices/${voice}.bin`)
  const data = alignedFloat32(await readFile(voiceAsset))
  if (data.length !== VOICE_PROFILE_FLOAT_COUNT) {
    throw new Error(`Kokoro voice profile has an invalid size: ${voice}`)
  }
  if (!data.every(Number.isFinite))
    throw new Error(`Kokoro voice profile contains invalid values: ${voice}`)
  voices.set(voice, data)
  return data
}

function textWords(text: string, language: 'en-us' | 'en-gb'): KokoroTextWord[] {
  const segmenter = new Intl.Segmenter(language, { granularity: 'word' })
  return Array.from(segmenter.segment(text))
    .filter((segment) => segment.isWordLike)
    .map((segment) => ({
      text: segment.segment,
      startOffset: segment.index,
      endOffset: segment.index + segment.segment.length,
    }))
}

function phonemeRuns(tokenCharacters: readonly string[]): KokoroPhonemeRun[] {
  const runs: KokoroPhonemeRun[] = []
  let startToken: number | undefined
  for (const [index, character] of tokenCharacters.entries()) {
    if (character === ' ') {
      if (startToken !== undefined) runs.push({ startToken, endToken: index })
      startToken = undefined
    } else {
      startToken ??= index
    }
  }
  if (startToken !== undefined) runs.push({ startToken, endToken: tokenCharacters.length })
  return runs
}

function allocatePhonemeRuns(weights: readonly number[], runCount: number): number[] {
  if (weights.length === 0 || runCount < weights.length) return []
  const totalWeight = weights.reduce((total, weight) => total + Math.max(1, weight), 0)
  const boundaries = [0]
  let cumulativeWeight = 0
  for (let index = 0; index < weights.length - 1; index += 1) {
    cumulativeWeight += Math.max(1, weights[index] ?? 1)
    const previous = boundaries.at(-1) ?? 0
    const remainingWords = weights.length - index - 1
    const proportional = Math.round((cumulativeWeight / totalWeight) * runCount)
    boundaries.push(Math.max(previous + 1, Math.min(proportional, runCount - remainingWords)))
  }
  boundaries.push(runCount)
  return boundaries
}

async function wordRunWeights(
  words: readonly KokoroTextWord[],
  language: 'en-us' | 'en-gb',
  knownCharacter: (character: string) => boolean,
): Promise<number[]> {
  return Promise.all(
    words.map(async (word) => {
      const phonemes = await phonemizeText(word.text, language)
      return Math.max(1, phonemeRuns(Array.from(phonemes).filter(knownCharacter)).length)
    }),
  )
}

async function generateChunk(
  text: string,
  voice: string,
  speed: number,
): Promise<KokoroAudioChunk> {
  await prepareKokoro()
  if (session === undefined || vocabulary === undefined)
    throw new Error('Kokoro did not finish loading')
  const language = voice.startsWith('b') ? 'en-gb' : 'en-us'
  const phonemes = await phonemizeText(text, language)
  const tokens = Array.from(phonemes)
    .map((character) => ({ character, id: vocabulary?.[character] }))
    .filter((token): token is { character: string; id: number } => token.id !== undefined)
    .slice(0, 510)
  const tokenIds = tokens.map((token) => token.id)
  if (tokenIds.length === 0) throw new Error('Kokoro could not tokenize the narration text')

  const inputIds = [0, ...tokenIds, 0]
  const selectedVoice = await voiceData(voice)
  const styleIndex = Math.min(Math.max(inputIds.length - 2, 0), 509)
  const style = selectedVoice.slice(
    styleIndex * VOICE_STYLE_WIDTH,
    (styleIndex + 1) * VOICE_STYLE_WIDTH,
  )
  if (style.length !== VOICE_STYLE_WIDTH)
    throw new Error(`Kokoro voice profile is invalid: ${voice}`)

  const output = await session.run({
    input_ids: new ort.Tensor('int64', BigInt64Array.from(inputIds.map(BigInt)), [
      1,
      inputIds.length,
    ]),
    style: new ort.Tensor('float32', style, [1, VOICE_STYLE_WIDTH]),
    speed: new ort.Tensor('float32', new Float32Array([speed]), [1]),
  })
  const primaryOutputName = session.outputNames.at(0)
  const waveform =
    output.waveform ?? (primaryOutputName === undefined ? undefined : output[primaryOutputName])
  if (waveform === undefined) throw new Error('Kokoro did not return a waveform')
  if (!(waveform.data instanceof Float32Array)) {
    throw new Error(`Kokoro returned an unexpected waveform type: ${waveform.type}`)
  }
  if (waveform.data.length === 0) throw new Error('Kokoro returned an empty waveform')
  const samples = Float32Array.from(waveform.data)
  if (!samples.every(Number.isFinite))
    throw new Error('Kokoro returned non-finite waveform samples')
  const duration = output.duration
  if (duration?.type !== 'int64') {
    throw new Error('Kokoro model does not provide token durations')
  }
  const durations = Array.from(duration.data as BigInt64Array, Number)
  if (
    durations.length !== inputIds.length ||
    durations.some((value) => !Number.isSafeInteger(value) || value <= 0)
  ) {
    throw new Error('Kokoro returned invalid token durations')
  }

  const words = textWords(text, language)
  const runs = phonemeRuns(tokens.map((token) => token.character))
  const weights = await wordRunWeights(
    words,
    language,
    (character) => vocabulary?.[character] !== undefined,
  )
  const boundaries = allocatePhonemeRuns(weights, runs.length)
  if (words.length > 0 && boundaries.length === 0) {
    throw new Error('Kokoro could not map token durations to narration words')
  }
  const cumulativeDurations = [0]
  for (const value of durations) {
    cumulativeDurations.push((cumulativeDurations.at(-1) ?? 0) + value)
  }
  const totalDurationUnits = cumulativeDurations.at(-1) ?? 0
  if (totalDurationUnits <= 0) throw new Error('Kokoro returned empty token durations')
  const millisecondsPerUnit = (samples.length / SAMPLE_RATE / totalDurationUnits) * 1_000
  const wordTimings = words.map((word, index): SuiteCutWordTiming => {
    const firstRun = runs[boundaries[index] ?? 0]
    const lastRun = runs[(boundaries[index + 1] ?? 1) - 1]
    if (firstRun === undefined || lastRun === undefined) {
      throw new Error(`Kokoro could not resolve timing for word ${word.text}`)
    }
    const startUnits = cumulativeDurations[1 + firstRun.startToken]
    const endUnits = cumulativeDurations[1 + lastRun.endToken]
    if (startUnits === undefined || endUnits === undefined) {
      throw new Error(`Kokoro returned incomplete timing for word ${word.text}`)
    }
    return {
      ...word,
      startMs: startUnits * millisecondsPerUnit,
      endMs: endUnits * millisecondsPerUnit,
    }
  })
  return { samples, words: wordTimings }
}

function joinAudio(chunks: readonly KokoroAudioChunk[]): Float32Array {
  const firstChunk = chunks.at(0)
  if (firstChunk === undefined) throw new Error('Kokoro produced no audio chunks')
  if (chunks.length === 1) return firstChunk.samples
  const pauseLength = Math.round(SAMPLE_RATE * 0.08)
  const totalLength =
    chunks.reduce((total, chunk) => total + chunk.samples.length, 0) +
    pauseLength * Math.max(0, chunks.length - 1)
  const result = new Float32Array(totalLength)
  let offset = 0
  for (const chunk of chunks) {
    result.set(chunk.samples, offset)
    offset += chunk.samples.length + pauseLength
  }
  return result
}

export async function synthesizeKokoro(
  text: string,
  voice: string,
  speed: number,
  outputPath: string,
  onProgress?: (progress: SuiteCutKokoroProgress) => void,
): Promise<SuiteCutKokoroSynthesisResult> {
  if (!KOKORO_VOICE_SET.has(voice)) throw new Error(`Kokoro voice is unavailable: ${voice}`)
  const request = KokoroSynthesisRequestSchema.parse({ text, voice, speed, outputPath })
  const normalizedText = normalizeKokoroText(request.text)
  const chunks = splitKokoroText(normalizedText)
  onProgress?.({ stage: 'loading-model', completedChunks: 0, totalChunks: chunks.length })
  await prepareKokoro()
  onProgress?.({ stage: 'loading-voice', completedChunks: 0, totalChunks: chunks.length })
  await voiceData(request.voice)
  const audioChunks: KokoroAudioChunk[] = []
  for (const [index, chunk] of chunks.entries()) {
    onProgress?.({
      stage: 'synthesizing',
      completedChunks: index,
      totalChunks: chunks.length,
    })
    audioChunks.push(await generateChunk(chunk, request.voice, request.speed))
  }
  onProgress?.({
    stage: 'writing-audio',
    completedChunks: chunks.length,
    totalChunks: chunks.length,
  })
  const audio = joinAudio(audioChunks)
  await writeFileAtomic(request.outputPath, encodePcm16Wav(audio, SAMPLE_RATE))

  const words: SuiteCutWordTiming[] = []
  const pauseMs = 80
  let textCursor = 0
  let audioOffsetMs = 0
  for (const [index, chunk] of chunks.entries()) {
    const chunkOffset = normalizedText.indexOf(chunk, textCursor)
    if (chunkOffset < 0) throw new Error('Kokoro could not locate a synthesized text chunk')
    const generated = audioChunks[index]
    if (generated === undefined) throw new Error('Kokoro audio chunk is missing')
    words.push(
      ...generated.words.map((word) => ({
        ...word,
        startOffset: chunkOffset + word.startOffset,
        endOffset: chunkOffset + word.endOffset,
        startMs: audioOffsetMs + word.startMs,
        endMs: audioOffsetMs + word.endMs,
      })),
    )
    textCursor = chunkOffset + chunk.length
    audioOffsetMs += (generated.samples.length / SAMPLE_RATE) * 1_000
    if (index < chunks.length - 1) audioOffsetMs += pauseMs
  }
  return {
    text: normalizedText,
    durationMs: (audio.length / SAMPLE_RATE) * 1_000,
    words,
  }
}
