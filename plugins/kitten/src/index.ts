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
  voices: sherpaFilePathSchema,
  tokens: sherpaFilePathSchema,
  dataDir: sherpaFilePathSchema.exactOptional(),
  lengthScale: sherpaPositiveScaleSchema.exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'kitten',
  schema: OptionsSchema,
  model: (options) => ({
    kitten: {
      model: options.model,
      voices: options.voices,
      tokens: options.tokens,
      ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
      ...(options.lengthScale === undefined ? {} : { lengthScale: options.lengthScale }),
    },
  }),
})
