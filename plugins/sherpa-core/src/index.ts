import sherpaOnnx from 'sherpa-onnx'
import { type SuiteCutAudioPlugin } from 'suitecut/audio-plugin'
import { z } from 'zod'

export const sherpaFilePathSchema = z.string().trim().min(1)
export const sherpaPositiveScaleSchema = z.number().finite().positive()

const SpeakerIdSchema = z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER)

export const sherpaCommonOptionsShape = {
  speakerIds: z.record(z.string().trim().min(1), SpeakerIdSchema).exactOptional(),
  numThreads: z.number().int().positive().max(16).exactOptional(),
  debug: z.boolean().exactOptional(),
  ruleFsts: z.array(sherpaFilePathSchema).exactOptional(),
  ruleFars: z.array(sherpaFilePathSchema).exactOptional(),
  maxNumSentences: z.number().int().positive().exactOptional(),
  silenceScale: sherpaPositiveScaleSchema.exactOptional(),
}

export interface SherpaCommonOptions {
  speakerIds?: Record<string, number>
  numThreads?: number
  debug?: boolean
  ruleFsts?: string[]
  ruleFars?: string[]
  maxNumSentences?: number
  silenceScale?: number
}

export type SherpaModelFamily =
  'vits' | 'matcha' | 'kokoro' | 'kitten' | 'zipvoice' | 'pocket' | 'supertonic'

export interface SherpaAudioPluginDefinition<Options extends SherpaCommonOptions> {
  family: SherpaModelFamily
  schema: z.ZodType<Options>
  model(options: Options): object
}

type OfflineTts = ReturnType<typeof sherpaOnnx.createOfflineTts>

const engines = new Map<string, OfflineTts>()

function engineFor<Options extends SherpaCommonOptions>(
  definition: SherpaAudioPluginDefinition<Options>,
  configuration: Options,
): OfflineTts {
  const key = `${definition.family}:${JSON.stringify(configuration)}`
  let engine = engines.get(key)
  if (engine === undefined) {
    engine = sherpaOnnx.createOfflineTts({
      model: {
        ...definition.model(configuration),
        numThreads: configuration.numThreads ?? 1,
        debug: configuration.debug ? 1 : 0,
        provider: 'cpu',
      },
      ruleFsts: configuration.ruleFsts?.join(',') ?? '',
      ruleFars: configuration.ruleFars?.join(',') ?? '',
      maxNumSentences: configuration.maxNumSentences ?? 1,
      silenceScale: configuration.silenceScale ?? 0.2,
    })
    engines.set(key, engine)
  }
  return engine
}

function speakerId(configuration: SherpaCommonOptions, voice: string, numSpeakers: number): number {
  const configured = configuration.speakerIds?.[voice]
  let id: number
  if (configured !== undefined) id = configured
  else if (voice === 'default') id = 0
  else if (/^\d+$/u.test(voice)) id = Number(voice)
  else throw new Error(`Sherpa-ONNX speaker ID is not configured for voice ${voice}`)

  if (!Number.isSafeInteger(id)) {
    throw new Error(`Sherpa-ONNX speaker ID is outside the safe integer range: ${voice}`)
  }
  if (numSpeakers > 0 && id >= numSpeakers) {
    throw new Error(
      `Sherpa-ONNX speaker ID ${String(id)} is outside the model's ${String(numSpeakers)} speakers`,
    )
  }
  return id
}

const AudioSchema = z.strictObject({
  samples: z.instanceof(Float32Array).refine((samples) => samples.length > 0, {
    message: 'Sherpa-ONNX returned an empty waveform',
  }),
  sampleRate: z.number().int().positive(),
})

/** Creates one SuiteCut plugin for a single Sherpa model family. */
export function createSherpaAudioPlugin<Options extends SherpaCommonOptions>(
  definition: SherpaAudioPluginDefinition<Options>,
): SuiteCutAudioPlugin {
  return {
    synthesize(request) {
      return Promise.resolve().then(() => {
        const configuration = definition.schema.parse(request.options)
        const engine = engineFor(definition, configuration)
        const audio = AudioSchema.parse(
          engine.generate({
            text: request.text,
            sid: speakerId(configuration, request.voice, engine.numSpeakers),
            speed: request.speed,
          }),
        )
        engine.save(request.outputPath, audio)
      })
    },
    dispose() {
      for (const engine of engines.values()) engine.free()
      engines.clear()
    },
  }
}
