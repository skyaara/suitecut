import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
  sherpaPositiveScaleSchema,
} from '@suitecut/audio-sherpa-core'
import { z } from 'zod'

const OptionsSchema = z.strictObject({
  ...sherpaCommonOptionsShape,
  tokens: sherpaFilePathSchema,
  encoder: sherpaFilePathSchema,
  decoder: sherpaFilePathSchema,
  vocoder: sherpaFilePathSchema,
  dataDir: sherpaFilePathSchema.exactOptional(),
  lexicon: sherpaFilePathSchema.exactOptional(),
  featScale: sherpaPositiveScaleSchema.exactOptional(),
  tShift: sherpaPositiveScaleSchema.exactOptional(),
  targetRms: sherpaPositiveScaleSchema.exactOptional(),
  guidanceScale: sherpaPositiveScaleSchema.exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'zipvoice',
  schema: OptionsSchema,
  model: (options) => ({
    zipvoice: {
      tokens: options.tokens,
      encoder: options.encoder,
      decoder: options.decoder,
      vocoder: options.vocoder,
      ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
      ...(options.lexicon === undefined ? {} : { lexicon: options.lexicon }),
      ...(options.featScale === undefined ? {} : { featScale: options.featScale }),
      ...(options.tShift === undefined ? {} : { tShift: options.tShift }),
      ...(options.targetRms === undefined ? {} : { targetRMS: options.targetRms }),
      ...(options.guidanceScale === undefined ? {} : { guidanceScale: options.guidanceScale }),
    },
  }),
})
