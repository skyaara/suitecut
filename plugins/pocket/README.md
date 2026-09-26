# Pocket TTS audio plugin

Install only this adapter when SuiteCut narration uses a Pocket TTS model.

```sh
npm install --save-dev https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-sherpa-core-2.0.0.tgz https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-pocket-2.0.0.tgz
```

```ts
{
  provider: 'pocket',
  module: '@suitecut/audio-pocket',
  options: {
    lmFlow: './models/pocket/lm-flow.onnx',
    lmMain: './models/pocket/lm-main.onnx',
    encoder: './models/pocket/encoder.onnx',
    decoder: './models/pocket/decoder.onnx',
    textConditioner: './models/pocket/text-conditioner.onnx',
    vocabJson: './models/pocket/vocab.json',
    tokenScoresJson: './models/pocket/token-scores.json',
  },
}
```
