import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
} from '@suitecut/audio-sherpa-core'
import { z } from 'zod'

const OptionsSchema = z.strictObject({
  ...sherpaCommonOptionsShape,
  durationPredictor: sherpaFilePathSchema,
  textEncoder: sherpaFilePathSchema,
  vectorEstimator: sherpaFilePathSchema,
  vocoder: sherpaFilePathSchema,
  ttsJson: sherpaFilePathSchema,
  unicodeIndexer: sherpaFilePathSchema,
  voiceStyle: sherpaFilePathSchema,
})

export default createSherpaAudioPlugin({
  family: 'supertonic',
  schema: OptionsSchema,
  model: (options) => ({
    supertonic: {
      durationPredictor: options.durationPredictor,
      textEncoder: options.textEncoder,
      vectorEstimator: options.vectorEstimator,
      vocoder: options.vocoder,
      ttsJson: options.ttsJson,
      unicodeIndexer: options.unicodeIndexer,
      voiceStyle: options.voiceStyle,
    },
  }),
})
