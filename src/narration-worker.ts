import { execFile } from 'node:child_process'
import { mkdir } from 'node:fs/promises'
import { dirname } from 'node:path'
import { parentPort } from 'node:worker_threads'

import { loadAudioPlugin } from './audio-plugin-loader.js'
import { synthesizeKokoro } from './kokoro.js'
import {
  parseNarrationWorkerMessage,
  type SuiteCutNarrationWorkerFailure,
  type SuiteCutNarrationWorkerRequest,
  type SuiteCutNarrationWorkerResponse,
  type SuiteCutNarrationWorkerSuccess,
} from './narration-worker-protocol.js'
import { toError } from './untrusted.js'
import { type UntrustedInput } from './untrusted.js'

const SAY_EXECUTABLE = '/usr/bin/say'

function runSay(request: SuiteCutNarrationWorkerRequest): Promise<void> {
  const args: string[] = []
  if (request.voice !== 'default') {
    args.push('-v', request.voice)
  }
  if (request.speed !== 1) args.push('-r', String(Math.round(175 * request.speed)))
  args.push('-o', request.outputPath, request.text)

  return new Promise((resolve, reject) => {
    execFile(SAY_EXECUTABLE, args, { maxBuffer: 1024 * 1024 }, (error, _stdout, stderr) => {
      if (error === null) {
        resolve()
        return
      }

      const message = stderr.trim()
      reject(new Error(message.length > 0 ? message : error.message, { cause: error }))
    })
  })
}

function serializeError(error: UntrustedInput): SuiteCutNarrationWorkerFailure['error'] {
  const normalized = toError(error)
  return {
    name: normalized.name,
    message: normalized.message,
    ...(normalized.stack === undefined ? {} : { stack: normalized.stack }),
  }
}

const port = parentPort
if (port === null) {
  throw new Error('SuiteCut narration worker requires a parent port')
}
const workerPort = port
const plugins = new Map<string, Awaited<ReturnType<typeof loadAudioPlugin>>>()

async function audioPlugin(request: SuiteCutNarrationWorkerRequest) {
  const reference = request.plugin
  if (reference === undefined) {
    throw new Error(`SuiteCut audio plugin is not configured for provider ${request.provider}`)
  }
  let plugin = plugins.get(request.provider)
  if (plugin === undefined) {
    plugin = await loadAudioPlugin(reference)
    plugins.set(request.provider, plugin)
  }
  return plugin
}

async function synthesize(request: SuiteCutNarrationWorkerRequest): Promise<void> {
  try {
    await mkdir(dirname(request.outputPath), { recursive: true })
    let timing
    if (request.provider === 'kokoro') {
      timing = await synthesizeKokoro(
        request.text,
        request.voice,
        request.speed,
        request.outputPath,
      )
    } else if (request.provider === 'macos-say') {
      await runSay(request)
    } else {
      const plugin = await audioPlugin(request)
      await plugin.synthesize({
        text: request.text,
        voice: request.voice,
        speed: request.speed,
        outputPath: request.outputPath,
        ...(request.plugin?.options === undefined ? {} : { options: request.plugin.options }),
      })
    }
    const response: SuiteCutNarrationWorkerSuccess = {
      type: 'synthesized',
      jobId: request.jobId,
      ...(timing === undefined ? {} : { timing }),
    }
    workerPort.postMessage(response)
  } catch (error) {
    const response: SuiteCutNarrationWorkerFailure = {
      type: 'failed',
      jobId: request.jobId,
      error: serializeError(error as UntrustedInput),
    }
    workerPort.postMessage(response)
  }
}

async function closeWorker(): Promise<void> {
  try {
    const results = await Promise.allSettled(
      [...plugins.values()].map(async (plugin) => plugin.dispose?.()),
    )
    plugins.clear()
    const errors = results.flatMap((result) =>
      result.status === 'rejected' ? [toError(result.reason as UntrustedInput)] : [],
    )
    if (errors.length > 0) {
      throw new AggregateError(errors, 'Failed to dispose SuiteCut audio plugins')
    }
    workerPort.postMessage({ type: 'closed' } satisfies SuiteCutNarrationWorkerResponse)
  } catch (error) {
    workerPort.postMessage({
      type: 'close-failed',
      error: serializeError(error as UntrustedInput),
    } satisfies SuiteCutNarrationWorkerResponse)
  }
}

let queue = Promise.resolve()
let closing = false
workerPort.on('message', (message: UntrustedInput) => {
  const request = parseNarrationWorkerMessage(message)
  if (request.type === 'close') {
    if (closing) return
    closing = true
    queue = queue.then(closeWorker, closeWorker)
    return
  }
  if (closing) return

  queue = queue.then(
    () => synthesize(request),
    () => synthesize(request),
  )
})
