# VITS and Piper audio plugin

Install only this adapter when SuiteCut narration uses a VITS or Piper model.

```sh
npm install --save-dev @suitecut/audio-vits
```

```ts
{
  provider: 'piper-amy',
  module: '@suitecut/audio-vits',
  options: {
    model: './models/piper/en_US-amy-low.onnx',
    tokens: './models/piper/tokens.txt',
    dataDir: './models/piper/espeak-ng-data',
  },
}
```
