import assert from 'node:assert/strict'
import {spawn} from 'node:child_process'
import {once} from 'node:events'
import {createWriteStream} from 'node:fs'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {createServer} from 'node:http'
import {createServer as socketServer} from 'node:net'
import {join,resolve} from 'node:path'
import {monitorEventLoopDelay,performance} from 'node:perf_hooks'
import {setTimeout as delay} from 'node:timers/promises'
import {chromium} from 'playwright'
import {ObsClient} from './obs-client.mjs'

const root=resolve(process.env.OBS_LONG_OUTPUT??'benchmarks/obs-cef/2026-10-09-obs-long-control')
const media=resolve(process.env.OBS_LONG_MEDIA??'.suitecut/obs-long-control/2026-10-09/media')
const duration=Number(process.env.OBS_LONG_DURATION_MS??1800000)
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const result={backend:'obs',startedAt:new Date().toISOString(),requestedMs:duration,width:1920,height:1080,targetFps:30,syncPulseMs:250,wheelEveryMs:5000,mode:'simultaneous independent OBS recording and RTMP streaming from one external browser display/audio source',status:'starting',privateMemorySampling:false}
async function save(){await writeFile(join(root,'result.json'),JSON.stringify(result,null,2))}
await save()
const fixture=await readFile(new URL('./fixture.html',import.meta.url),'utf8')
const server=createServer((_req,res)=>{res.setHeader('Content-Type','text/html');res.end(fixture)})
server.listen(0,'127.0.0.1');await once(server,'listening')
const portProbe=socketServer();portProbe.listen(0,'127.0.0.1');await once(portProbe,'listening');const port=portProbe.address().port;await new Promise(r=>portProbe.close(r))
const receiverPath=join(media,'receiver.flv');result.receiverPath=receiverPath
const receiver=spawn('ffmpeg',['-v','error','-y','-listen','1','-i',`rtmp://127.0.0.1:${port}/live/soak`,'-c','copy',receiverPath],{stdio:['ignore','ignore','pipe']})
const receiverLog=[];receiver.stderr.on('data',x=>receiverLog.push(x));const receiverClosed=once(receiver,'close');receiverClosed.catch(()=>{})
const obs=new ObsClient(),samples=createWriteStream(join(root,'status-samples.jsonl'))
const loop=monitorEventLoopDelay({resolution:20});loop.enable()
let browser,recording=false,streaming=false,sampler
try{
 await obs.connect()
 browser=await chromium.launch({headless:false,chromiumSandbox:true,args:['--window-position=0,0','--window-size=3840,2300','--autoplay-policy=no-user-gesture-required']})
 result.browserVersion=browser.version()
 const context=await browser.newContext({viewport:{width:1920,height:1080},deviceScaleFactor:1});const page=await context.newPage();const cdp=await context.newCDPSession(page)
 await page.goto(`http://127.0.0.1:${server.address().port}`);await page.bringToFront()
 await obs.request('SetVideoSettings',{baseWidth:1920,baseHeight:1080,outputWidth:1920,outputHeight:1080,fpsNumerator:30,fpsDenominator:1})
 const {sceneItemId}=await obs.request('GetSceneItemId',{sceneName:'Scene',sourceName:'Benchmark display'})
 await obs.request('SetSceneItemEnabled',{sceneName:'Scene',sceneItemId,sceneItemEnabled:true})
 await obs.request('SetSceneItemTransform',{sceneName:'Scene',sceneItemId,sceneItemTransform:{positionX:0,positionY:0,scaleX:1,scaleY:1}})
 await obs.request('SetInputSettings',{inputName:'Benchmark display',inputSettings:{advanced:true,cut_top:87,cut_bot:2300-87-1080,cut_left:0,cut_right:3840-1920,show_cursor:false},overlay:true})
 const {inputs}=await obs.request('GetInputList')
 if(!inputs.some(i=>i.inputName==='Benchmark audio'))await obs.request('CreateInput',{sceneName:'Scene',inputName:'Benchmark audio',inputKind:'pulse_output_capture',inputSettings:{device_id:'benchmark.monitor'},sceneItemEnabled:true})
 await obs.request('SetInputMute',{inputName:'Benchmark audio',inputMuted:false})
 await obs.request('SetRecordDirectory',{recordDirectory:media})
 for(const [name,value] of Object.entries({Encoder:'obs_x264',RecEncoder:'obs_x264',TrackIndex:1,ApplyServiceSettings:false}))await obs.request('SetProfileParameter',{parameterCategory:'AdvOut',parameterName:name,parameterValue:String(value)})
 await obs.request('SetStreamServiceSettings',{streamServiceType:'rtmp_custom',streamServiceSettings:{server:`rtmp://127.0.0.1:${port}/live`,key:'soak',use_auth:false}})
 result.effectiveVideoSettings=await obs.request('GetVideoSettings');result.obsBefore=await obs.request('GetStats');result.beforeStart=process.memoryUsage()
 await obs.request('StartRecord');recording=true
 await page.evaluate(()=>window.activate('canvas'))
 // Identical 250 ms pulse function to the final SuiteCut soak.
 await page.evaluate(async()=>{
  const canvas=document.createElement('canvas');canvas.width=64;canvas.height=64;Object.assign(canvas.style,{position:'fixed',left:'400px',top:'0px',zIndex:1000});document.body.append(canvas);const ctx=canvas.getContext('2d')
  const audio=new AudioContext({sampleRate:48000});await audio.resume();const osc=audio.createOscillator(),gain=audio.createGain();osc.frequency.value=440;gain.gain.value=0;osc.connect(gain).connect(audio.destination);osc.start();let next=Math.ceil(audio.currentTime)+1
  setInterval(()=>{while(next<audio.currentTime+2){gain.gain.setValueAtTime(.15,next);gain.gain.setValueAtTime(0,next+.25);next++}},50)
  function draw(){ctx.fillStyle=audio.currentTime%1<.25?'white':'black';ctx.fillRect(0,0,64,64);requestAnimationFrame(draw)}draw()
 })
 await obs.request('StartStream');streaming=true
 let ready=false
 for(let i=0;i<120;i++){if((await obs.request('GetStreamStatus')).outputActive){ready=true;break}await delay(250)}
 assert(ready,'OBS RTMP publisher did not become active')
 await obs.request('SaveSourceScreenshot',{sourceName:'Benchmark display',imageFormat:'png',imageFilePath:join(root,'start-capture.png')})
 result.baselineSnapshot=await page.evaluate(()=>window.snapshot())
 const runStart=performance.now();result.readyAt=new Date().toISOString();result.status='running';result.baseline=process.memoryUsage();await save()
 sampler=spawn('python3',[resolve('benchmarks/obs-cef/profile-obs-long.py'),join(root,'processes.jsonl'),String(process.pid),process.env.OBS_PID,process.env.XVFB_PID,process.env.PULSE_PID],{stdio:['ignore','ignore','pipe']})
 sampler.stderr.on('data',data=>{void writeFile(join(root,'sampler-error.log'),data)})
 let scrolls=0,lastScroll=runStart,lastPoll=runStart,lastSave=runStart,lastSnapshot=runStart
 while(performance.now()-runStart<duration){
  await delay(250)
  if(receiver.exitCode!==null)throw new Error(`RTMP receiver exited early: ${receiver.exitCode}`)
  if(performance.now()-lastScroll>5000){await cdp.send('Input.dispatchMouseEvent',{type:'mouseWheel',x:600,y:400,deltaX:0,deltaY:(scrolls++%40<20?180:-180)});lastScroll=performance.now()}
  if(performance.now()-lastPoll>1000){
   const stream=await obs.request('GetStreamStatus'),record=await obs.request('GetRecordStatus'),stats=await obs.request('GetStats')
   assert(stream.outputActive&&!stream.outputReconnecting,'OBS streaming became inactive/reconnecting');assert(record.outputActive,'OBS recording became inactive')
   const row={atMs:performance.now()-runStart,stream,record,stats,nodeMemory:process.memoryUsage()}
   if(performance.now()-lastSnapshot>30000){row.browserSnapshot=await page.evaluate(()=>window.snapshot());lastSnapshot=performance.now()}
   samples.write(JSON.stringify(row)+'\n');lastPoll=performance.now();result.latestStatus=row
  }
  if(performance.now()-lastSave>10000){result.observedActiveMs=performance.now()-runStart;result.scrollCommands=scrolls;await save();lastSave=performance.now()}
 }
 result.activeMs=performance.now()-runStart;result.snapshot=await page.evaluate(()=>window.snapshot());result.scrollCommands=scrolls;result.obsAfter=await obs.request('GetStats');result.finalStreamStatus=await obs.request('GetStreamStatus');result.finalRecordStatus=await obs.request('GetRecordStatus')
 const stopStart=performance.now();await obs.request('StopStream');streaming=false
 for(let i=0;i<120;i++){if(!(await obs.request('GetStreamStatus')).outputActive)break;await delay(250)}
 const stopped=await obs.request('StopRecord');recording=false;result.recordingPath=stopped.outputPath
 for(let i=0;i<120;i++){if(!(await obs.request('GetRecordStatus')).outputActive)break;await delay(250)}
 await Promise.race([receiverClosed,delay(15000).then(()=>{throw new Error('RTMP receiver finalization timed out')})])
 result.stopMs=performance.now()-stopStart;result.status='workload completed; media validation pending'
}catch(error){result.status='failed';result.error=String(error.stack??error)}
finally{
 if(streaming)await obs.request('StopStream').catch(()=>{})
 if(recording)await obs.request('StopRecord').then(x=>{result.recordingPath=x.outputPath}).catch(()=>{})
 sampler?.kill('SIGTERM');await browser?.close().catch(()=>{});receiver.kill('SIGINT');obs.close();server.closeAllConnections();server.close();samples.end();loop.disable()
 result.afterCleanup=process.memoryUsage();globalThis.gc?.();result.afterCleanupGc=process.memoryUsage();result.eventLoopDelay={meanMs:loop.mean/1e6,p99Ms:loop.percentile(99)/1e6,maxMs:loop.max/1e6};result.completedAt=new Date().toISOString();await writeFile(join(root,'receiver.log'),Buffer.concat(receiverLog));await save();console.log(JSON.stringify(result,null,2))
}
if(result.error)process.exitCode=1
