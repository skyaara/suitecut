# ZipVoice audio plugin

Install only this adapter when SuiteCut narration uses a ZipVoice model.

```sh
npm install --save-dev https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-sherpa-core-2.0.0.tgz https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-zipvoice-2.0.0.tgz
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
