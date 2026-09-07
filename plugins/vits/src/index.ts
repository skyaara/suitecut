import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
  sherpaPositiveScaleSchema,
} from '@suitecut/audio-sherpa-core'
import { z } from 'zod'

const OptionsSchema = z.strictObject({
  ...sherpaCommonOptionsShape,
  model: sherpaFilePathSchema,
  tokens: sherpaFilePathSchema,
  lexicon: sherpaFilePathSchema.exactOptional(),
  dataDir: sherpaFilePathSchema.exactOptional(),
  noiseScale: sherpaPositiveScaleSchema.exactOptional(),
  noiseScaleW: sherpaPositiveScaleSchema.exactOptional(),
  lengthScale: sherpaPositiveScaleSchema.exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'vits',
  schema: OptionsSchema,
  model: (options) => ({
    vits: {
      model: options.model,
      tokens: options.tokens,
      ...(options.lexicon === undefined ? {} : { lexicon: options.lexicon }),
      ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
      ...(options.noiseScale === undefined ? {} : { noiseScale: options.noiseScale }),
      ...(options.noiseScaleW === undefined ? {} : { noiseScaleW: options.noiseScaleW }),
      ...(options.lengthScale === undefined ? {} : { lengthScale: options.lengthScale }),
    },
  }),
})
