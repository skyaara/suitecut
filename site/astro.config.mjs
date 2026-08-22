import { defineConfig } from 'astro/config'

export default defineConfig({
  output: 'static',
  site: 'https://skyaara.github.io',
  base: '/suitecut',
  devToolbar: {
    enabled: false,
  },
  build: {
    format: 'directory',
  },
})
