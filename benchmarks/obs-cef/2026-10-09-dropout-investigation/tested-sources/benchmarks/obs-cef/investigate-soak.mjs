import assert from 'node:assert/strict'
import {spawn,execFile} from 'node:child_process'
import {once} from 'node:events'
import {createWriteStream} from 'node:fs'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {createServer} from 'node:http'
import {createServer as socketServer} from 'node:net'
import {resolve,join,dirname} from 'node:path'
import {monitorEventLoopDelay,performance} from 'node:perf_hooks'
import {setTimeout as delay} from 'node:timers/promises'
import {promisify} from 'node:util'
import {launchNativeBrowser,recordNative,createNativeBroadcast} from '../../dist/native.js'

const label=process.env.SOAK_LABEL??'soak'
const root=resolve('benchmarks/obs-cef/2026-10-09-dropout-investigation',label),media=resolve('.suitecut/dropout-investigation',label)
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const duration=Number(process.env.SOAK_DURATION_MS??1800000),exec=promisify(execFile)
const targetFps=Number(process.env.SOAK_FPS??60)
assert([30,60].includes(targetFps),'Supported long-run frame targets are 30 and 60')
const fixture=await readFile(new URL('./fixture.html',import.meta.url),'utf8')
const server=createServer((_req,res)=>{res.setHeader('Content-Type','text/html');res.end(fixture)})
server.listen(0,'127.0.0.1');await once(server,'listening')
const portProbe=socketServer();portProbe.listen(0,'127.0.0.1');await once(portProbe,'listening');const port=portProbe.address().port;await new Promise(r=>portProbe.close(r))
const streamUrl=`rtmp://127.0.0.1:${port}/live/soak`,receiverPath=join(media,'receiver.flv')
const receiver=spawn('ffmpeg',['-v','error','-y','-listen','1','-i',streamUrl,'-c','copy',receiverPath],{stdio:['ignore','ignore','pipe']})
const receiverLogs=[];receiver.stderr.on('data',x=>receiverLogs.push(x));const receiverClosed=once(receiver,'close');receiverClosed.catch(()=>{})
const samples=createWriteStream(join(root,'node-memory.jsonl'));const loop=monitorEventLoopDelay({resolution:20});loop.enable()
const result={startedAt:new Date().toISOString(),requestedMs:duration,width:1920,height:1080,targetFps,audio:true,syncPulseMs:250,privateMemoryEveryIterations:Number(process.env.PROFILE_PRIVATE_EVERY??20),mode:'simultaneous recording and RTMP streaming on one caller-owned CEF source',gcDuringRun:false,receiverPath}
let lastFrameAt,lastFrameReceived,lastMarker,lastMarkerAt,maxMarkerHoldMs=0,maxFrameReceiveGapMs=0,maxFrameCaptureGapMs=0,maxFrameTransitMs=0,nativeVideoReplaced=0,lastAudioAt,lastAudioReceived,maxAudioReceiveGapMs=0,maxAudioCaptureGapMs=0;
let source,broadcast,sampler,timer,frames=0,audioPackets=0,audioOffsets={count:0,minMs:Infinity,maxMs:-Infinity,futureOver100:0,futureOver500:0,pastOver10000:0},unsubscribeFrame,unsubscribeAudio,failure;const baselineCpu=process.cpuUsage(),baselineLoop=performance.eventLoopUtilization(),started=performance.now()
try{
 sampler=spawn('python3',[resolve('benchmarks/obs-cef/profile-pipeline.py'),join(root,'processes.jsonl'),String(process.pid)],{stdio:'ignore'})
 source=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1920,height:1080,framesPerSecond:targetFps,pixelFormat:'i420'})
 source.failure.catch(error=>{failure=error});await source.navigate(`http://127.0.0.1:${server.address().port}`)
 unsubscribeFrame=source.onFrame(frame=>{const now=performance.now();frames++;if(lastFrameReceived!==undefined)maxFrameReceiveGapMs=Math.max(maxFrameReceiveGapMs,now-lastFrameReceived);if(lastFrameAt!==undefined)maxFrameCaptureGapMs=Math.max(maxFrameCaptureGapMs,frame.timestampMs-lastFrameAt);maxFrameTransitMs=Math.max(maxFrameTransitMs,now-frame.timestampMs);lastFrameAt=frame.timestampMs;lastFrameReceived=now;nativeVideoReplaced=frame.droppedFrames;const marker=Array.from({length:20},(_,bit)=>frame.data[8+bit*16]>128?1<<bit:0).reduce((a,b)=>a+b,0);if(lastMarker!==marker){if(lastMarkerAt!==undefined)maxMarkerHoldMs=Math.max(maxMarkerHoldMs,frame.timestampMs-lastMarkerAt);lastMarker=marker;lastMarkerAt=frame.timestampMs}});unsubscribeAudio=source.onAudio(packet=>{audioPackets++;const now=performance.now();if(lastAudioReceived!==undefined)maxAudioReceiveGapMs=Math.max(maxAudioReceiveGapMs,now-lastAudioReceived);if(lastAudioAt!==undefined)maxAudioCaptureGapMs=Math.max(maxAudioCaptureGapMs,packet.timestampMs-lastAudioAt);lastAudioReceived=now;lastAudioAt=packet.timestampMs;const offset=packet.timestampMs-now;audioOffsets.count++;audioOffsets.minMs=Math.min(audioOffsets.minMs,offset);audioOffsets.maxMs=Math.max(audioOffsets.maxMs,offset);if(offset>100)audioOffsets.futureOver100++;if(offset>500)audioOffsets.futureOver500++;if(offset < -10000)audioOffsets.pastOver10000++})
 const recording=await recordNative('30-minute simultaneous browser recording and streaming',async({page})=>{
  await page.evaluate(()=>window.activate('canvas'))
  await page.evaluate(async()=>{
   const canvas=document.createElement('canvas');canvas.width=64;canvas.height=64;Object.assign(canvas.style,{position:'fixed',left:'400px',top:'0px',zIndex:1000});document.body.append(canvas);const ctx=canvas.getContext('2d')
   const audio=new AudioContext({sampleRate:48000});await audio.resume();const osc=audio.createOscillator(),gain=audio.createGain();osc.frequency.value=440;gain.gain.value=0;osc.connect(gain).connect(audio.destination);osc.start();let next=Math.ceil(audio.currentTime)+1
   setInterval(()=>{while(next<audio.currentTime+2){gain.gain.setValueAtTime(.15,next);gain.gain.setValueAtTime(0,next+.25);next++}},50)
   function draw(){ctx.fillStyle=audio.currentTime%1<.25?'white':'black';ctx.fillRect(0,0,64,64);requestAnimationFrame(draw)}draw()
  })
  broadcast=await createNativeBroadcast({source,ffmpegPath:'/usr/bin/ffmpeg',stream:{url:streamUrl,audio:true,bitrateKbps:4500,onDiagnostic:event=>{void writeFile(join(root,'latest-stream-diagnostic.json'),JSON.stringify(event,null,2))}}});broadcast.failure.catch(error=>{failure=error});await broadcast.ready()
  const runStart=performance.now();result.readyAt=new Date().toISOString();result.runStartPerformanceMs=runStart;result.runStartMonotonicSeconds=parseFloat(await readFile('/proc/uptime','utf8'))-(performance.now()-runStart)/1000;result.baseline=process.memoryUsage()
  timer=setInterval(()=>samples.write(JSON.stringify({atMs:performance.now()-runStart,frames,audioPackets,audioOffsets:{...audioOffsets},lastFrameAt,lastFrameReceived,lastAudioAt,lastAudioReceived,lastMarker,maxMarkerHoldMs,maxFrameReceiveGapMs,maxFrameCaptureGapMs,maxFrameTransitMs,nativeVideoReplaced,maxAudioReceiveGapMs,maxAudioCaptureGapMs,...process.memoryUsage(),cpu:process.cpuUsage(baselineCpu),eventLoop:performance.eventLoopUtilization(baselineLoop)})+'\n'),1000)
  let injected=false;let scrolls=0,lastScroll=runStart
  while(performance.now()-runStart<duration){
   await delay(250);if(failure)throw failure
   if(!injected && Number(process.env.INVESTIGATE_STALL_MS)>0 && performance.now()-runStart>40000){injected=true;result.injection={kind:'controlled Node event-loop blocking wait',atMs:performance.now()-runStart,durationMs:Number(process.env.INVESTIGATE_STALL_MS)};Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,result.injection.durationMs)}
   if(performance.now()-lastScroll>5000){await source.sendDevToolsCommand('Input.dispatchMouseEvent',{type:'mouseWheel',x:600,y:400,deltaX:0,deltaY:(scrolls++%40<20?180:-180)});lastScroll=performance.now()}
  }
  result.activeMs=performance.now()-runStart;result.snapshot=await page.evaluate(()=>window.snapshot());result.scrollCommands=scrolls;result.beforeStop=process.memoryUsage();result.frames=frames;result.audioPackets=audioPackets;result.pipelineTiming={maxMarkerHoldMs,maxFrameReceiveGapMs,maxFrameCaptureGapMs,maxFrameTransitMs,nativeVideoReplaced,maxAudioReceiveGapMs,maxAudioCaptureGapMs};result.audioOffsets=audioOffsets
  await broadcast.stop();broadcast=undefined
 },{source,capture:{audio:true},output:{directory:media,manifestPath:join(root,'manifest.json')}})
 result.recordingPath=resolve(dirname(recording.manifestPath),recording.manifest.tests[0].attempts[0].artifacts.find(a=>a.role==='source-video').path)
 clearInterval(timer);timer=undefined;await Promise.race([receiverClosed,delay(10000).then(()=>receiver.kill('SIGINT'))]);result.sourceStateAfterRecording=source.state
 for(const [name,path] of [['recording',result.recordingPath],['receiver',receiverPath]]){
  const {stdout}=await exec('ffprobe',['-v','error','-show_streams','-show_format','-of','json',path]);const probe=JSON.parse(stdout);await writeFile(join(root,`${name}-probe.json`),stdout)
  assert(Number(probe.format.duration)>=duration/1000*.95,`${name} duration truncated`);assert(probe.streams.some(s=>s.codec_type==='audio'));const v=probe.streams.find(s=>s.codec_type==='video');assert.equal(v.width,1920);assert.equal(v.height,1080)
  const windows=[]
  for(const offset of [...new Set([5,Math.floor(duration/2000),Math.floor(duration/1000)-20])]){
   const out=join(root,`${name}-${offset}`);await mkdir(out,{recursive:true})
   // Decode synchronized 15-second windows at the beginning, middle and end.
   await exec('ffmpeg',['-v','error','-threads','2','-ss',String(offset),'-i',path,'-t','15','-an','-vf','crop=64:64:400:0,scale=1:1,fps=60:start_time=0,format=gray','-f','rawvideo','-y',join(out,'flashes.gray')])
   await exec('ffmpeg',['-v','error','-ss',String(offset),'-i',path,'-t','15','-vn','-ac','1','-ar','48000','-f','f32le','-y',join(out,'audio.f32')])
   const {stdout:sync}=await exec('python3',[resolve('benchmarks/obs-cef/analyze-sync.py'),join(out,'flashes.gray'),join(out,'audio.f32')]);const details=JSON.parse(sync);await writeFile(join(out,'sync.json'),sync);windows.push({offset,...details})
   assert(details.matchedFlashes>=5,`${name} ${offset}: missing flash/tone matches`);assert(details.p95AbsoluteOffsetMs<200,`${name} ${offset}: A/V drift ${details.p95AbsoluteOffsetMs}ms`)
   await exec('ffmpeg',['-v','error','-ss',String(offset),'-i',path,'-frames:v','1','-y',join(out,'frame.png')])
  }
  result[name]={durationSeconds:Number(probe.format.duration),bytes:Number(probe.format.size),windows}
 }
 result.status='passed'
}catch(error){result.status='failed';result.error=String(error.stack??error)}
finally{
 clearInterval(timer);await broadcast?.stop().catch(()=>{});unsubscribeFrame?.();unsubscribeAudio?.();await source?.close();receiver.kill('SIGINT');await delay(500);sampler?.kill('SIGTERM');samples.end();server.closeAllConnections();server.close();loop.disable()
 result.afterCleanup=process.memoryUsage();globalThis.gc?.();await delay(100);globalThis.gc?.();result.afterCleanupGc=process.memoryUsage();result.nodeCpuMs=(process.cpuUsage(baselineCpu).user+process.cpuUsage(baselineCpu).system)/1000;result.elapsedMs=performance.now()-started;result.eventLoopDelay={meanMs:loop.mean/1e6,p99Ms:loop.percentile(99)/1e6,maxMs:loop.max/1e6};result.completedAt=new Date().toISOString();await writeFile(join(root,'receiver.log'),Buffer.concat(receiverLogs));await writeFile(join(root,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2))
}
