# Sherpa Kokoro audio plugin

Install this adapter for Sherpa-compatible Kokoro model files. SuiteCut's bundled `kokoro`
provider does not need this package.

```sh
npm install --save-dev @suitecut/audio-kokoro-sherpa
```

Configure a provider ID other than the reserved built-in `kokoro` ID.

```ts
{
  provider: 'kokoro-sherpa',
  module: '@suitecut/audio-kokoro-sherpa',
  options: {
    model: './models/kokoro/model.onnx',
    voices: './models/kokoro/voices.bin',
    tokens: './models/kokoro/tokens.txt',
    dataDir: './models/kokoro/espeak-ng-data',
  },
}
```
