import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'

import { defineConfig } from 'vitest/config'

export default defineConfig({
  resolve: {
    alias: {
      '@suitecut/audio-sherpa-core': fileURLToPath(
        new URL('./plugins/sherpa-core/src/index.ts', import.meta.url),
      ),
      'sherpa-onnx': createRequire(
        new URL('./plugins/sherpa-core/package.json', import.meta.url),
      ).resolve('sherpa-onnx'),
      'suitecut/audio-plugin': fileURLToPath(new URL('./src/audio-plugin.ts', import.meta.url)),
    },
  },
  test: {
    include: ['tests/**/*.test.ts'],
  },
})
