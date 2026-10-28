import process from 'node:process'

import { defineConfig } from 'astro/config'

export default defineConfig({
  output: 'static',
  site: process.env.SUITECUT_SITE_URL ?? 'https://suitecut.aakashreddy.com',
  base: process.env.SUITECUT_SITE_BASE ?? '/',
  trailingSlash: 'never',
  // Keep the space between text and an inline element that starts the next source line.
  compressHTML: false,
  devToolbar: {
    enabled: false,
  },
  build: {
    format: 'directory',
  },
})
