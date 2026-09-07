# Supertonic audio plugin

Install only this adapter when SuiteCut narration uses a Supertonic model.

```sh
npm install --save-dev @suitecut/audio-supertonic
```

```ts
{
  provider: 'supertonic',
  module: '@suitecut/audio-supertonic',
  options: {
    durationPredictor: './models/supertonic/duration-predictor.onnx',
    textEncoder: './models/supertonic/text-encoder.onnx',
    vectorEstimator: './models/supertonic/vector-estimator.onnx',
    vocoder: './models/supertonic/vocoder.onnx',
    ttsJson: './models/supertonic/tts.json',
    unicodeIndexer: './models/supertonic/unicode-indexer.json',
    voiceStyle: './models/supertonic/voice-style.json',
  },
}
```
