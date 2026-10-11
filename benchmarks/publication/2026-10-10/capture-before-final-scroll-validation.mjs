import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as tcpServer } from 'node:net'
import { resolve, join } from 'node:path'
import { monitorEventLoopDelay, performance } from 'node:perf_hooks'
import { setTimeout as delay } from 'node:timers/promises'

import { chromium } from 'playwright'

import { launchEngine } from '../../dist/engine.js'
import { ObsClient } from '../obs-cef/obs-client.mjs'
import { connectObs } from '../obs-cef/native-audio-profile/cdp.mjs'
import { audioFixture } from '../obs-cef/native-audio-profile/fixture.mjs'

const directory = resolve(process.env.BENCH_DIRECTORY)
const backend = process.env.BENCH_BACKEND
const mode = process.env.BENCH_MODE ?? 'record'
const duration = Number(process.env.BENCH_DURATION_MS ?? 30000)
const nativeOld = resolve('.suitecut/publication-old/native-build/suitecut-browser')
const legacy = await import('../../.suitecut/publication-old/dist/native.js')
const oldPage = await import('../../.suitecut/publication-old/dist/native-page.js')
const oldRecorder = await import('../../.suitecut/publication-old/dist/native-recorder.js')
const oldSessions = await import('../../.suitecut/publication-old/dist/suitecut.js')
const oldSink = await import('../../.suitecut/publication-old/dist/recording-output.js')
const oldTabAudio = await import('../../.suitecut/publication-old/dist/tab-audio.js')
const monotonic = () => Number(process.hrtime.bigint()) / 1e9
const normalized = resolve('benchmarks/publication/ffmpeg-normalized.py')
process.env.SUITECUT_FFMPEG_PATH = normalized
const result = { backend, mode, durationRequestedMs: duration, width: 1920, height: 1080, fps: 30,
  status: 'starting', startedAt: new Date().toISOString(), profile: process.env.BENCH_PROFILE ?? 'performance',
  encoding: { video: 'H264', preset: 'veryfast', crf: mode === 'record' ? 18 : null,
    bitrateKbps: mode === 'stream' ? 4500 : null, encoderThreads: 1, audio: mode === 'stream' ? 'AAC 48kHz stereo 128kbps' : backend.startsWith('obs-') ? 'silent AAC track imposed by OBS recording profile; silent page' : 'video only; silent page' },
  recordingPath: join(directory, 'recording.mkv'), receiverPath: join(directory, 'receiver.flv'), phases: [], events: [] }
await mkdir(directory, { recursive: true })
const save = () => writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
await save()
let html = await readFile('benchmarks/publication/fixture.html', 'utf8')
if (mode === 'stream') html = audioFixture(html)
await writeFile(join(directory, 'fixture.html'), html)
const server = createServer((_req, res) => { res.setHeader('Content-Type', 'text/html'); res.end(html) })
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const url = `http://127.0.0.1:${server.address().port}`
const launchOptions = { headless: false, chromiumSandbox: true, args: ['--kiosk', '--window-position=0,0', '--window-size=1920,1167', '--autoplay-policy=no-user-gesture-required'] }
let receiver, receiverClosed, obs, engine, source, browser, context, page, cdp, obsConnection, nativeRecord, nativeStream, session, sampler
let failure, unsub = () => {}, unsubDiagnostics = () => {}, recording = false, streaming = false, stopOutput
const statusFile = createWriteStream(join(directory, 'status.jsonl'))
const loop = monitorEventLoopDelay({ resolution: 20 })
process.once('SIGTERM', () => { failure = new Error('Runtime requested termination; check memory guard') })
loop.enable()
try {
  let rtmp
  if (mode === 'stream') {
    const probe = tcpServer(); probe.listen(0, '127.0.0.1'); await once(probe, 'listening')
    const port = probe.address().port; await new Promise(done => probe.close(done))
    rtmp = `rtmp://127.0.0.1:${port}/live/benchmark`
    receiver = spawn('/usr/bin/ffmpeg', ['-v','error','-y','-listen','1','-i',rtmp,'-c','copy',result.receiverPath], { stdio: ['ignore','ignore','pipe'] })
    receiver.stderr.pipe(createWriteStream(join(directory, 'receiver.log')))
    receiverClosed = once(receiver, 'close'); receiverClosed.catch(() => {})
  }
  const start = monotonic()
  if (backend.startsWith('obs-')) {
    for (let attempt = 0; attempt < 80; attempt++) {
      obs = new ObsClient()
      try { await obs.connect(); break } catch { obs.close(); obs = undefined; await delay(250) }
    }
    assert(obs, 'OBS control socket did not become ready')
    let frontendReady = false
    for(let index=0;index<80;index++){try{await obs.request('GetSceneList');frontendReady=true;break}catch(error){if(!String(error).includes('207'))throw error;await delay(250)}}
    assert(frontendReady,'OBS frontend did not finish startup')
    result.obsVersion = await obs.request('GetVersion');result.inputKinds = await obs.request('GetInputKindList')
    const currentVideo = await obs.request('GetVideoSettings');assert.equal(currentVideo.outputWidth,1920);assert.equal(currentVideo.outputHeight,1080);assert.equal(currentVideo.fpsNumerator/currentVideo.fpsDenominator,30)
    const scene = 'Publication benchmark'
    const scenes = await obs.request('GetSceneList')
    if (!scenes.scenes.some(s => s.sceneName === scene)) await obs.request('CreateScene', { sceneName:scene })
    for (const input of (await obs.request('GetInputList')).inputs) await obs.request('RemoveInput', { inputName:input.inputName })
    const sourceName = 'Publication capture'
    if (backend === 'obs-cef') {
      await obs.request('CreateInput', { sceneName:scene,inputName:sourceName,inputKind:'browser_source',sceneItemEnabled:true,
        inputSettings:{url,width:1920,height:1080,fps:30,fps_custom:true,reroute_audio:true,shutdown:false,css:'body {margin:0}'} })
      await obs.request('SetInputMute', { inputName:sourceName,inputMuted:mode !== 'stream' })
      await obs.request('SetInputVolume', { inputName:sourceName,inputVolumeMul:1 })
      await delay(1000)
      obsConnection = await connectObs(chromium, directory)
      page = obsConnection.browser.contexts().flatMap(c => c.pages()).find(p => p.url().startsWith(url))
      assert(page, 'OBS embedded CEF target missing')
      result.browserVersion = obsConnection.browser.version()
    } else {
      browser = await chromium.launch({ ...launchOptions, ignoreDefaultArgs: ['--mute-audio'] })
      context = await browser.newContext({ viewport:{width:1920,height:1080},deviceScaleFactor:1 })
      page = await context.newPage(); await page.goto(url); await page.bringToFront()
      await obs.request('CreateInput', { sceneName:scene,inputName:sourceName,inputKind:result.inputKinds.inputKinds.find(kind => kind.startsWith('xshm_input')),sceneItemEnabled:true,
        inputSettings:{screen:0,show_cursor:false,advanced:true,cut_top:87,cut_bot:0,cut_left:0,cut_right:0} })
      if (mode === 'stream') {
        await obs.request('CreateInput', { sceneName:scene,inputName:'Publication audio',inputKind:result.inputKinds.inputKinds.find(kind => kind.startsWith('pulse_output_capture')),sceneItemEnabled:true,inputSettings:{device_id:'publication.monitor'} })
        await obs.request('SetInputVolume',{inputName:'Publication audio',inputVolumeMul:1})
        await obs.request('SetInputMute',{inputName:'Publication audio',inputMuted:false})
      }
      result.browserVersion = browser.version()
    }
    await obs.request('SetCurrentProgramScene', { sceneName:scene })
    await obs.request('SetRecordDirectory', { recordDirectory:directory })
    for (const [parameterName,value] of Object.entries({ Encoder:'obs_x264',RecEncoder:'obs_x264',RecTracks:mode === 'record' ? 0 : 1,TrackIndex:1,AudioEncoder:'ffmpeg_aac',RecAudioEncoder:'ffmpeg_aac',Track1Bitrate:128,ApplyServiceSettings:false })) {
      await obs.request('SetProfileParameter', { parameterCategory:'AdvOut',parameterName,parameterValue:String(value) })
    }
    if (mode === 'stream') await obs.request('SetStreamServiceSettings', { streamServiceType:'rtmp_custom',streamServiceSettings:{server:rtmp.slice(0,rtmp.lastIndexOf('/')),key:'benchmark',use_auth:false} })
    result.effectiveVideo = await obs.request('GetVideoSettings')
    result.obsBefore = await obs.request('GetStats')
    await obs.request('SaveSourceScreenshot', { sourceName,imageFormat:'png',imageFilePath:join(directory,'capture-source.png') })
    result.screenshotSource = sourceName
  } else if (backend === 'cpp-cef') {
    engine = await launchEngine({ executablePath:resolve('.suitecut/native-build/suitecut-browser'),width:1920,height:1080,framesPerSecond:30 })
    page = engine.browser.contexts()[0].pages()[0]; await page.goto(url)
    result.browserVersion = engine.browser.version(); result.cefVersion = engine.source.cefVersion
  } else if (backend === 'ts-cef') {
    source = await legacy.launchNativeBrowser({ executablePath:nativeOld,width:1920,height:1080,framesPerSecond:30,pixelFormat:'i420' })
    source.failure.catch(error => { failure = error;result.events.push({name:'native browser failure',status:result.status,monotonic:monotonic(),error:String(error)}) })
    page = oldPage.createNativePage(source); await page.goto(url); result.cefVersion = source.cefVersion
  } else if (backend === 'ts-external') {
    if (mode === 'stream') {
      context = await oldTabAudio.launchTabAudioContext(launchOptions, {viewport:{width:1920,height:1080},deviceScaleFactor:1})
      browser = context.browser(); page = context.pages()[0] ?? await context.newPage()
    } else {
      browser = await chromium.launch(launchOptions)
      context = await browser.newContext({viewport:{width:1920,height:1080},deviceScaleFactor:1});page = await context.newPage()
    }
    await page.goto(url); result.browserVersion = browser.version()
  } else throw new Error(`Unsupported capture backend: ${backend}`)
  if (!source) cdp = await page.context().newCDPSession(page)
  result.browserReadySeconds = monotonic() - start
  if(source) {
    result.sourceFrames={count:0,firstTimestampMs:null,lastTimestampMs:null,nativeDroppedFrames:0}
    unsubDiagnostics=source.onFrame(frame=>{ const d=result.sourceFrames;d.count++;d.firstTimestampMs ??=frame.timestampMs;d.lastTimestampMs=frame.timestampMs;d.nativeDroppedFrames=frame.droppedFrames })
  }
  if (mode === 'stream') { result.audioStarted = await page.evaluate(() => window.startAudioProfile()); await delay(2000) }
  await page.evaluate(() => window.activate('canvas'))
  await delay(1000)
  result.beforeCapture = await page.evaluate(() => window.snapshot())
  assert.equal(result.beforeCapture.width,1920);assert.equal(result.beforeCapture.height,1080)
  if (page.screenshot) await page.screenshot({path:join(directory,'browser.png')})
  const roots = [process.pid,process.env.OBS_PID,process.env.XVFB_PID,process.env.PULSE_PID].filter(Boolean)
  sampler = spawn('python3',['benchmarks/publication/sample.py',join(directory,'resources.jsonl'),...roots.map(String)],{stdio:['ignore','ignore','pipe']})
  sampler.stderr.pipe(createWriteStream(join(directory,'sampler.log')))
  const outputStart = monotonic()
  if (obs) {
    if (mode === 'record') { await obs.request('StartRecord'); recording = true }
    else { await obs.request('StartStream'); streaming = true }
    stopOutput = async () => {
      if (mode === 'record') {
        const stopped = await obs.request('StopRecord');recording = false
        result.originalRecordPath = stopped.outputPath
        if (stopped.outputPath !== result.recordingPath) await rename(stopped.outputPath,result.recordingPath)
      } else { result.finalStream = await obs.request('GetStreamStatus');await obs.request('StopStream');streaming = false }
      result.obsAfter = await obs.request('GetStats')
    }
  } else if (engine) {
    const config = {audio:mode === 'stream',videoCodec:'libx264',audioCodec:'aac',width:1920,height:1080,fps:30}
    if (mode === 'record') nativeRecord = await engine.record(page,{...config,destination:result.recordingPath,crf:18})
    else nativeStream = await engine.stream(page,{...config,destination:rtmp,bitrateKbps:4500,audioBitrateKbps:128})
    const output = nativeRecord ?? nativeStream
    output.failure.catch(error => { failure = error })
    stopOutput = async () => { result.finalOutput = await output.stop() }
  } else if (source) {
    if (mode === 'record') {
      nativeRecord = oldRecorder.createNativeVideoRecorder({source,ffmpegPath:normalized,outputPath:result.recordingPath,size:{width:1920,height:1080},audio:false})
      nativeRecord.failure.catch(error => { failure = error })
      unsub = source.onFrame(frame => nativeRecord.push(frame.data,frame.timestampMs))
      for (let index = 0; nativeRecord.firstAtMs === undefined && index < 100; index++) { if(failure)throw failure;await delay(50) }
      assert(nativeRecord.firstAtMs !== undefined,'Old CEF recorder received no frame')
      stopOutput = async () => { unsub();await nativeRecord.stop(performance.now()) }
    } else {
      nativeStream = await legacy.createNativeBroadcast({source,ffmpegPath:normalized,stream:{url:rtmp,audio:true,bitrateKbps:4500,size:{width:1920,height:1080}}})
      nativeStream.failure.catch(error => { failure = error });await nativeStream.ready()
      stopOutput = async () => { result.finalHealth = nativeStream.health();await nativeStream.stop() }
    }
  } else {
    session = await oldSessions.createRecordingSession({page,output:oldSink.rawArtifactSink(directory),captureOptions:{
      viewport:{width:1920,height:1080},size:{width:1920,height:1080},framesPerSecond:30,
      ...(mode === 'stream' ? {stream:{url:rtmp,audio:true,bitrateKbps:4500}} : {}),
    }})
    session.streamFailure?.catch(error => { failure = error })
    stopOutput = async () => {
      const sealed = await session.seal();await writeFile(join(directory,'session.json'),JSON.stringify(sealed,null,2))
      if (mode === 'record') {
        const artifact = sealed.videos[0];assert(artifact,'Old external recorder produced no source video')
        const path = join(directory,artifact.attachmentName);result.originalRecordPath = path
        if(path !== result.recordingPath)await rename(path,result.recordingPath)
      }
    }
  }
  result.outputReadySeconds = monotonic() - outputStart
  await delay(1000)
  result.activeBaseline = await page.evaluate(() => window.snapshot());result.activeStartMonotonic = monotonic();result.status = 'running';result.nodeBefore = process.memoryUsage();await save()
  let lastPhase = -1,lastWheel = -1,lastStatus = -1,lastSave = -1,scrolls = 0,actions = 0
  const startActive = performance.now()
  while (performance.now() - startActive < duration) {
    if (failure) throw failure
    assert(sampler.exitCode===null,'Resource sampler exited during the workload')
    if (receiver?.exitCode !== null && receiver?.exitCode !== undefined) throw new Error('RTMP receiver exited before the workload completed')
    const at = performance.now() - startActive
    const phase = Math.floor(at/10000)%3
    if (phase !== lastPhase) {
      const name = ['static','canvas','scroll'][phase]
      await page.evaluate(mode => window.activate(mode),name)
      if (phase === 0) {
        if(source)await source.sendDevToolsCommand('Runtime.evaluate',{expression:'window.scrollTo(0,0)'})
        else await page.evaluate(() => window.scrollTo(0,0))
      }
      result.phases.push({name,atMs:at,monotonic:monotonic()});lastPhase = phase
    }
    if (phase === 1 && !actions) {
      await page.locator('#click').click();await delay(200)
      await page.locator('#entry').fill('SuiteCut benchmark');await delay(200)
      await page.locator('#choice').evaluate(element => { element.value='Second';element.dispatchEvent(new Event('change',{bubbles:true})) })
      actions++
      result.events.push({name:'click, input and selection',atMs:performance.now()-startActive})
    }
    if (phase === 2 && at-lastWheel >= 500) {
      const params={type:'mouseWheel',x:600,y:400,deltaX:0,deltaY:scrolls%16<8?180:-180}
      if(source)await source.sendDevToolsCommand('Input.dispatchMouseEvent',params)
      else await cdp.send('Input.dispatchMouseEvent',params)
      scrolls++;lastWheel=at
    }
    if (at-lastStatus >= 1000) {
      const row={atMs:at,monotonic:monotonic(),nodeMemory:process.memoryUsage()}
      if (obs) {
        const outputStatus = await obs.request(mode === 'record'?'GetRecordStatus':'GetStreamStatus')
        assert(outputStatus.outputActive,'OBS output became inactive');assert(!outputStatus.outputReconnecting,'OBS reconnected during the workload')
        row.output = outputStatus
      } else if(engine)row.output = await (nativeRecord ?? nativeStream).status()
      statusFile.write(JSON.stringify(row)+'\n');lastStatus=at
    }
    if(at-lastSave>=5000){result.observedActiveMs=at;result.scrollCommands=scrolls;await save();lastSave=at}
    await delay(50)
  }
  result.activeEndMonotonic = monotonic();result.activeMs = performance.now()-startActive
  result.afterCapture = await page.evaluate(() => window.snapshot());result.scrollCommands=scrolls;result.nodeAfter = process.memoryUsage()
  if(duration>=30000){assert.equal(result.afterCapture.actionMask,15,'Browser operations did not update every recorded action bit');assert(result.afterCapture.scrollEvents>0,'No actual browser scroll event')}
  const stopStart = monotonic();await stopOutput();stopOutput=undefined;result.finalizationSeconds=monotonic()-stopStart
  if(receiverClosed)await Promise.race([receiverClosed,delay(15000,undefined,{ref:false}).then(() => { throw new Error('RTMP receiver finalization timeout') })])
  result.status = 'completed; media validation pending'
} catch (error) {
  result.status = 'failed';result.error = String(error.stack ?? error);failure=error
} finally {
  unsubDiagnostics()
  await stopOutput?.().catch(error => {result.cleanupOutputError=String(error)})
  if(streaming)await obs?.request('StopStream').catch(() => {})
  if(recording)await obs?.request('StopRecord').catch(() => {})
  unsub();await cdp?.detach().catch(() => {});await obsConnection?.close().catch(() => {})
  await source?.close().catch(() => {});await engine?.close().catch(() => {})
  await context?.close().catch(() => {});await browser?.close().catch(() => {})
  if(receiver?.exitCode===null){
    receiver.kill('SIGTERM')
    const exited=once(receiver,'close')
    const timer=setTimeout(() => receiver.kill('SIGKILL'),2000)
    await exited;clearTimeout(timer)
  }
  await delay(500)
  if(sampler?.exitCode===null){sampler.kill('SIGTERM');await once(sampler,'close')}
  obs?.close();server.closeAllConnections();await new Promise(done => server.close(done))
  await new Promise(done => statusFile.end(done));loop.disable()
  result.eventLoop={meanMs:loop.mean/1e6,p99Ms:loop.percentile(99)/1e6,maxMs:loop.max/1e6}
  if(failure && !result.error){result.cleanupFailure=String(failure)}
  result.nodeAfterCleanup=process.memoryUsage();result.completedAt=new Date().toISOString();await save()
}
console.log(JSON.stringify({backend,mode,status:result.status,error:result.error,directory}))
if(failure)process.exitCode=1
