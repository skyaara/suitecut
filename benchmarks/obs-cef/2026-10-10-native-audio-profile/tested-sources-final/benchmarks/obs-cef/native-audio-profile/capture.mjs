import {createServer} from 'node:http'
import {createServer as netServer} from 'node:net'
import {once} from 'node:events'
import {spawn} from 'node:child_process'
import {readFile,writeFile,mkdir,rename} from 'node:fs/promises'
import {createWriteStream} from 'node:fs'
import {resolve,join} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {chromium} from 'playwright'
import {launchEngine} from '../../../dist/engine.js'
import {ObsClient} from '../obs-client.mjs'
import {connectObs} from './cdp.mjs'
import {audioFixture} from './fixture.mjs'

const directory=resolve(process.env.PROFILE_DIRECTORY)
await mkdir(directory,{recursive:true})
const backend=process.env.PROFILE_BACKEND??'suitecut'
const duration=Number(process.env.PROFILE_DURATION_MS??60000)
const stress=process.env.PROFILE_STRESS==='1',cold=process.env.PROFILE_COLD==='1'
const result={backend,durationRequestedMs:duration,stress,cold,width:1920,height:1080,fps:30,
  startedAt:new Date().toISOString(),fixture:'Continuous deterministic AudioWorklet noise; no intentional silence',
  events:[],status:'starting',recordingPath:join(directory,'recording.mkv'),receiverPath:join(directory,'receiver.flv')}
const save=()=>writeFile(join(directory,'result.json'),JSON.stringify(result,null,2))
async function hostMemory(){
 const values={}
 for(const name of ['memory.current','memory.events','memory.stat']) {
  try{values[name]=await readFile('/sys/fs/cgroup/'+name,'utf8')}catch{}
 }
 for(const name of ['cpu','memory','io']) {
  try{values['pressure.'+name]=await readFile('/proc/pressure/'+name,'utf8')}catch{}
 }
 return values
}
result.hostMemoryBefore=await hostMemory()
await save()
const html=audioFixture(await readFile('benchmarks/obs-cef/fixture.html','utf8'))
await writeFile(join(directory,'fixture.html'),html)
const server=createServer((_req,res)=>{res.setHeader('content-type','text/html');res.end(html)})
server.listen(0,'127.0.0.1');await once(server,'listening')
const url=`http://127.0.0.1:${server.address().port}`
const portProbe=netServer();portProbe.listen(0,'127.0.0.1');await once(portProbe,'listening')
const port=portProbe.address().port;await new Promise(r=>portProbe.close(r))
const rtmp=`rtmp://127.0.0.1:${port}/live/profile`
const receiver=spawn('ffmpeg',['-v','error','-y','-listen','1','-i',rtmp,'-c','copy',result.receiverPath],{stdio:['ignore','ignore','pipe']})
receiver.stderr.pipe(createWriteStream(join(directory,'receiver.log')))
const receiverClosed=once(receiver,'close');receiverClosed.catch(()=>{})
const status=createWriteStream(join(directory,'status.jsonl'))
let engine,obs,connection,page,record,stream,sampler,recording=false,streaming=false
let traceSession,traceFile,traceCompleted
let runStart
try {
 if(backend==='obs') {
  for(let i=0;i<80;i++){obs=new ObsClient();try{await obs.connect();break}catch{obs.close();obs=undefined;await delay(500)}}
  if(!obs)throw new Error('OBS never became ready')
  result.version=await obs.request('GetVersion')
  const kinds=await obs.request('GetInputKindList')
  if(!kinds.inputKinds.includes('browser_source'))throw new Error('Real OBS CEF source is required')
  await obs.request('SetVideoSettings',{baseWidth:1920,baseHeight:1080,outputWidth:1920,outputHeight:1080,fpsNumerator:30,fpsDenominator:1})
  const scene='CEF Audio Profile',source='Audio profile '+Date.now()
  const scenes=await obs.request('GetSceneList')
  if(!scenes.scenes.some(s=>s.sceneName===scene))await obs.request('CreateScene',{sceneName:scene})
  const inputs=await obs.request('GetInputList')
  for(const i of inputs.inputs)await obs.request('RemoveInput',{inputName:i.inputName})
  await obs.request('CreateInput',{sceneName:scene,inputName:source,inputKind:'browser_source',inputSettings:{url,width:1920,height:1080,fps:30,fps_custom:true,reroute_audio:true,shutdown:false,css:'body {margin:0}'},sceneItemEnabled:true})
  await obs.request('SetCurrentProgramScene',{sceneName:scene})
  await obs.request('SetInputMute',{inputName:source,inputMuted:false})
  await obs.request('SetInputVolume',{inputName:source,inputVolumeMul:1})
  await obs.request('SetRecordDirectory',{recordDirectory:directory})
  for(const [parameterName,value] of Object.entries({Encoder:'obs_x264',RecEncoder:'obs_x264',AudioEncoder:'ffmpeg_aac',RecAudioEncoder:'ffmpeg_aac',Track1Bitrate:128,ApplyServiceSettings:false}))
   await obs.request('SetProfileParameter',{parameterCategory:'AdvOut',parameterName,parameterValue:String(value)})
  await obs.request('SetStreamServiceSettings',{streamServiceType:'rtmp_custom',streamServiceSettings:{server:`rtmp://127.0.0.1:${port}/live`,key:'profile',use_auth:false}})
  await delay(2000)
  connection=await connectObs(chromium,directory)
  result.cdp=connection.version
  page=connection.browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().startsWith(url))
  if(!page)throw new Error('CEF target is missing')
  result.browserVersion=connection.browser.version()
  result.obsBefore=await obs.request('GetStats')
 } else {
  engine=await launchEngine({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1920,height:1080,framesPerSecond:30})
  page=engine.browser.contexts()[0].pages()[0]
  await page.goto(url)
  result.browserVersion=engine.browser.version()
 }
 await page.evaluate(()=>window.activate('canvas'))
 if(process.env.PROFILE_CHROMIUM_TRACE==='1') {
  try {
   traceSession=await page.context().newCDPSession(page)
   traceFile=createWriteStream(join(directory,'chromium-audio-trace.jsonl'))
   traceSession.on('Tracing.dataCollected',event=>{for(const value of event.value)traceFile.write(JSON.stringify(value)+'\n')})
   traceCompleted=new Promise(resolve=>traceSession.once('Tracing.tracingComplete',resolve))
   await traceSession.send('Tracing.start',{categories:'audio',transferMode:'ReportEvents',options:'record-until-full'})
   result.chromiumTracing='audio category enabled'
  } catch(error){result.chromiumTracing=String(error);traceSession=undefined;traceFile?.end()}
 }
 if(!cold){result.audioStart=await page.evaluate(()=>window.startAudioProfile());await delay(2000)}
 const roots=backend==='obs'?[process.pid,process.env.OBS_PID,process.env.XVFB_PID,process.env.PULSE_PID]:[process.pid]
 sampler=spawn('python3',['benchmarks/obs-cef/profile-obs-long.py',join(directory,'processes.jsonl'),...roots.map(String)],{stdio:'ignore'})
 if(backend==='obs') {
  await obs.request('StartRecord');recording=true
  await obs.request('StartStream');streaming=true
  for(let i=0;i<120;i++){if((await obs.request('GetStreamStatus')).outputActive)break;await delay(100)}
 } else {
  record=await engine.record(page,{destination:result.recordingPath,videoCodec:'libx264',audioCodec:'aac',crf:18,audioBitrateKbps:128});recording=true
  stream=await engine.stream(page,{destination:rtmp,videoCodec:'libx264',audioCodec:'aac',bitrateKbps:4500,audioBitrateKbps:128});streaming=true
 }
 runStart=performance.now()
 if(cold){await delay(1000);result.audioStart=await page.evaluate(()=>window.startAudioProfile());result.audioStartAtMs=performance.now()-runStart}
 result.status='running';await save()
 let lastPoll=0,lastScroll=0,blockedPage=false,blockedController=false
 while(performance.now()-runStart<duration) {
  await delay(100)
  const at=performance.now()-runStart
  if(receiver.exitCode!==null)throw new Error('Receiver exited early')
  if(at-lastScroll>5000){await page.mouse.wheel(0,Math.floor(at/5000)%2?180:-180);lastScroll=at}
  if(stress&&!blockedPage&&at>20000){blockedPage=true;result.events.push({atMs:at,type:'renderer-main-thread-block',durationMs:4000});await page.evaluate(()=>{const until=performance.now()+4000;while(performance.now()<until){}})}
  if(stress&&!blockedController&&at>40000){blockedController=true;result.events.push({atMs:at,type:'controller-block',durationMs:4000});Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,4000)}
  if(at-lastPoll>1000){
   const row={atMs:at,record:backend==='obs'?await obs.request('GetRecordStatus'):await record.status(),stream:backend==='obs'?await obs.request('GetStreamStatus'):await stream.status()}
   if(Math.floor(at/1000)%10===0)row.hostMemory=await hostMemory()
   if(backend==='obs')row.stats=await obs.request('GetStats')
   status.write(JSON.stringify(row)+'\n');lastPoll=at;result.latest=row;await save()
  }
 }
 result.activeMs=performance.now()-runStart
 result.hostMemoryAfter=await hostMemory()
 result.page=await page.evaluate(()=>({snapshot:window.snapshot(),audioProfile:window.audioProfile}))
 if(backend==='obs') {
  result.obsAfter=await obs.request('GetStats')
  await obs.request('StopStream');streaming=false
  for(let i=0;i<120;i++){if(!(await obs.request('GetStreamStatus')).outputActive)break;await delay(100)}
  const stopped=await obs.request('StopRecord');recording=false;result.originalRecordingPath=stopped.outputPath
  await rename(stopped.outputPath,result.recordingPath)
 } else {
  result.finalStream=await stream.stop();streaming=false
  result.finalRecord=await record.stop();recording=false
 }
 const [code]=await Promise.race([receiverClosed,delay(15000).then(()=>{throw new Error('Receiver did not finish')})])
 result.receiverExitCode=code
 if(traceSession){await traceSession.send('Tracing.end');await Promise.race([traceCompleted,delay(15000).then(()=>{throw new Error('Chromium tracing did not flush')})]);await new Promise(r=>traceFile.end(r));await traceSession.detach();traceSession=undefined}
 result.status='capture-completed; analysis-pending'
} catch(error){result.status='failed';result.error=String(error.stack??error)}
finally {
 if(streaming){if(obs)await obs.request('StopStream').catch(()=>{});else await stream?.stop().catch(()=>{})}
 if(recording){if(obs)await obs.request('StopRecord').catch(()=>{});else await record?.stop().catch(()=>{})}
 sampler?.kill('SIGTERM');await connection?.close().catch(()=>{});await engine?.close().catch(()=>{})
 traceFile?.end()
 obs?.close();receiver.kill('SIGINT');status.end();server.closeAllConnections();await new Promise(r=>server.close(r))
 result.completedAt=new Date().toISOString();await save();console.log(JSON.stringify(result,null,2))
}
if(result.error)process.exitCode=1
