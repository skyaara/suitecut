import { mkdir, rm } from 'node:fs/promises'
import { resolve } from 'node:path'

const barrierDirectory = resolve(import.meta.dirname, '../../.suitecut/retry-parallel-barrier')

export default async function prepareRetryParallelBarrier(): Promise<void> {
  await rm(barrierDirectory, { force: true, recursive: true })
  await mkdir(barrierDirectory, { recursive: true })
}
