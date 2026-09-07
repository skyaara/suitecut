import process from 'node:process'

import { defineConfig } from 'astro/config'

export default defineConfig({
  output: 'static',
  site: process.env.SUITECUT_SITE_URL ?? 'https://suitecut.aakashreddy.com',
  base: process.env.SUITECUT_SITE_BASE ?? '/',
  trailingSlash: 'never',
  devToolbar: {
    enabled: false,
  },
  build: {
    format: 'directory',
  },
})
