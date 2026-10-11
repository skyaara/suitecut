import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as tcpServer } from 'node:net'
import { resolve, join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'
import { audioFixture } from '../obs-cef/native-audio-profile/fixture.mjs'

const directory=resolve(process.env.SYNC_DIRECTORY),width=Number(process.env.SYNC_WIDTH??1920),height=Number(process.env.SYNC_HEIGHT??1080),fps=Number(process.env.SYNC_FPS??30),duration=Number(process.env.SYNC_DURATION_MS??60000),mode=process.env.SYNC_MODE??'record'
await mkdir(directory,{recursive:true})
let html=await readFile('benchmarks/publication/fixture.html','utf8')
html=html.replaceAll('*.03',`*${fps/1000}`).replaceAll('1080p30',`${width}×${height} / ${fps} FPS`)
html=audioFixture(html)
await writeFile(join(directory,'fixture.html'),html)
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end(html)})
server.listen(0,'127.0.0.1');await once(server,'listening')
const monotonic=()=>Number(process.hrtime.bigint())/1e9
const result={backend:'cpp-cef',mode,profile:process.env.SYNC_PROFILE??'performance',width,height,fps,durationRequestedMs:duration,startedAt:new Date().toISOString(),status:'starting',recordingPath:join(directory,'recording.mkv'),receiverPath:join(directory,'receiver.flv'),phases:[],encoding:{video:'H264 veryfast zerolatency, GOP 2 seconds; configured thread count in finalOutput',audio:'AAC 48kHz stereo 128kbps',recordCrf:18,streamBitrateKbps:width>=3840?12000:width>=1920?4500:3500}}
const save=()=>writeFile(join(directory,'result.json'),JSON.stringify(result,null,2)+'\n')
result.encoding.video=`H264 ${width>=3840&&height>=2160?'ultrafast':'veryfast'} zerolatency, GOP 2 seconds; configured thread count/preset in finalOutput`
let engine,sampler,receiver,receiverClosed,output,timer,failure
const statuses=createWriteStream(join(directory,'status.jsonl'))
process.once('SIGTERM',()=>{failure=new Error('Runtime termination / memory guard')})
await save()
try{
 let rtmp
 if(mode==='stream'){
  const socket=tcpServer();socket.listen(0,'127.0.0.1');await once(socket,'listening');const port=socket.address().port;await new Promise(done=>socket.close(done));rtmp=`rtmp://127.0.0.1:${port}/live/sharedclock`
  receiver=spawn('/usr/bin/ffmpeg',['-v','error','-y','-listen','1','-i',rtmp,'-c','copy',result.receiverPath],{stdio:['ignore','ignore','pipe']});receiver.stderr.pipe(createWriteStream(join(directory,'receiver.log')));receiverClosed=once(receiver,'close');void receiverClosed.catch(()=>{});await delay(250)
 }
 engine=await launchEngine({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width,height,framesPerSecond:fps})
 engine.source.failure.catch(error=>{failure=error})
 const page=engine.browser.contexts()[0].pages()[0];await page.goto(`http://127.0.0.1:${server.address().port}`)
 result.cefVersion=engine.source.cefVersion;result.browserVersion=engine.browser.version();result.audioStarted=await page.evaluate(()=>window.startAudioProfile());await page.evaluate(()=>window.activate('static'));await delay(750)
 // This benchmark deliberately runs continuous audio. Wait for captured PCM,
 // including zero-valued PCM, rather than guessing readiness from audibility.
 const audioWait=performance.now();let audioState
 do{audioState=await engine.source.command('audio.status');if(audioState.active&&audioState.packets>=8)break;await delay(25)}while(performance.now()-audioWait<10000)
 assert(audioState.active&&audioState.packets>=8,'Continuous fixture did not initialize native audio capture');result.captureAudioReadiness={waitMs:performance.now()-audioWait,...audioState}
 result.beforeCapture=await page.evaluate(()=>window.snapshot());assert.equal(result.beforeCapture.width,width);assert.equal(result.beforeCapture.height,height)
 sampler=spawn('python',['benchmarks/publication/sample.py',join(directory,'resources.jsonl'),String(process.pid),process.env.XVFB_PID,process.env.PULSE_PID],{stdio:['ignore','ignore','pipe']});sampler.stderr.pipe(createWriteStream(join(directory,'sampler.log')))
 const config={width,height,fps,audio:true,audioBitrateKbps:128}
 output=mode==='stream'?await engine.stream(page,{...config,destination:rtmp,bitrateKbps:result.encoding.streamBitrateKbps}):await engine.record(page,{...config,destination:result.recordingPath,crf:18})
 output.failure.catch(error=>{failure=error})
 for(let i=0;i<250;i++){if(failure)throw failure;if((await output.status()).ready)break;await delay(20)}
 assert((await output.status()).ready,'Native output readiness timeout')
 result.activeBaseline=await page.evaluate(()=>window.snapshot());result.activeStartMonotonic=monotonic();const start=performance.now();result.status='running';await save()
 let phase=-1,scrollAt=0,inputs=false,scrollCommands=0,stallInjected=false
 timer=setInterval(async()=>{try{statuses.write(JSON.stringify({atMs:performance.now()-start,nodeMemory:process.memoryUsage(),output:await output.status()})+'\n')}catch(error){failure=error}},1000)
 while(performance.now()-start<duration){
  if(failure)throw failure;const elapsed=performance.now()-start,p=Math.floor(elapsed/10000)%3
  if(p!==phase){phase=p;const name=['static','canvas','scroll'][p];await page.evaluate(name=>window.activate(name),name);result.phases.push({name,atMs:elapsed,monotonic:monotonic()})}
  if(!inputs&&elapsed>=10000){result.inputDwellMs=200;await page.locator('#click').click();await delay(200);await page.locator('#entry').fill('SuiteCut benchmark');await delay(200);await page.locator('#choice').selectOption({label:'Second'});await delay(200);inputs=true}
  if(phase===2&&elapsed-scrollAt>=500){await page.mouse.wheel(0,scrollCommands%20<10?180:-180);scrollAt=elapsed;scrollCommands++}
  if(process.env.SYNC_CONTROLLER_STALL==='1'&&!stallInjected&&elapsed>15000){
   stallInjected=true;result.controllerBeforeStall={output:await output.status(),audio:await engine.source.command('audio.status')}
   const began=performance.now();result.controllerStallElapsedStartMs=began-start
   while(performance.now()-began<2000){}
   result.controllerStallMs=performance.now()-began;result.controllerStallElapsedEndMs=performance.now()-start
   result.controllerAfterStall={output:await output.status(),audio:await engine.source.command('audio.status')}
  }
  await delay(50)
 }
 result.activeEndMonotonic=monotonic();result.activeMs=(result.activeEndMonotonic-result.activeStartMonotonic)*1000;clearInterval(timer);timer=undefined
 result.afterCapture=await page.evaluate(()=>window.snapshot());result.scrollCommands=scrollCommands
 if(duration>=30000){assert.equal(result.afterCapture.actionMask&7,7);assert(result.afterCapture.scrollEvents>0)}
 result.finalOutput=await output.stop();output=undefined
 result.status='completed; media validation pending'
}catch(error){result.status='failed';result.error=String(error);console.error(error);process.exitCode=1}
finally{
 clearInterval(timer)
 if(output)try{result.finalOutput=await output.stop()}catch(error){result.stopError=String(error)}
 if(engine)try{await engine.close()}catch(error){result.cleanupFailure=String(error);process.exitCode=1}
 if(sampler){sampler.kill('SIGTERM');await once(sampler,'close')}
 if(receiver){await Promise.race([receiverClosed,delay(10000)]);if(receiver.exitCode===null){receiver.kill('SIGTERM');await receiverClosed}result.receiverExitCode=receiver.exitCode}
 server.closeAllConnections();await new Promise(done=>server.close(done));await new Promise(done=>statuses.end(done))
 result.finishedAt=new Date().toISOString();await save();console.log(JSON.stringify({directory,status:result.status,error:result.error,finalOutput:result.finalOutput}))
}
