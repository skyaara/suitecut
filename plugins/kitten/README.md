# KittenTTS audio plugin

Install only this adapter when SuiteCut narration uses a KittenTTS model.

```sh
npm install --save-dev ./suitecut-audio-sherpa-core-2.1.0.tgz ./suitecut-audio-kitten-2.1.0.tgz
```

```ts
{
  provider: 'kitten-nano',
  module: '@suitecut/audio-kitten',
  options: {
    model: './models/kitten/model.onnx',
    voices: './models/kitten/voices.bin',
    tokens: './models/kitten/tokens.txt',
    dataDir: './models/kitten/espeak-ng-data',
  },
}
```
