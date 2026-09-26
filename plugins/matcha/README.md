# Matcha audio plugin

Install only this adapter when SuiteCut narration uses a Matcha model.

```sh
npm install --save-dev https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-sherpa-core-2.0.0.tgz https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-matcha-2.0.0.tgz
```

```ts
{
  provider: 'matcha-english',
  module: '@suitecut/audio-matcha',
  options: {
    acousticModel: './models/matcha/model.onnx',
    vocoder: './models/matcha/vocos.onnx',
    tokens: './models/matcha/tokens.txt',
    dataDir: './models/matcha/espeak-ng-data',
  },
}
```
