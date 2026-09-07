# ZipVoice audio plugin

Install only this adapter when SuiteCut narration uses a ZipVoice model.

```sh
npm install --save-dev @suitecut/audio-zipvoice
```

```ts
{
  provider: 'zipvoice',
  module: '@suitecut/audio-zipvoice',
  options: {
    tokens: './models/zipvoice/tokens.txt',
    encoder: './models/zipvoice/encoder.onnx',
    decoder: './models/zipvoice/decoder.onnx',
    vocoder: './models/zipvoice/vocoder.onnx',
  },
}
```
