# Pocket TTS audio plugin

Install only this adapter when SuiteCut narration uses a Pocket TTS model.

```sh
npm install --save-dev @suitecut/audio-pocket
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
