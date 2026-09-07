#!/usr/bin/env node

import { spawn } from 'node:child_process'
import { realpathSync } from 'node:fs'
import { createRequire } from 'node:module'
import process from 'node:process'
import { fileURLToPath, pathToFileURL } from 'node:url'

import { renderSuiteCut, type SuiteCutRenderConfig } from './render.js'

const USAGE = `SuiteCut

Usage:
  suitecut test [--manifest <path>] -- [Playwright test options]
  suitecut render --manifest <path> --output <video-path> [options]

Render options:
  --test-id <id>          Select a test. Defaults to the first test.
  --retry <number>        Select a retry. Defaults to the latest attempt.
  --container <format>    mp4, webm, mov, or mkv. Defaults from the output extension.
  --format <format>       Deprecated alias for --container.
  --video-codec <name>    FFmpeg video encoder. Defaults for the container.
  --audio-codec <name>    FFmpeg audio encoder. Defaults for the container.
  --pixel-format <name>   FFmpeg output pixel format. Defaults for the video encoder.
  --color-range <range>   auto, full, or limited. Default auto.
  --width <pixels>        Output width. Default 1920.
  --height <pixels>       Output height. Default 1080.
  --fps <30|60>           Output frame rate. Default 30.
  --quality <profile>     standard, high, or master. Default standard.
  --result-hold-ms <ms>   Freeze the final frame for this duration. Default 0.
  --background-color <c>  Hex or basic named letterbox color. Default #0B1020.
  --failure-mode <mode>   strict or best-effort. Default strict.
  --ffmpeg-path <path>    Executable FFmpeg path.
  --no-narration          Omit narration audio.
`

interface ParsedRenderArguments {
  manifestPath: string
  outputPath: string
  testId?: string
  retry?: number
  config: SuiteCutRenderConfig
}

function requiredValue(args: readonly string[], index: number, option: string): string {
  const value = args[index + 1]
  if (value === undefined || value.startsWith('--')) {
    throw new Error(`${option} requires a value`)
  }
  return value
}

function positiveInteger(value: string, option: string): number {
  const parsed = Number(value)
  if (!Number.isInteger(parsed) || parsed <= 0)
    throw new Error(`${option} requires a positive integer`)
  return parsed
}

function nonNegativeInteger(value: string, option: string): number {
  const parsed = Number(value)
  if (!Number.isInteger(parsed) || parsed < 0)
    throw new Error(`${option} requires a non-negative integer`)
  return parsed
}

function outputContainer(value: string, option: string): 'mp4' | 'webm' | 'mov' | 'mkv' {
  if (value === 'mp4' || value === 'webm' || value === 'mov' || value === 'mkv') return value
  throw new Error(`${option} must be mp4, webm, mov, or mkv`)
}

function parseRenderArguments(args: readonly string[]): ParsedRenderArguments {
  let manifestPath: string | undefined
  let outputPath: string | undefined
  let testId: string | undefined
  let retry: number | undefined
  const output: NonNullable<SuiteCutRenderConfig['output']> = {}
  const config: SuiteCutRenderConfig = { output }

  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index]
    if (argument === undefined) throw new Error(`Missing argument at position ${index}`)
    switch (argument) {
      case '--manifest':
        manifestPath = requiredValue(args, index, argument)
        index += 1
        break
      case '--output':
        outputPath = requiredValue(args, index, argument)
        index += 1
        break
      case '--test-id':
        testId = requiredValue(args, index, argument)
        index += 1
        break
      case '--retry':
        retry = nonNegativeInteger(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--container':
        output.container = outputContainer(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--format':
        output.format = outputContainer(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--video-codec':
        output.videoCodec = requiredValue(args, index, argument)
        index += 1
        break
      case '--audio-codec':
        output.audioCodec = requiredValue(args, index, argument)
        index += 1
        break
      case '--pixel-format':
        output.pixelFormat = requiredValue(args, index, argument)
        index += 1
        break
      case '--color-range': {
        const value = requiredValue(args, index, argument)
        if (value !== 'auto' && value !== 'full' && value !== 'limited') {
          throw new Error('--color-range must be auto, full, or limited')
        }
        output.colorRange = value
        index += 1
        break
      }
      case '--width':
        output.width = positiveInteger(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--height':
        output.height = positiveInteger(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--fps': {
        const value = positiveInteger(requiredValue(args, index, argument), argument)
        if (value !== 30 && value !== 60) throw new Error('--fps must be 30 or 60')
        output.framesPerSecond = value
        index += 1
        break
      }
      case '--quality': {
        const value = requiredValue(args, index, argument)
        if (value !== 'standard' && value !== 'high' && value !== 'master') {
          throw new Error('--quality must be standard, high, or master')
        }
        output.quality = value
        index += 1
        break
      }
      case '--result-hold-ms':
        config.resultHoldMs = nonNegativeInteger(requiredValue(args, index, argument), argument)
        index += 1
        break
      case '--background-color':
        config.backgroundColor = requiredValue(args, index, argument)
        index += 1
        break
      case '--failure-mode': {
        const value = requiredValue(args, index, argument)
        if (value !== 'strict' && value !== 'best-effort') {
          throw new Error('--failure-mode must be strict or best-effort')
        }
        config.failureMode = value
        index += 1
        break
      }
      case '--ffmpeg-path':
        config.ffmpegPath = requiredValue(args, index, argument)
        index += 1
        break
      case '--no-narration':
        config.narrationEnabled = false
        break
      default:
        throw new Error(`Unknown render option: ${argument}`)
    }
  }
  if (manifestPath === undefined) throw new Error('render requires --manifest')
  if (outputPath === undefined) throw new Error('render requires --output')
  const parsed: ParsedRenderArguments = { manifestPath, outputPath, config }
  if (testId !== undefined) parsed.testId = testId
  if (retry !== undefined) parsed.retry = retry
  return parsed
}

async function runPlaywright(args: readonly string[]): Promise<number> {
  const forwarded = [...args]
  let manifestPath: string | undefined
  for (let index = 0; index < forwarded.length; index += 1) {
    if (forwarded[index] === '--manifest') {
      manifestPath = requiredValue(forwarded, index, '--manifest')
      forwarded.splice(index, 2)
      break
    }
  }
  const separatorIndex = forwarded.indexOf('--')
  if (separatorIndex !== -1) forwarded.splice(separatorIndex, 1)

  const require = createRequire(import.meta.url)
  const playwrightCli = require.resolve('@playwright/test/cli')
  const reporterPath = fileURLToPath(new URL('./reporter.js', import.meta.url))
  const playwrightArgs = [playwrightCli, 'test', ...forwarded, `--reporter=line,${reporterPath}`]
  return new Promise((resolvePromise, reject) => {
    const child = spawn(process.execPath, playwrightArgs, {
      cwd: process.cwd(),
      env: {
        ...process.env,
        ...(manifestPath === undefined ? {} : { SUITECUT_MANIFEST_PATH: manifestPath }),
      },
      shell: false,
      stdio: 'inherit',
    })
    child.once('error', reject)
    child.once('close', (exitCode) => resolvePromise(exitCode ?? 1))
  })
}

export async function runSuiteCutCli(args: readonly string[]): Promise<number> {
  try {
    const [command, ...rest] = args
    if (command === undefined || command === '--help' || command === '-h' || command === 'help') {
      process.stdout.write(USAGE)
      return 0
    }
    if (command === 'test') return runPlaywright(rest)
    if (command === 'render') {
      const parsed = parseRenderArguments(rest)
      const selection: { testId?: string; retry?: number } = {}
      if (parsed.testId !== undefined) selection.testId = parsed.testId
      if (parsed.retry !== undefined) selection.retry = parsed.retry
      const report = await renderSuiteCut({
        manifestPath: parsed.manifestPath,
        outputPath: parsed.outputPath,
        selection,
        config: parsed.config,
      })
      process.stdout.write(`Rendered ${report.outputPath ?? parsed.outputPath}\n`)
      return 0
    }
    throw new Error(`Unknown command: ${command}`)
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`)
    return 1
  }
}

const entryPath = process.argv[1]
const isEntryPoint =
  entryPath !== undefined && import.meta.url === pathToFileURL(realpathSync(entryPath)).href
if (isEntryPoint) {
  process.exitCode = await runSuiteCutCli(process.argv.slice(2))
}
