# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> real Playwright locators, auto-wait, input, scrolling, iframe and screenshot
- Location: benchmarks/native-engine/engine.e2e.spec.ts:52:1

# Error details

```
Error: Native browser exited unexpectedly (127)
```

# Test source

```ts
  176 |         // Observers cannot prevent terminal process cleanup.
  177 |       }
  178 |     }
  179 |     failures.clear()
  180 |     for (const request of pending.values()) {
  181 |       clearTimeout(request.timer)
  182 |       request.reject(error)
  183 |     }
  184 |     pending.clear()
  185 |     if (!exited) child.kill('SIGKILL')
  186 |   }
  187 |   const decoder = new NativePacketDecoder((header, payload) => {
  188 |     if (terminal) return
  189 |     if (header.type === 'ready') {
  190 |       if (clockOffset !== undefined) throw new Error('Duplicate native handshake')
  191 |       clockOffset = performance.now() - header.timestampMs
  192 |       cefVersion = header.cefVersion
  193 |       frameSubscriptions = header.frameSubscriptions === true
  194 |       audioSubscriptions = header.audioSubscriptions === true
  195 |       nativeMedia = header.nativeMedia === true
  196 |       resolveReady()
  197 |       return
  198 |     }
  199 |     if (clockOffset === undefined) throw new Error('Native packet before handshake')
  200 |     if (header.type === 'event') {
  201 |       const message = JSON.parse(payload.toString('utf8')) as UntrustedInput
  202 |       for (const listener of protocolEvents) listener(message)
  203 |       return
  204 |     }
  205 |     if (header.type === 'error') {
  206 |       fail(new Error(`Native browser: ${header.error}`))
  207 |       return
  208 |     }
  209 |     if (header.type === 'response') {
  210 |       const request = pending.get(header.id)
  211 |       if (!request) return // A timed-out command may complete later.
  212 |       const result =
  213 |         header.ok && payload.length
  214 |           ? (JSON.parse(payload.toString('utf8')) as UntrustedInput)
  215 |           : undefined
  216 |       pending.delete(header.id)
  217 |       clearTimeout(request.timer)
  218 |       if (header.ok) request.resolve(result)
  219 |       else request.reject(new Error(header.error ?? 'Native command failed'))
  220 |       return
  221 |     }
  222 |     if (stopping) return
  223 |     if (header.type === 'frame') {
  224 |       if (
  225 |         header.pixelFormat !== config.pixelFormat ||
  226 |         header.width !== Math.round(config.width * config.deviceScaleFactor) ||
  227 |         header.height !== Math.round(config.height * config.deviceScaleFactor)
  228 |       ) {
  229 |         fail(new Error('Native frame dimensions changed unexpectedly'))
  230 |         return
  231 |       }
  232 |       if (frames.size === 0 && frameSubscriptions) return
  233 |       // The ready packet's one-way transit time can overestimate the clock
  234 |       // offset. A captured video frame cannot arrive from the future. Refine
  235 |       // that upper bound without moving the clock forward on delayed packets.
  236 |       clockOffset = Math.min(clockOffset ?? 0, performance.now() - header.timestampMs)
  237 |       lastFrame = {
  238 |         data: payload,
  239 |         pixelFormat: header.pixelFormat,
  240 |         width: header.width,
  241 |         height: header.height,
  242 |         timestampMs: header.timestampMs + clockOffset,
  243 |         droppedFrames: header.dropped,
  244 |       }
  245 |       for (const listener of frames) listener(lastFrame)
  246 |     } else {
  247 |       const packet = {
  248 |         data: payload,
  249 |         frames: header.frames,
  250 |         timestampMs: header.timestampMs + clockOffset,
  251 |       }
  252 |       for (const listener of audio) listener(packet)
  253 |     }
  254 |   })
  255 |   child.stdout.on('data', (chunk: Buffer) => {
  256 |     if (terminal) return
  257 |     try {
  258 |       decoder.push(chunk)
  259 |     } catch {
  260 |       fail(new Error('Native browser protocol or subscriber failure'))
  261 |     }
  262 |   })
  263 |   // Drain Chromium diagnostics without exposing page URLs or credentials to logs.
  264 |   child.stderr.resume()
  265 |   child.stdin.on('error', () => {
  266 |     if (!stopping) fail(new Error('Native browser command pipe failed'))
  267 |   })
  268 |   child.once('error', () => fail(new Error('Could not launch the native browser executable')))
  269 |   child.once('close', (code) => {
  270 |     exited = true
  271 |     try {
  272 |       decoder.end()
  273 |     } catch {
  274 |       if (!stopping) fail(new Error('Native browser output was truncated'))
  275 |     }
> 276 |     if (!stopping) fail(new Error(`Native browser exited unexpectedly (${String(code)})`))
      |                         ^ Error: Native browser exited unexpectedly (127)
  277 |     frames.clear()
  278 |     audio.clear()
  279 |     protocolEvents.clear()
  280 |     lastFrame = undefined
  281 |     config.signal?.removeEventListener('abort', abort)
  282 |     const finish = () => {
  283 |       fullyClosed = true
  284 |       resolveClosed()
  285 |       for (const listener of closes) {
  286 |         try {
  287 |           listener()
  288 |         } catch {
  289 |           // Closing all subscriptions takes precedence over observer errors.
  290 |         }
  291 |       }
  292 |       closes.clear()
  293 |       failures.clear()
  294 |     }
  295 |     void rm(profile, { recursive: true, force: true }).then(finish, () => {
  296 |       fail(new Error('Could not remove the native browser temporary profile'))
  297 |       finish()
  298 |     })
  299 |   })
  300 |   const request = (
  301 |     command: string,
  302 |     fields: Record<string, UntrustedInput> = {},
  303 |   ): Promise<UntrustedInput> => {
  304 |     if (terminal || stopping || exited)
  305 |       return Promise.reject(terminal ?? new Error('Native browser is closed'))
  306 |     if (pending.size >= 64) return Promise.reject(new Error('Too many pending native commands'))
  307 |     const id = ++nextId
  308 |     const message =
  309 |       JSON.stringify({ ...fields, version: NATIVE_PROTOCOL_VERSION, id, command }) + '\n'
  310 |     if (Buffer.byteLength(message) > 4_194_304)
  311 |       return Promise.reject(new Error('Native command exceeds 4 MiB'))
  312 |     return new Promise((resolve, reject) => {
  313 |       const timer = setTimeout(
  314 |         () => {
  315 |           pending.delete(id)
  316 |           reject(new Error('Native browser command timed out'))
  317 |         },
  318 |         command === 'media.render' || command === 'media.cut' ? 3_600_000 : 30_000,
  319 |       )
  320 |       pending.set(id, { resolve, reject, timer })
  321 |       child.stdin.write(message)
  322 |     })
  323 |   }
  324 |   const close = (): Promise<void> => {
  325 |     if (closing) return closing
  326 |     stopping = true
  327 |     closing = (async () => {
  328 |       for (const item of pending.values()) {
  329 |         clearTimeout(item.timer)
  330 |         item.reject(new Error('Native browser is closing'))
  331 |       }
  332 |       pending.clear()
  333 |       if (!exited) child.stdin.end()
  334 |       const timer = setTimeout(() => child.kill('SIGKILL'), 15000)
  335 |       try {
  336 |         await closed
  337 |       } finally {
  338 |         clearTimeout(timer)
  339 |       }
  340 |     })()
  341 |     return closing
  342 |   }
  343 |   const abort = (): void => {
  344 |     fail(new Error('Native browser aborted'))
  345 |     void close()
  346 |   }
  347 |   config.signal?.addEventListener('abort', abort, { once: true })
  348 |   if (config.signal?.aborted) abort()
  349 |   try {
  350 |     await raceWithAbort(
  351 |       Promise.race([ready, failure]),
  352 |       AbortSignal.timeout(config.startupTimeoutMs),
  353 |     )
  354 |     if (frameSubscriptions) await request('frames', { enabled: false })
  355 |     if (audioSubscriptions) await request('audio', { enabled: false })
  356 |   } catch (error) {
  357 |     await close()
  358 |     throw error
  359 |   }
  360 |   return {
  361 |     get state() {
  362 |       return terminal ? 'failed' : exited ? 'closed' : stopping ? 'closing' : 'running'
  363 |     },
  364 |     pixelFormat: config.pixelFormat,
  365 |     width: Math.round(config.width * config.deviceScaleFactor),
  366 |     height: Math.round(config.height * config.deviceScaleFactor),
  367 |     framesPerSecond: config.framesPerSecond,
  368 |     cefVersion,
  369 |     nativeMedia,
  370 |     command: request,
  371 |     onProtocolEvent: (listener) => {
  372 |       protocolEvents.add(listener)
  373 |       return () => {
  374 |         protocolEvents.delete(listener)
  375 |       }
  376 |     },
```