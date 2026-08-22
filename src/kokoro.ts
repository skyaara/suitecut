import { randomUUID } from 'node:crypto'
import { mkdir, readFile, rename, stat, unlink, writeFile } from 'node:fs/promises'
import { homedir } from 'node:os'
import { join } from 'node:path'

import * as ort from 'onnxruntime-web/wasm'
import { phonemize } from 'phonemizer'
import { z } from 'zod'

const SAMPLE_RATE = 24_000
const MODEL_BASE_URL = 'https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/main'
const MODEL_ASSET = 'onnx/model_quantized.onnx'
const TOKENIZER_ASSET = 'tokenizer.json'

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
  const response = await fetch(`${MODEL_BASE_URL}/${remotePath}`)
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
  const [modelPath, tokenizerPath] = await Promise.all([
    cachedAsset('model_quantized.onnx', MODEL_ASSET),
    cachedAsset('tokenizer.json', TOKENIZER_ASSET),
  ])
  const [modelBytes, tokenizerBytes] = await Promise.all([
    readFile(modelPath),
    readFile(tokenizerPath, 'utf8'),
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
  const voicePath = await cachedAsset(`${voice}.bin`, `voices/${voice}.bin`)
  const data = alignedFloat32(await readFile(voicePath))
  if (data.length < VOICE_STYLE_COUNT * VOICE_STYLE_WIDTH) {
    throw new Error(`Kokoro voice profile is incomplete: ${voice}`)
  }
  if (!data.every(Number.isFinite))
    throw new Error(`Kokoro voice profile contains invalid values: ${voice}`)
  voices.set(voice, data)
  return data
}

async function generateChunk(text: string, voice: string, speed: number): Promise<Float32Array> {
  await prepareKokoro()
  if (session === undefined || vocabulary === undefined)
    throw new Error('Kokoro did not finish loading')
  const language = voice.startsWith('b') ? 'en-gb' : 'en-us'
  const phonemes = await phonemizeText(normalizeKokoroText(text), language)
  const tokenIds = Array.from(phonemes)
    .map((character) => vocabulary?.[character])
    .filter((token): token is number => token !== undefined)
    .slice(0, 510)
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
  return samples
}

function joinAudio(chunks: readonly Float32Array[]): Float32Array {
  const firstChunk = chunks.at(0)
  if (firstChunk === undefined) throw new Error('Kokoro produced no audio chunks')
  if (chunks.length === 1) return firstChunk
  const pauseLength = Math.round(SAMPLE_RATE * 0.08)
  const totalLength =
    chunks.reduce((total, chunk) => total + chunk.length, 0) +
    pauseLength * Math.max(0, chunks.length - 1)
  const result = new Float32Array(totalLength)
  let offset = 0
  for (const chunk of chunks) {
    result.set(chunk, offset)
    offset += chunk.length + pauseLength
  }
  return result
}

export function encodePcm16Wav(samples: Float32Array, sampleRate = SAMPLE_RATE): Uint8Array {
  if (samples.length === 0) throw new Error('Cannot encode an empty waveform')
  if (!Number.isSafeInteger(sampleRate) || sampleRate <= 0)
    throw new Error('WAV sample rate must be a positive integer')
  if (!samples.every(Number.isFinite)) throw new Error('Cannot encode non-finite waveform samples')
  const bytes = new Uint8Array(44 + samples.length * 2)
  const view = new DataView(bytes.buffer)
  const writeAscii = (offset: number, value: string): void => {
    for (let index = 0; index < value.length; index += 1)
      view.setUint8(offset + index, value.charCodeAt(index))
  }
  writeAscii(0, 'RIFF')
  view.setUint32(4, 36 + samples.length * 2, true)
  writeAscii(8, 'WAVE')
  writeAscii(12, 'fmt ')
  view.setUint32(16, 16, true)
  view.setUint16(20, 1, true)
  view.setUint16(22, 1, true)
  view.setUint32(24, sampleRate, true)
  view.setUint32(28, sampleRate * 2, true)
  view.setUint16(32, 2, true)
  view.setUint16(34, 16, true)
  writeAscii(36, 'data')
  view.setUint32(40, samples.length * 2, true)
  for (let index = 0; index < samples.length; index += 1) {
    const sourceSample = samples[index]
    if (sourceSample === undefined) throw new Error(`Waveform sample ${index} is missing`)
    const sample = Math.max(-1, Math.min(1, sourceSample))
    view.setInt16(44 + index * 2, Math.round(sample < 0 ? sample * 32768 : sample * 32767), true)
  }
  return bytes
}

export async function synthesizeKokoro(
  text: string,
  voice: string,
  speed: number,
  outputPath: string,
): Promise<void> {
  if (!KOKORO_VOICE_SET.has(voice)) throw new Error(`Kokoro voice is unavailable: ${voice}`)
  const request = KokoroSynthesisRequestSchema.parse({ text, voice, speed, outputPath })
  const chunks = splitKokoroText(normalizeKokoroText(request.text))
  const audioChunks: Float32Array[] = []
  for (const chunk of chunks)
    audioChunks.push(await generateChunk(chunk, request.voice, request.speed))
  await writeFile(request.outputPath, encodePcm16Wav(joinAudio(audioChunks)))
}
