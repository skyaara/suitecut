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
  lexicon: sherpaFilePathSchema.exactOptional(),
  language: z.string().trim().min(1).exactOptional(),
  lengthScale: sherpaPositiveScaleSchema.exactOptional(),
})

export default createSherpaAudioPlugin({
  family: 'kokoro',
  schema: OptionsSchema,
  model: (options) => ({
    kokoro: {
      model: options.model,
      voices: options.voices,
      tokens: options.tokens,
      ...(options.dataDir === undefined ? {} : { dataDir: options.dataDir }),
      ...(options.lexicon === undefined ? {} : { lexicon: options.lexicon }),
      ...(options.language === undefined ? {} : { lang: options.language }),
      ...(options.lengthScale === undefined ? {} : { lengthScale: options.lengthScale }),
    },
  }),
})
