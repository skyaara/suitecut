import childProcess from 'node:child_process'
import {syncBuiltinESMExports} from 'node:module'
import {monitorEventLoopDelay, performance} from 'node:perf_hooks'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {createServer} from 'node:http'
import {createServer as socketServer} from 'node:net'
import {once} from 'node:events'
import {resolve,join} from 'node:path'
import {pathToFileURL} from 'node:url'
import {setTimeout as delay} from 'node:timers/promises'
import {promisify} from 'node:util'

const mode=process.env.PROFILE_MODE??'record',label=process.env.PROFILE_LABEL??`baseline-${mode}`
const root=resolve(process.env.PROFILE_OUTPUT??`benchmarks/obs-cef/2026-10-09-optimization/profiles/${label}`)
const media=resolve(`.suitecut/obs-cef-optimization/${label}`)
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const width=Number(process.env.PROFILE_WIDTH??1280),height=Number(process.env.PROFILE_HEIGHT??720),fps=Number(process.env.PROFILE_FPS??60),duration=Number(process.env.PROFILE_DURATION_MS??10000)
const children=[],spawn=childProcess.spawn
childProcess.spawn=function(command,args,options){
 if(args?.includes('rawvideo')&&process.env.PROFILE_FFMPEG_THREADS){args=[...args];args.splice(args.indexOf('-c:v'),0,'-threads',process.env.PROFILE_FFMPEG_THREADS,'-filter_threads','1')}
 if(args?.includes('rawvideo')&&process.env.PROFILE_X264_FRAME_THREADS){args=[...args];args.splice(args.indexOf('-c:v'),0,'-x264-params','sliced-threads=0')}
 if(args?.includes('libvpx-vp9')&&process.env.PROFILE_H264){args=[...args];for(const flag of ['-deadline','-cpu-used','-row-mt','-b:v']){const at=args.indexOf(flag);if(at>=0)args.splice(at,2)}args[args.indexOf('libvpx-vp9')]='libx264';args.splice(args.indexOf('-c:v'),0,'-preset','veryfast','-filter_threads','1','-f','matroska')}
 const child=spawn.call(this,command,args,options)
 const raw=args?.includes('rawvideo')||(args?.includes('matroska')&&args?.includes('pipe:0')),cef=String(command).includes('suitecut-browser')
 const row={pid:child.pid,role:raw?'encoder':cef?'cef':String(command).endsWith('ffmpeg')?'ffmpeg':'sampler',inputFrames:0,inputBytes:0,sameBufferWrites:0,blockedWrites:0,drainMs:0,cefBytes:0,cefChunks:0}
 children.push(row)
 if(raw&&child.stdin){let previous,pending;const write=child.stdin.write.bind(child.stdin)
  child.stdin.write=function(data,...rest){if(data.length===width*height*1.5){row.inputFrames++;if(data===previous)row.sameBufferWrites++;previous=data}row.inputBytes+=data.length;const accepted=write(data,...rest);if(!accepted){row.blockedWrites++;pending=performance.now()}return accepted}
  child.stdin.on('drain',()=>{if(pending!==undefined){row.drainMs+=performance.now()-pending;pending=undefined}})
 }
 if(cef)child.stdout?.on('data',data=>{row.cefBytes+=data.length;row.cefChunks++})
 return child
}
syncBuiltinESMExports()
const dist=resolve(process.env.PROFILE_DIST??'dist')
const {launchNativeBrowser,createNativePage,createNativeBroadcast}=await import(pathToFileURL(join(dist,'native.js')))
const {createNativeVideoRecorder}=await import(pathToFileURL(join(dist,'native-recorder.js')))
const exec=promisify(childProcess.execFile)
const fixture=await readFile(new URL('./fixture.html',import.meta.url),'utf8')
const server=createServer((_req,res)=>res.end(fixture));server.listen(0,'127.0.0.1');await once(server,'listening')
const result={label,mode,width,height,fps,durationMs:duration,dist,startedAt:new Date().toISOString(),children}
let source,recorder,broadcast,receiver,sampler,timer;const metrics=[];const loop=monitorEventLoopDelay({resolution:10});loop.enable()
const utilization=performance.eventLoopUtilization(),cpu=process.cpuUsage(),origin=performance.now()
try{
 sampler=spawn('python3',[resolve('benchmarks/obs-cef/profile-processes.py'),join(root,'processes.jsonl'),String(process.pid)],{stdio:'ignore'})
 source=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width,height,framesPerSecond:fps,pixelFormat:'i420'});source.failure.catch(()=>{})
 const page=createNativePage(source);await page.goto(`http://127.0.0.1:${server.address().port}`);await page.evaluate(()=>window.activate('canvas'));await delay(1000)
 let received=0;const count=source.onFrame(()=>received++)
 const started=performance.now();let unsubscribe
 timer=setInterval(()=>metrics.push({atMs:performance.now()-started,...process.memoryUsage(),eventLoop:performance.eventLoopUtilization(utilization),cpu:process.cpuUsage(cpu)}),1000)
 if(mode==='record'){
  recorder=createNativeVideoRecorder({source,ffmpegPath:'/usr/bin/ffmpeg',outputPath:join(media,'recording.webm'),size:{width,height}});recorder.failure.catch(()=>{});unsubscribe=source.onFrame(frame=>recorder.push(frame.data,frame.timestampMs))
 }else if(mode==='stream'){
  const probe=socketServer();probe.listen(0,'127.0.0.1');await once(probe,'listening');const port=probe.address().port;await new Promise(r=>probe.close(r));const url=`rtmp://127.0.0.1:${port}/live/profile`
  result.mediaPath=join(media,'receiver.flv');receiver=spawn('ffmpeg',['-v','error','-y','-listen','1','-i',url,'-c','copy',result.mediaPath],{stdio:['ignore','ignore','pipe']});const logs=[];receiver.stderr.on('data',x=>logs.push(x));receiver.once('close',()=>writeFile(join(root,'receiver.log'),Buffer.concat(logs)))
  await page.evaluate(()=>window.startAudio());broadcast=await createNativeBroadcast({source,ffmpegPath:'/usr/bin/ffmpeg',stream:{url,audio:true,bitrateKbps:4500,...(process.env.PROFILE_ADAPTIVE==='0'?{adaptiveBitrate:false,reconnect:false}:{}),onDiagnostic:event=>{void writeFile(join(root,'latest-stream-diagnostic.json'),JSON.stringify(event,null,2))}}});await broadcast.ready()
 }
 received=0;const active=performance.now()
 while(performance.now()-active<duration)await Promise.race([delay(250),source.failure,...(broadcast?[broadcast.failure]:[]),...(recorder?[recorder.failure]:[])])
 result.activeMs=performance.now()-active;result.received=received;result.captureFps=received/(result.activeMs/1000);count();unsubscribe?.()
 const stop=performance.now();if(recorder){await recorder.stop(stop);result.mediaPath=join(media,'recording.webm');recorder=undefined}if(broadcast){await broadcast.stop();broadcast=undefined;await delay(500)}result.finalizationMs=performance.now()-stop
 if(result.mediaPath){const {stdout}=await exec('ffprobe',['-v','error','-show_streams','-show_format','-of','json',result.mediaPath]);result.probe=JSON.parse(stdout)
  const {stdout:raw}=await exec('ffmpeg',['-v','error','-threads','2','-i',result.mediaPath,'-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:10*1024*1024});const ids=[];for(let n=0;n+20<=raw.length;n+=20){let id=0;for(let b=0;b<20;b++)if(raw[n+b]>128)id|=1<<b;ids.push(id)}let transitions=0,last;for(const id of ids){if(id!==last)transitions++;last=id}await writeFile(join(root,'marker-ids.json'),JSON.stringify(ids));result.decoded={frames:ids.length,transitions,distinctFps:transitions/Number(result.probe.format.duration)}
 }
}catch(e){result.error=String(e.stack??e)}
finally{
 clearInterval(timer);await broadcast?.stop().catch(()=>{});if(recorder)await recorder.stop(performance.now()).catch(()=>{});receiver?.kill('SIGINT');await source?.close();sampler?.kill('SIGTERM');server.closeAllConnections();server.close();loop.disable()
 result.nodeCpuMs=(process.cpuUsage(cpu).user+process.cpuUsage(cpu).system)/1000;result.elapsedMs=performance.now()-origin;result.nodeCpuPercent=result.nodeCpuMs/result.elapsedMs*100;result.eventLoopUtilization=performance.eventLoopUtilization(utilization);result.eventLoopDelay={meanMs:loop.mean/1e6,p99Ms:loop.percentile(99)/1e6,maxMs:loop.max/1e6};result.completedAt=new Date().toISOString()
 await writeFile(join(root,'node-metrics.json'),JSON.stringify(metrics,null,2));await writeFile(join(root,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result))
}
