import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
  sherpaPositiveScaleSchema,
} from '@suitecut/audio-sherpa-core'
import { z } from 'zod'

const OptionsSchema = z.strictObject({
  ...sherpaCommonOptionsShape,
  acousticModel: sherpaFilePathSchema,
  vocoder: sherpaFilePathSchema,
  tokens: sherpaFilePathSchema,
  lexicon: sherpaFilePathSchema.exactOptional(),
  dataDir: sherpaFilePathSchema.exactOptional(),
  noiseScale: sherpaPositiveScaleSchema.exactOptional(),
  lengthScale: sherpaPositiveScaleSchema.exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'matcha',
  schema: OptionsSchema,
  model: (options) => ({
    matcha: {
      acousticModel: options.acousticModel,
      vocoder: options.vocoder,
      tokens: options.tokens,
      ...(options.lexicon === undefined ? {} : { lexicon: options.lexicon }),
      ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
      ...(options.noiseScale === undefined ? {} : { noiseScale: options.noiseScale }),
      ...(options.lengthScale === undefined ? {} : { lengthScale: options.lengthScale }),
    },
  }),
})
