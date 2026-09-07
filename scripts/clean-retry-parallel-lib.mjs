import { rm } from 'node:fs/promises'

await rm(new URL('../.suitecut/retry-parallel-lib', import.meta.url), {
  recursive: true,
  force: true,
})
