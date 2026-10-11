import assert from 'node:assert/strict'
import {spawn,execFile} from 'node:child_process'
import {once} from 'node:events'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {createServer} from 'node:http'
import {resolve,join,dirname} from 'node:path'
import {performance} from 'node:perf_hooks'
import {setTimeout as delay} from 'node:timers/promises'
import {promisify} from 'node:util'
import {chromium} from 'playwright'
import {launchNativeBrowser,recordNative} from '../../dist/native.js'
import {ObsClient} from './obs-client.mjs'

const exec=promisify(execFile)
const root=resolve(process.env.BENCHMARK_OUTPUT??'benchmarks/obs-cef/2026-10-09')
const media=resolve(process.env.BENCHMARK_MEDIA??'.suitecut/obs-cef-evidence/2026-10-09')
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const fixture=await readFile(new URL('./fixture.html',import.meta.url),'utf8')
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html');if(req.url==='/missing'){res.writeHead(404);res.end('Missing');return}res.end(fixture)})
server.listen(0,'127.0.0.1');await once(server,'listening');const url=`http://127.0.0.1:${server.address().port}/fixture.html`
const obs=new ObsClient();await obs.connect()
const obsPid=Number(process.env.OBS_PID)
const xvfbPid=Number(process.env.XVFB_PID)
const results=process.env.BENCHMARK_RESUME==='1'?JSON.parse(await readFile(join(root,'trials.json'),'utf8')).filter(row=>!['running','interrupted'].includes(row.status)):[]
const cases=[
 {name:'720p60-static',width:1280,height:720,fps:60,workload:'static'},
 {name:'720p60-canvas',width:1280,height:720,fps:60,workload:'canvas'},
 {name:'720p60-scroll',width:1280,height:720,fps:60,workload:'scroll'},
 {name:'1080p30-canvas',width:1920,height:1080,fps:30,workload:'canvas'},
 {name:'1080p60-canvas',width:1920,height:1080,fps:60,workload:'canvas'},
 {name:'1080p60-scroll',width:1920,height:1080,fps:60,workload:'scroll'},
 {name:'4k30-canvas',width:3840,height:2160,fps:30,workload:'canvas'},
 {name:'4k60-canvas',width:3840,height:2160,fps:60,workload:'canvas'},
 {name:'4k60-scroll',width:3840,height:2160,fps:60,workload:'scroll'},
]
async function persist(){await writeFile(join(root,'trials.json'),JSON.stringify(results,null,2)+'\n')}
function errorDetails(error,depth=0){if(depth>5)return {message:'Nested error depth limit'};if(!error||typeof error!=='object')return {message:String(error)};return {name:error.name,message:error.message,stack:error.stack,...(error.cause?{cause:errorDetails(error.cause,depth+1)}:{}),...(Array.isArray(error.errors)?{errors:error.errors.map(e=>errorDetails(e,depth+1))}:{})}}
async function setProfile(category,name,value){await obs.request('SetProfileParameter',{parameterCategory:category,parameterName:name,parameterValue:String(value)})}
async function prepareObs(c){
 await obs.request('SetVideoSettings',{baseWidth:c.width,baseHeight:c.height,outputWidth:c.width,outputHeight:c.height,fpsNumerator:c.fps,fpsDenominator:1})
 await obs.request('SetRecordDirectory',{recordDirectory:media})
 for(const [name,value] of Object.entries({RecType:'Standard',RecFormat2:'mkv',RecEncoder:'obs_x264',RecTracks:1,RecRescale:false}))await setProfile('AdvOut',name,value)
 const inputs=await obs.request('GetInputList')
 if(!inputs.inputs.some(x=>x.inputName==='Benchmark display'))await obs.request('CreateInput',{sceneName:'Scene',inputName:'Benchmark display',inputKind:'xshm_input',inputSettings:{screen:0,show_cursor:false},sceneItemEnabled:true})
 await obs.request('SetInputSettings',{inputName:'Benchmark display',inputSettings:{screen:0,show_cursor:false,advanced:true,cut_left:0,cut_top:87,cut_right:3840-c.width,cut_bot:2300-87-c.height},overlay:true})
 const {sceneItemId}=await obs.request('GetSceneItemId',{sceneName:'Scene',sourceName:'Benchmark display'})
 await obs.request('SetSceneItemEnabled',{sceneName:'Scene',sceneItemId,sceneItemEnabled:true})
 await obs.request('SetSceneItemTransform',{sceneName:'Scene',sceneItemId,sceneItemTransform:{positionX:0,positionY:0,scaleX:1,scaleY:1}})
}
async function openBrowser(c){
 const started=performance.now()
 const browser=await chromium.launch({headless:false,chromiumSandbox:true,args:['--kiosk','--window-position=0,0','--window-size=3840,2160','--autoplay-policy=no-user-gesture-required','--disable-background-timer-throttling','--disable-renderer-backgrounding']})
 const context=await browser.newContext({viewport:{width:c.width,height:c.height},deviceScaleFactor:1})
 const page=await context.newPage();await page.goto(url);await page.waitForTimeout(1000)
 return {browser,page,startupMs:performance.now()-started,browserVersion:browser.version()}
}
async function sampleStart(directory,backend){
 const roots=[process.pid,xvfbPid,...(backend==='obs'?[obsPid]:[])].filter(v=>v>0)
 const child=spawn('python3',[resolve('benchmarks/obs-cef/sample-processes.py'),join(directory,'resources.jsonl'),...roots.map(String)],{stdio:'ignore'})
 return child
}
async function workload(page,c,durationMs){
 await page.evaluate(mode=>window.activate(mode),c.workload)
 const start=performance.now();let ticks=0
 while(performance.now()-start<durationMs){
  if(c.workload==='scroll'){
   const y=(ticks%50<25?1:-1)*120
   if(page.mouse)await page.mouse.wheel(0,y)
   else await page.source.sendDevToolsCommand('Input.dispatchMouseEvent',{type:'mouseWheel',x:600,y:300,deltaX:0,deltaY:y})
   ticks++
  }
  await delay(c.workload==='scroll'?50:200)
 }
 return {wallMs:performance.now()-start,scrollCommands:ticks,snapshot:await page.evaluate(()=>window.snapshot())}
}
async function decode(file,directory){
 const {stdout}=await exec('ffprobe',['-v','error','-show_streams','-show_format','-of','json',file],{maxBuffer:4*1024*1024})
 const probe=JSON.parse(stdout);await writeFile(join(directory,'probe.json'),stdout)
 // Decode only the fixed binary marker. A 20-bit frame id occupies 320 x 32.
 const out=await exec('ffmpeg',['-v','error','-threads','2','-i',file,'-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:64*1024*1024})
 const raw=out.stdout,ids=[]
 for(let n=0;n+20<=raw.length;n+=20){let id=0;for(let bit=0;bit<20;bit++)if(raw[n+bit]>128)id|=1<<bit;ids.push(id)}
 await writeFile(join(directory,'marker-ids.json'),JSON.stringify(ids)+'\n')
 const video=probe.streams.find(s=>s.codec_type==='video');const fps=video.avg_frame_rate.split('/').map(Number);const rate=fps[0]/fps[1]
 let transitions=0,run=0,maxRun=0,previous=-1,regressions=0
 for(const id of ids){if(id!==previous){transitions++;run=1;if(previous>0&&id>0&&id<previous)regressions++}else run++;maxRun=Math.max(maxRun,run);previous=id}
 const {stdout:frameProbe}=await exec('ffprobe',['-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time,pkt_duration_time','-of','json',file],{maxBuffer:64*1024*1024})
 await writeFile(join(directory,'frame-timestamps.json'),frameProbe)
 const pts=JSON.parse(frameProbe).frames.map(f=>Number(f.best_effort_timestamp_time))
 assert.equal(pts.length,ids.length,'Frame timestamps must align with decoded markers')
 assert(pts.every(Number.isFinite),'Frame timestamps must be finite')
 assert(pts.every((p,i)=>i===0||p>=pts[i-1]),'Frame timestamps must be monotonic')
 const positive=ids.filter(n=>n>0);const activeFrames=positive.length
 let activeTransitions=0,activeLast=-1,activeMaxSeconds=0,holdStart,activeStart
 const end=Number(probe.format.duration)
 for(let i=0;i<ids.length;i++){
  if(ids[i]<=0)continue
  activeStart??=pts[i]
  if(ids[i]!==activeLast){if(holdStart!==undefined)activeMaxSeconds=Math.max(activeMaxSeconds,pts[i]-holdStart);holdStart=pts[i];activeTransitions++}
  activeLast=ids[i]
 }
 if(holdStart!==undefined)activeMaxSeconds=Math.max(activeMaxSeconds,end-holdStart)
 const activeSeconds=activeStart===undefined?0:end-activeStart
 await exec('ffmpeg',['-v','error','-i',file,'-ss','2','-frames:v','1','-y',join(directory,'frame.png')])
 return {width:video.width,height:video.height,codec:video.codec_name,containerFps:rate,durationSeconds:Number(probe.format.duration),bytes:Number(probe.format.size),decodedFrames:ids.length,markerTransitions:transitions,markerRegressions:regressions,activeFrames,distinctActiveFrames:activeTransitions,activeDurationSeconds:activeSeconds,measurementMethod:"decoded marker transitions / timestamped active media duration",distinctActiveFps:activeSeconds>0?activeTransitions/activeSeconds:null,repeatFraction:activeFrames?1-activeTransitions/activeFrames:null,longestActiveHoldMs:activeFrames?activeMaxSeconds*1000:null,audioStreams:probe.streams.filter(s=>s.codec_type==='audio').map(s=>({codec:s.codec_name,sampleRate:s.sample_rate,channels:s.channels}))}
}
async function trial(backend,c,trialNumber,durationMs){
 const id=`${c.name}-${backend}-${trialNumber}`;const directory=join(root,'trials',id);await mkdir(directory,{recursive:true})
 if(results.some(row=>row.id===id))return
 const r={id,backend,case:c,trialNumber,durationMs,status:'running',startedAt:new Date().toISOString()}
 results.push(r);await persist();await writeFile(join(directory,'settings.json'),JSON.stringify(r,null,2)+'\n')
 let browser,source,sampler,recording=false
 try{
  if(backend==='obs'){
   await prepareObs(c);const opened=await openBrowser(c);browser=opened.browser;const page=opened.page
   r.startupMs=opened.startupMs;r.browserVersion=opened.browserVersion
   await obs.request('SaveSourceScreenshot',{sourceName:'Benchmark display',imageFormat:'png',imageFilePath:join(directory,'before.png')})
   r.obsBefore=await obs.request('GetStats');sampler=await sampleStart(directory,backend)
   const start=performance.now();await obs.request('StartRecord');recording=true;r.recordStartMs=performance.now()-start
   await delay(500);r.workload=await workload(page,c,durationMs);r.obsAfter=await obs.request('GetStats')
   const stop=performance.now();const stopped=await obs.request('StopRecord');recording=false
   for(let i=0;i<120;i++){const status=await obs.request('GetRecordStatus');if(!status.outputActive)break;await delay(250)}
   // StopRecord acknowledges the stop request before the muxer finishes.
   let valid=false
   for(let i=0;i<120;i++){try{await exec('ffprobe',['-v','error','-show_format',stopped.outputPath]);valid=true;break}catch{await delay(250)}}
   if(!valid)throw new Error('OBS muxer did not finalize a readable file within 30 seconds')
   r.finalizationMs=performance.now()-stop
   r.mediaPath=stopped.outputPath
   await browser.close();browser=undefined
  }else{
   const {sceneItemId}=await obs.request('GetSceneItemId',{sceneName:'Scene',sourceName:'Benchmark display'})
   await obs.request('SetSceneItemEnabled',{sceneName:'Scene',sceneItemId,sceneItemEnabled:false})
   const start=performance.now();source=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:c.width,height:c.height,framesPerSecond:c.fps,pixelFormat:'i420'})
   source.failure.catch(()=>{});await source.navigate(url);await delay(1000);r.startupMs=performance.now()-start;r.browserVersion=source.cefVersion
   sampler=await sampleStart(directory,backend)
   let received=0,lastDropped=0;source.onFrame(frame=>{received++;lastDropped=frame.droppedFrames})
   let end;const rec=await recordNative(id,async({page})=>{await delay(500);const before=received;const drops=lastDropped;r.workload=await workload(page,c,durationMs);r.nativeCapture={frames:received-before,fps:(received-before)/(r.workload.wallMs/1000),nativeDrops:lastDropped-drops};end=performance.now()},{source,capture:{audio:false},output:{directory:join(media,id),manifestPath:join(directory,'manifest.json')}})
   r.finalizationMs=performance.now()-end
   const artifact=rec.manifest.tests[0].attempts[0].artifacts.find(a=>a.role==='source-video')
   r.mediaPath=resolve(dirname(rec.manifestPath),artifact.path)
   await source.close();source=undefined
  }
  sampler?.kill('SIGTERM');sampler=undefined
  r.decoded=await decode(r.mediaPath,directory)
  if(r.decoded.width!==c.width||r.decoded.height!==c.height)throw new Error('Encoded dimensions do not match requested dimensions')
  if(c.workload!=='static'&&r.decoded.distinctActiveFrames<3)throw new Error('Decoded output does not contain the active motion marker')
  if(c.workload==='scroll'&&r.workload.snapshot.scrollEvents<2)throw new Error('Wheel commands did not cause real page scrolling')
  const samples=(await readFile(join(directory,'resources.jsonl'),'utf8')).trim().split('\n').filter(Boolean).map(JSON.parse)
  r.resources={sampleCount:samples.length,meanCpuPercent:samples.slice(1).reduce((s,x)=>s+x.cpuPercent,0)/Math.max(1,samples.length-1),peakRssBytes:Math.max(...samples.map(x=>x.rssBytes)),firstRssBytes:samples[0]?.rssBytes,lastRssBytes:samples.at(-1)?.rssBytes}
  r.status='passed'
 }catch(e){r.status='failed';r.error=String(e.stack??e);r.errorDetails=errorDetails(e)}
 finally{sampler?.kill('SIGTERM');if(recording)await obs.request('StopRecord').catch(()=>{});await browser?.close().catch(()=>{});await source?.close().catch(()=>{});r.completedAt=new Date().toISOString();await writeFile(join(directory,'result.json'),JSON.stringify(r,null,2)+'\n');await persist();console.log(JSON.stringify({id,status:r.status,fps:r.decoded?.distinctActiveFps,error:r.error}))}
}
try{
 const trials=Number(process.env.BENCHMARK_TRIALS??5),duration=Number(process.env.BENCHMARK_DURATION_MS??10000)
 const selected=cases.filter(c=>!process.env.BENCHMARK_CASE||process.env.BENCHMARK_CASE.split(',').includes(c.name))
 const backends=(process.env.BENCHMARK_BACKENDS??'obs,native').split(',')
 for(const c of selected){for(let t=0;t<trials;t++){for(const backend of t%2?[...backends].reverse():backends)await trial(backend,c,t+1,duration)}}
}finally{await persist();obs.close();server.closeAllConnections();await new Promise(r=>server.close(r))}
