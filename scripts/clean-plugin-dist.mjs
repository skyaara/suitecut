import { rm } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

await rm(resolve(process.cwd(), 'dist'), { recursive: true, force: true })
