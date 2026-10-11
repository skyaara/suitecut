# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: engine.e2e.spec.ts >> native protocol rejects bypasses and requires audio readiness when audio is omitted
- Location: benchmarks/native-engine/engine.e2e.spec.ts:180:1

# Error details

```
Error: Native audio readiness timed out (waiting for source sound)
```

# Test source

```ts
  119 |         '--suitecut-frame-subscriptions',
  120 |       ],
  121 |       { shell: false, stdio: ['pipe', 'pipe', 'pipe'] },
  122 |     )
  123 |   let child: ReturnType<typeof launch>
  124 |   try {
  125 |     child = launch()
  126 |   } catch (error) {
  127 |     await rm(profile, { recursive: true, force: true })
  128 |     throw error
  129 |   }
  130 |   const frames = new Set<(frame: NativeVideoFrame) => void>()
  131 |   const audio = new Set<(packet: NativeAudioPacket) => void>()
  132 |   const failures = new Set<(error: Error) => void>()
  133 |   const closes = new Set<() => void>()
  134 |   const pending = new Map<
  135 |     number,
  136 |     {
  137 |       resolve: (value: UntrustedInput) => void
  138 |       reject: (error: Error) => void
  139 |       timer: NodeJS.Timeout
  140 |     }
  141 |   >()
  142 |   let nextId = 0
  143 |   let closing: Promise<void> | undefined
  144 |   let stopping = false
  145 |   let exited = false
  146 |   let fullyClosed = false
  147 |   let terminal: Error | undefined
  148 |   let lastFrame: NativeVideoFrame | undefined
  149 |   let clockOffset: number | undefined
  150 |   let cefVersion = ''
  151 |   let frameSubscriptions = false
  152 |   let audioSubscriptions = false
  153 |   let nativeMedia = false
  154 |   const protocolEvents = new Set<(message: UntrustedInput) => void>()
  155 |   let rejectFailure: (error: Error) => void = () => undefined
  156 |   let resolveReady: () => void = () => undefined
  157 |   let resolveClosed: () => void = () => undefined
  158 |   const failure = new Promise<never>((_, reject) => {
  159 |     rejectFailure = reject
  160 |   })
  161 |   void failure.catch(() => undefined)
  162 |   const ready = new Promise<void>((resolve) => {
  163 |     resolveReady = resolve
  164 |   })
  165 |   const closed = new Promise<void>((resolve) => {
  166 |     resolveClosed = resolve
  167 |   })
  168 |   const fail = (error: Error): void => {
  169 |     if (terminal) return
  170 |     terminal = error
  171 |     rejectFailure(error)
  172 |     for (const listener of failures) {
  173 |       try {
  174 |         listener(error)
  175 |       } catch {
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
> 219 |       else request.reject(new Error(header.error ?? 'Native command failed'))
      |                           ^ Error: Native audio readiness timed out (waiting for source sound)
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
  276 |     if (!stopping) fail(new Error(`Native browser exited unexpectedly (${String(code)})`))
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
```