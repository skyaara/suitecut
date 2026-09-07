# Matcha audio plugin

Install only this adapter when SuiteCut narration uses a Matcha model.

```sh
npm install --save-dev @suitecut/audio-matcha
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
