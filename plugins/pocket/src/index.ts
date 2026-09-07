import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
} from '@suitecut/audio-sherpa-core'
import { z } from 'zod'

const OptionsSchema = z.strictObject({
  ...sherpaCommonOptionsShape,
  lmFlow: sherpaFilePathSchema,
  lmMain: sherpaFilePathSchema,
  encoder: sherpaFilePathSchema,
  decoder: sherpaFilePathSchema,
  textConditioner: sherpaFilePathSchema,
  vocabJson: sherpaFilePathSchema,
  tokenScoresJson: sherpaFilePathSchema,
  voiceEmbeddingCacheCapacity: z.number().int().positive().exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'pocket',
  schema: OptionsSchema,
  model: (options) => ({
    pocket: {
      lmFlow: options.lmFlow,
      lmMain: options.lmMain,
      encoder: options.encoder,
      decoder: options.decoder,
      textConditioner: options.textConditioner,
      vocabJson: options.vocabJson,
      tokenScoresJson: options.tokenScoresJson,
      ...(options.voiceEmbeddingCacheCapacity === undefined
        ? {}
        : { voiceEmbeddingCacheCapacity: options.voiceEmbeddingCacheCapacity }),
    },
  }),
})
