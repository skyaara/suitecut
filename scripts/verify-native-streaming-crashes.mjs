// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, Socket } from 'node:net'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as deadlineTimer } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePublisher,
  launchNativeBrowser,
  NativeBrowserContexts,
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
/**
 * Bounds observation of actual native failure/physical settlement.
 * @template T
 * @param {Promise<T>} operation
 * @param {string} label
 * @returns {Promise<T>}
 */
async function bounded(operation, label) {
  const cancel = new AbortController()
  try {
    return await Promise.race([
      operation,
      deadlineTimer(10000, undefined, { signal: cancel.signal }).then(() => {
        throw new Error(`Timeout: actual ${label} was not observed`)
      }),
    ])
  } finally {
    cancel.abort()
  }
}

const directory = resolve(
  process.env.SUITECUT_NATIVE_STREAMING_DIRECTORY ?? '.suitecut/native-streaming-source-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Hashes the actual proof input.
 * @param {string} file
 */
const hash = async (file) =>
  createHash('sha256')
    .update(await readFile(file))
    .digest('hex')
/**
 * Fails if an authored native publisher subscribes to host media.
 * @param {import('../dist/native-browser.js').NativeBrowser} source
 */
function noHostMedia(source) {
  source.onFrame = () => {
    throw new Error('Native publisher installed a host frame subscription')
  }
  source.onAudio = () => {
    throw new Error('Native publisher installed a host audio subscription')
  }
}
/**
 * Receives real native publisher TCP bytes.
 * @param {string} name
 */
async function receiver(name) {
  const chunks = z.array(z.instanceof(Buffer)).parse([])
  let bytes = 0
  const active = z.object({ socket: z.instanceof(Socket).optional() }).parse({})
  let eof = Promise.resolve()
  let connected = false
  const server = createServer((socket) => {
    active.socket = socket
    connected = true
    eof = once(socket, 'end').then(() => undefined)
    void eof.catch(() => undefined)
    socket.on('data', (value) => {
      const chunk = z.instanceof(Buffer).parse(value)
      bytes += chunk.length
      if (bytes > 64 * 1024 * 1024) {
        socket.destroy(new Error('Actual publisher bytes exceeded verifier bound'))
        return
      }
      chunks.push(chunk)
    })
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  assert(address && typeof address === 'object')
  return {
    url: `tcp://127.0.0.1:${address.port}`,
    async finish() {
      assert(connected, 'Actual publisher never connected')
      await eof
      const path = join(directory, name)
      await writeFile(path, Buffer.concat(chunks))
      return path
    },
    async close() {
      active.socket?.destroy()
      await new Promise((resolve) => server.close(resolve))
    },
  }
}
/**
 * Creates an actual animated CEF page and real page audio.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {boolean} [variant]
 * @param {boolean} [noise]
 */
async function scene(page, variant = false, noise = false) {
  await page.setContent(`<style>body{margin:0;overflow:hidden}button{position:absolute;left:0;top:0}</style><canvas width=160 height=96></canvas><button>audio</button><script>
    globalThis.frames=0;const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){${noise ? 'const data=ctx.createImageData(160,96);for(let i=0;i<data.data.length;i+=4){data.data[i]=Math.random()*256;data.data[i+1]=Math.random()*256;data.data[i+2]=Math.random()*256;data.data[i+3]=255}ctx.putImageData(data,0,0);' : `ctx.fillStyle=${variant ? "'#22ee44'" : "(Math.floor(frames/15)%2)?'#2244ee':'#ee2244'"};ctx.fillRect(0,0,160,96);`}frames++;requestAnimationFrame(draw)}draw();
    document.querySelector('button').onclick=async()=>{const audio=new AudioContext();await audio.resume();const osc=audio.createOscillator();const gain=audio.createGain();gain.gain.value=.15;osc.frequency.value=660;osc.connect(gain).connect(audio.destination);osc.start();globalThis.nativeAudio=audio;globalThis.nativeTone=osc;};
  </script>`)
  await page.click('button')
  await page.waitForFunction('globalThis.frames>=12')
}
/**
 * Waits for actual renderer frames.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {number} count
 */
async function advance(page, count) {
  const before = z.number().parse(await page.evaluate('globalThis.frames'))
  await page.waitForFunction(`globalThis.frames>=${before + count}`)
}
/**
 * Admits the actual context transport.
 * @param {import('../dist/native-browser.js').NativeBrowser} source
 */
function contextsFor(source) {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Actual native context transport unavailable')
  return new NativeBrowserContexts(
    Object.assign(source, {
      runContext: source.runContext.bind(source),
      pageSource: source.pageSource.bind(source),
      onContextEvent: source.onContextEvent.bind(source),
    }),
  )
}
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat: 'i420',
  headless: true,
})
noHostMedia(source)
const contexts = contextsFor(source)
const results = []
try {
  for (const target of ['selected', 'owner']) {
    const ownerContext = await contexts.newContext(),
      selectedContext = await contexts.newContext()
    const owner = await ownerContext.newPage(),
      selected = await selectedContext.newPage()
    noHostMedia(owner.source)
    noHostMedia(selected.source)
    await scene(owner)
    await scene(selected, true)
    const tcp = await receiver(`${target}-crash.webm`)
    try {
      const publisher = await createNativePublisher({
        source: owner.source,
        ffmpegPath: '/usr/bin/ffmpeg',
        publisher: {
          destination: tcp.url,
          container: 'webm',
          videoCodec: 'vp9',
          audioCodec: 'opus',
          framesPerSecond: 29.97,
          audio: true,
        },
      })
      await publisher.selectSource(selected.source)
      await advance(selected, 40)
      const before = await publisher.status()
      assert(before.physicalEncoderPid > 0)
      const doomed = target === 'selected' ? selected.source : owner.source
      const crash = doomed
        .sendDevToolsCommand('Page.crash')
        .catch((error) => (error instanceof Error ? error.message : 'Non-Error crash response'))
      const sourceFailure = await bounded(
        doomed.failure.catch((error) =>
          error instanceof Error ? error.message : 'Non-Error source failure',
        ),
        'renderer failure',
      )
      assert.match(sourceFailure, /renderer|terminated|crash/iu)
      const failure = await bounded(
        publisher.failure.catch((error) =>
          error instanceof Error ? error.message : 'Non-Error publisher failure',
        ),
        'publisher failure',
      )
      assert.match(failure, /failed|renderer|closed|terminated/iu)
      const deadline = performance.now() + 5000
      let diagnostics
      do {
        diagnostics = z
          .object({ nativeStreamingPhysical: z.number().int().nonnegative() })
          .parse(await source.runAutomation('diagnostics'))
      } while (diagnostics.nativeStreamingPhysical && performance.now() < deadline)
      assert.equal(diagnostics.nativeStreamingPhysical, 0)
      assert.throws(() => process.kill(before.physicalEncoderPid, 0), { code: 'ESRCH' })
      await bounded(tcp.finish(), 'publisher socket EOF')
      const terminal = await publisher.status()
      if (terminal) {
        assert.equal(terminal.phase, 'failed')
        assert.equal(terminal.physicalEncoderPid, 0)
      }
      await selectedContext.close()
      await ownerContext.close()
      assert.equal(source.state, 'running')
      const healthy = z
        .object({ result: z.object({ value: z.number() }) })
        .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
      assert.equal(healthy.result.value, 42)
      results.push({
        target,
        before,
        sourceFailure,
        failure,
        terminal,
        diagnostics,
        crash: await bounded(crash, 'crashed command cancellation'),
      })
    } finally {
      await tcp.close()
      await selectedContext.close()
      await ownerContext.close()
    }
  }
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify({ completed: true, binarySHA256: await hash(executable), results }, null, 2) +
      '\n',
  )
  console.log('Actual native publisher renderer-crash ownership and waitpid proof passed')
} catch (error) {
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        results,
        error: error instanceof Error ? error.stack : 'Non-Error failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await source.close()
}
