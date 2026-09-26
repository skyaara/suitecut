# Sherpa Kokoro audio plugin

Install this adapter for Sherpa-compatible Kokoro model files. SuiteCut's bundled `kokoro`
provider does not need this package.

```sh
npm install --save-dev https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-sherpa-core-2.0.0.tgz https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-audio-kokoro-sherpa-2.0.0.tgz
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
