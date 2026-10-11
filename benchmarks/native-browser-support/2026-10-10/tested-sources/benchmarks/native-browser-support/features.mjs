import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { setTimeout as delay } from 'node:timers/promises'
import sharp from 'sharp'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { WebSocketServer } from 'ws'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.MEMORY_DIRECTORY ?? '.suitecut/browser-support')
await mkdir(directory, { recursive: true })
const server = createServer((request, response) => {
  if (request.url === '/sw.js') {
    response.setHeader('Content-Type', 'application/javascript')
    response.end(`self.addEventListener('install',e=>e.waitUntil(self.skipWaiting()));
      self.addEventListener('activate',e=>e.waitUntil(self.clients.claim()));
      self.addEventListener('fetch',e=>{if(new URL(e.request.url).pathname==='/offline')e.respondWith(Promise.resolve(new Response('service-worker-response')))})`)
  } else {
    response.setHeader('Content-Type', 'text/html')
    response.end(
      '<!doctype html><title>SuiteCut browser feature checks</title><h1>Browser compatibility</h1>',
    )
  }
})
const sockets = new WebSocketServer({ server })
sockets.on('connection', (socket) =>
  socket.on('message', (data) => socket.send(data, { binary: false })),
)
await new Promise((ready) => server.listen(0, '127.0.0.1', ready))
const url = `http://127.0.0.1:${server.address().port}`
const result = { tests: [], status: 'starting', startedAt: new Date().toISOString() }
let engine
try {
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: 640,
    height: 360,
    framesPerSecond: 30,
    graphics: process.env.SUITECUT_GRAPHICS ?? 'auto',
  })
  result.cefVersion = engine.source.cefVersion
  result.graphics = process.env.SUITECUT_GRAPHICS ?? 'auto'
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(url)
  async function check(name, operation, required = true, argument) {
    try {
      const detail = await page.evaluate(operation, argument)
      const status = detail === false ? 'unavailable' : 'passed'
      result.tests.push({ name, required, status, detail })
    } catch (error) {
      result.tests.push({ name, required, status: 'failed', error: error.message })
    }
    console.log(JSON.stringify(result.tests.at(-1)))
  }
  await check('DOM, shadow DOM and CSS layout', () => {
    const host = document.createElement('div')
    document.body.append(host)
    const shadow = host.attachShadow({ mode: 'open' })
    shadow.innerHTML =
      '<div style="display:grid;width:123px;grid-template-columns:1fr 1fr"><span>A</span><span>B</span></div>'
    const element = shadow.querySelector('div')
    if (
      element.getBoundingClientRect().width !== 123 ||
      getComputedStyle(element).display !== 'grid'
    )
      throw Error('Layout failed')
    return true
  })
  await check('Canvas 2D pixels', () => {
    const canvas = document.createElement('canvas'),
      context = canvas.getContext('2d')
    context.fillStyle = 'rgb(12,34,56)'
    context.fillRect(0, 0, 1, 1)
    if (String(context.getImageData(0, 0, 1, 1).data) !== '12,34,56,255')
      throw Error('Canvas pixels differ')
    return true
  })
  await check('WebAssembly execution', async () => {
    const bytes = new Uint8Array([
      0, 97, 115, 109, 1, 0, 0, 0, 1, 5, 1, 96, 0, 1, 127, 3, 2, 1, 0, 7, 10, 1, 6, 97, 110, 115,
      119, 101, 114, 0, 0, 10, 6, 1, 4, 0, 65, 42, 11,
    ])
    const { instance } = await WebAssembly.instantiate(bytes)
    if (instance.exports.answer() !== 42) throw Error('WASM result differs')
    return true
  })
  await check(
    'Dedicated worker messaging',
    () =>
      new Promise((resolve, reject) => {
        const source = URL.createObjectURL(
          new Blob(['onmessage=e=>postMessage(e.data+1)'], { type: 'application/javascript' }),
        )
        const worker = new Worker(source),
          timer = setTimeout(() => {
            worker.terminate()
            reject(Error('Worker timed out'))
          }, 5000)
        worker.onerror = reject
        worker.onmessage = (event) => {
          clearTimeout(timer)
          worker.terminate()
          URL.revokeObjectURL(source)
          resolve(event.data === 42)
        }
        worker.postMessage(41)
      }),
  )
  await check(
    'Shared worker messaging',
    () =>
      new Promise((resolve, reject) => {
        const source = URL.createObjectURL(
          new Blob(['onconnect=e=>{const p=e.ports[0];p.onmessage=e=>p.postMessage(e.data+1)}'], {
            type: 'application/javascript',
          }),
        )
        const worker = new SharedWorker(source),
          timer = setTimeout(() => {
            worker.port.close()
            reject(Error('Shared worker timed out'))
          }, 5000)
        worker.onerror = reject
        worker.port.onmessage = (event) => {
          clearTimeout(timer)
          worker.port.close()
          URL.revokeObjectURL(source)
          resolve(event.data === 42)
        }
        worker.port.start()
        worker.port.postMessage(41)
      }),
  )
  await check(
    'IndexedDB write/read',
    () =>
      new Promise((resolve, reject) => {
        const request = indexedDB.open('suitecut-compatibility', 1)
        request.onupgradeneeded = () => request.result.createObjectStore('values')
        request.onerror = () => reject(request.error)
        request.onsuccess = () => {
          const database = request.result,
            transaction = database.transaction('values', 'readwrite')
          transaction.objectStore('values').put(42, 'answer')
          transaction.onerror = () => reject(transaction.error)
          transaction.oncomplete = () => {
            const read = database.transaction('values').objectStore('values').get('answer')
            read.onerror = () => reject(read.error)
            read.onsuccess = () => {
              database.close()
              resolve(read.result === 42)
            }
          }
        }
      }),
  )
  await check('Service worker registration and fetch interception', async () => {
    const registration = await navigator.serviceWorker.register('/sw.js')
    await navigator.serviceWorker.ready
    if (!navigator.serviceWorker.controller)
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(Error('Service worker control timed out')), 5000)
        navigator.serviceWorker.addEventListener(
          'controllerchange',
          () => {
            clearTimeout(timer)
            resolve()
          },
          { once: true },
        )
      })
    const value = await (await fetch('/offline')).text()
    await registration.unregister()
    if (value !== 'service-worker-response') throw Error('Service worker did not intercept fetch')
    return true
  })
  await check('CacheStorage write/read', async () => {
    const cache = await caches.open('suitecut-compatibility')
    await cache.put('/cache-test', new Response('cache-response'))
    const value = await (await cache.match('/cache-test')).text()
    await caches.delete('suitecut-compatibility')
    return value === 'cache-response'
  })
  await check(
    'WebSocket round trip',
    () =>
      new Promise((resolve, reject) => {
        const socket = new WebSocket(location.origin.replace('http:', 'ws:'))
        const timer = setTimeout(() => {
          socket.close()
          reject(Error('WebSocket timed out'))
        }, 5000)
        socket.onerror = () => reject(Error('WebSocket failed'))
        socket.onopen = () => socket.send('suitecut-echo')
        socket.onmessage = (event) => {
          clearTimeout(timer)
          socket.close()
          resolve(event.data === 'suitecut-echo')
        }
      }),
  )
  await check('WebRTC data-channel round trip', async () => {
    const a = new RTCPeerConnection(),
      b = new RTCPeerConnection()
    try {
      a.onicecandidate = (event) => {
        if (event.candidate) b.addIceCandidate(event.candidate).catch(() => {})
      }
      b.onicecandidate = (event) => {
        if (event.candidate) a.addIceCandidate(event.candidate).catch(() => {})
      }
      const sent = a.createDataChannel('suitecut')
      const received = new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(Error('WebRTC timed out')), 8000)
        b.ondatachannel = (event) => {
          event.channel.onmessage = (message) => {
            clearTimeout(timer)
            resolve(message.data === 'suitecut-rtc')
          }
        }
        sent.onopen = () => sent.send('suitecut-rtc')
      })
      await a.setLocalDescription(await a.createOffer())
      await b.setRemoteDescription(a.localDescription)
      await b.setLocalDescription(await b.createAnswer())
      await a.setRemoteDescription(b.localDescription)
      return await received
    } finally {
      a.close()
      b.close()
    }
  })
  await page.reload()
  await check(
    'IndexedDB survives document reload',
    () =>
      new Promise((resolve, reject) => {
        const request = indexedDB.open('suitecut-compatibility', 1)
        request.onerror = () => reject(request.error)
        request.onsuccess = () => {
          const database = request.result
          const read = database.transaction('values').objectStore('values').get('answer')
          read.onerror = () => reject(read.error)
          read.onsuccess = () => {
            database.close()
            resolve(read.result === 42)
          }
        }
      }),
  )
  await check('WebRTC canvas video loopback', async () => {
    const a = new RTCPeerConnection(),
      b = new RTCPeerConnection()
    const canvas = document.createElement('canvas')
    canvas.width = canvas.height = 64
    const painter = canvas.getContext('2d')
    const video = document.createElement('video')
    video.muted = true
    document.body.append(video)
    const stream = canvas.captureStream(30)
    const timer = setInterval(() => {
      painter.fillStyle = 'rgb(20,180,40)'
      painter.fillRect(0, 0, 64, 64)
    }, 30)
    try {
      a.onicecandidate = (event) => {
        if (event.candidate) b.addIceCandidate(event.candidate).catch(() => {})
      }
      b.onicecandidate = (event) => {
        if (event.candidate) a.addIceCandidate(event.candidate).catch(() => {})
      }
      stream.getTracks().forEach((track) => a.addTrack(track, stream))
      b.ontrack = (event) => {
        video.srcObject = event.streams[0]
        void video.play()
      }
      await a.setLocalDescription(await a.createOffer())
      await b.setRemoteDescription(a.localDescription)
      await b.setLocalDescription(await b.createAnswer())
      await a.setRemoteDescription(b.localDescription)
      const start = performance.now()
      while (performance.now() - start < 8000) {
        if (video.readyState >= 2 && video.videoWidth === 64) {
          const sample = document.createElement('canvas')
          sample.width = sample.height = 64
          const context = sample.getContext('2d')
          context.drawImage(video, 0, 0)
          const pixel = [...context.getImageData(32, 32, 1, 1).data]
          if (pixel[1] > 160 && pixel[0] < 50 && pixel[2] < 70)
            return { pixel, width: video.videoWidth, height: video.videoHeight }
        }
        await new Promise((resolve) => setTimeout(resolve, 50))
      }
      throw Error('RTC video did not decode the expected canvas pixels')
    } finally {
      clearInterval(timer)
      stream.getTracks().forEach((track) => track.stop())
      video.srcObject = null
      video.remove()
      a.close()
      b.close()
    }
  })
  for (const api of ['webgl', 'webgl2'])
    await check(
      `${api} shader rendering on this host`,
      (api) => {
        const canvas = document.createElement('canvas')
        canvas.width = canvas.height = 128
        canvas.style.cssText = `position:fixed;top:0;left:${api === 'webgl' ? 0 : 128}px;width:128px;height:128px;z-index:999`
        document.body.append(canvas)
        const context = canvas.getContext(api)
        if (!context) return false
        const modern = api === 'webgl2'
        const program = context.createProgram()
        function shader(type, source) {
          const shader = context.createShader(type)
          context.shaderSource(shader, source)
          context.compileShader(shader)
          if (!context.getShaderParameter(shader, context.COMPILE_STATUS))
            throw Error(context.getShaderInfoLog(shader))
          context.attachShader(program, shader)
          context.deleteShader(shader)
        }
        shader(
          context.VERTEX_SHADER,
          modern
            ? '#version 300 es\nin vec2 p;void main(){gl_Position=vec4(p,0,1);}'
            : 'attribute vec2 p;void main(){gl_Position=vec4(p,0,1);}',
        )
        const color = modern ? 'vec4(0,1,0,1)' : 'vec4(1,0,0,1)'
        shader(
          context.FRAGMENT_SHADER,
          modern
            ? '#version 300 es\nprecision mediump float;out vec4 c;void main(){c=' + color + ';}'
            : 'precision mediump float;void main(){gl_FragColor=' + color + ';}',
        )
        context.linkProgram(program)
        if (!context.getProgramParameter(program, context.LINK_STATUS))
          throw Error(context.getProgramInfoLog(program))
        context.useProgram(program)
        const buffer = context.createBuffer()
        context.bindBuffer(context.ARRAY_BUFFER, buffer)
        context.bufferData(
          context.ARRAY_BUFFER,
          new Float32Array([-1, -1, 3, -1, -1, 3]),
          context.STATIC_DRAW,
        )
        const position = context.getAttribLocation(program, 'p')
        context.enableVertexAttribArray(position)
        context.vertexAttribPointer(position, 2, context.FLOAT, false, 0, 0)
        function draw() {
          context.drawArrays(context.TRIANGLES, 0, 3)
          window[api + 'Frames'] = (window[api + 'Frames'] ?? 0) + 1
          requestAnimationFrame(draw)
        }
        draw()
        const pixel = new Uint8Array(4)
        context.readPixels(64, 64, 1, 1, context.RGBA, context.UNSIGNED_BYTE, pixel)
        if (String(pixel) !== (modern ? '0,255,0,255' : '255,0,0,255'))
          throw Error('Shader pixels differ: ' + pixel)
        const debug = context.getExtension('WEBGL_debug_renderer_info')
        return {
          version: context.getParameter(context.VERSION),
          renderer: context.getParameter(context.RENDERER),
          unmaskedRenderer: debug ? context.getParameter(debug.UNMASKED_RENDERER_WEBGL) : null,
          pixel: [...pixel],
        }
      },
      process.env.SUITECUT_ALLOW_MISSING_GL !== '1',
      api,
    )
  if (
    result.tests
      .filter((test) => test.name.includes('shader rendering'))
      .every((test) => test.status === 'passed')
  ) {
    const output = await engine.record(page, {
      destination: resolve(directory, 'webgl-native.mkv'),
      videoCodec: 'ffv1',
      container: 'mkv',
      audio: false,
    })
    await delay(3000)
    const metrics = await output.stop()
    await writeFile(
      resolve(directory, 'webgl-recording.json'),
      JSON.stringify(metrics, null, 2) + '\n',
    )
    assert.equal(metrics.error, '')
    const screenshot = await page.screenshot({ path: resolve(directory, 'webgl-shaders.png') })
    const image = await sharp(screenshot).removeAlpha().raw().toBuffer({ resolveWithObject: true })
    const decoder = spawn('ffmpeg', [
      '-v',
      'error',
      '-i',
      resolve(directory, 'webgl-native.mkv'),
      '-ss',
      '1',
      '-frames:v',
      '1',
      '-f',
      'rawvideo',
      '-pix_fmt',
      'rgb24',
      'pipe:1',
    ])
    const chunks = [],
      errors = []
    decoder.stdout.on('data', (chunk) => chunks.push(chunk))
    decoder.stderr.on('data', (chunk) => errors.push(chunk))
    const [code] = await once(decoder, 'close')
    assert.equal(code, 0, Buffer.concat(errors).toString())
    const decoded = Buffer.concat(chunks)
    assert.equal(decoded.length, 640 * 360 * 3)
    const pixels = []
    for (const [kind, data, width] of [
      ['screenshot', image.data, image.info.width],
      ['native recording', decoded, 640],
    ]) {
      for (const [x, channel] of [
        [64, 0],
        [192, 1],
      ]) {
        const offset = (64 * width + x) * 3
        const pixel = [...data.subarray(offset, offset + 3)]
        assert(
          pixel[channel] > 240 &&
            pixel.filter((_, i) => i !== channel).every((value) => value < 15),
          `${kind}: ${pixel}`,
        )
        pixels.push({ kind, x, pixel })
      }
    }
    const frames = await page.evaluate(() => [window.webglFrames, window.webgl2Frames])
    assert(frames.every((count) => count > 20))
    result.tests.push({
      name: 'WebGL compositor screenshot and native FFV1 recording',
      required: true,
      status: 'passed',
      detail: { pixels, frames, metrics },
    })
  }
  await check(
    'WebGPU adapter on this host',
    async () => {
      if (!navigator.gpu) return false
      return Boolean(await navigator.gpu.requestAdapter())
    },
    false,
  )
  try {
    const pdf = await page.pdf({ path: resolve(directory, 'browser-document.pdf'), format: 'A4' })
    assert.equal(pdf.subarray(0, 5).toString(), '%PDF-')
    result.tests.push({
      name: 'Playwright HTML-to-PDF export',
      required: false,
      status: 'passed',
      bytes: pdf.length,
    })
  } catch (error) {
    result.tests.push({
      name: 'Playwright HTML-to-PDF export',
      required: false,
      status: 'failed',
      error: error.message,
    })
  }
  await page.context().grantPermissions(['clipboard-read', 'clipboard-write'])
  await check(
    'Clipboard write/read in owned display',
    async () => {
      await navigator.clipboard.writeText('suitecut-clipboard-fixture')
      const value = await navigator.clipboard.readText()
      await navigator.clipboard.writeText('')
      return value === 'suitecut-clipboard-fixture'
    },
    false,
  )
  await page.screenshot({ path: resolve(directory, 'browser-features.png') })
  assert(
    result.tests.filter((test) => test.required).every((test) => test.status === 'passed'),
    'Required browser functionality failed; inspect features.json',
  )
  result.status = 'passed'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  await engine?.close()
  for (const socket of sockets.clients) socket.terminate()
  await new Promise((ready) => sockets.close(ready))
  server.closeAllConnections()
  await new Promise((ready) => server.close(ready))
  result.finishedAt = new Date().toISOString()
  await writeFile(resolve(directory, 'features.json'), JSON.stringify(result, null, 2) + '\n')
}
