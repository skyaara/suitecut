import { execFile } from 'node:child_process'
import { mkdir } from 'node:fs/promises'
import { dirname } from 'node:path'
import { parentPort } from 'node:worker_threads'

import { synthesizeKokoro } from './kokoro.js'
import {
  parseNarrationWorkerRequest,
  type SuiteCutNarrationWorkerFailure,
  type SuiteCutNarrationWorkerRequest,
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

async function synthesize(request: SuiteCutNarrationWorkerRequest): Promise<void> {
  try {
    await mkdir(dirname(request.outputPath), { recursive: true })
    if (request.provider === 'kokoro') {
      await synthesizeKokoro(request.text, request.voice, request.speed, request.outputPath)
    } else {
      await runSay(request)
    }
    const response: SuiteCutNarrationWorkerSuccess = {
      type: 'synthesized',
      jobId: request.jobId,
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

let queue = Promise.resolve()
workerPort.on('message', (message: UntrustedInput) => {
  const request = parseNarrationWorkerRequest(message)

  queue = queue.then(
    () => synthesize(request),
    () => synthesize(request),
  )
})
