import assert from 'node:assert/strict'
import {execFile,spawn} from 'node:child_process'
import {once} from 'node:events'
import {mkdir,readFile,writeFile} from 'node:fs/promises'
import {createServer} from 'node:http'
import {createServer as socketServer} from 'node:net'
import {resolve,join} from 'node:path'
import {performance} from 'node:perf_hooks'
import {setTimeout as delay} from 'node:timers/promises'
import {promisify} from 'node:util'
import {chromium} from 'playwright'
import {launchNativeBrowser,createNativePage,recordNative,createNativeBroadcast} from '../../dist/native.js'
import {renderSuiteCut} from '../../dist/render.js'
import {ObsClient} from './obs-client.mjs'

const exec=promisify(execFile),root=resolve(process.env.FEATURE_OUTPUT??'benchmarks/obs-cef/2026-10-09/features'),media=resolve(process.env.FEATURE_MEDIA??'.suitecut/obs-cef-evidence/2026-10-09/features')
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const fixture=await readFile(new URL('./fixture.html',import.meta.url),'utf8')
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html');if(req.url==='/missing'){res.writeHead(404);res.end('Missing')}else res.end(fixture)})
server.listen(0,'127.0.0.1');await once(server,'listening');const base=`http://127.0.0.1:${server.address().port}`
const obs=new ObsClient();await obs.connect();const rows=[]
function errorDetails(e,depth=0){if(depth>5)return {message:'Nested error depth limit'};if(!e||typeof e!=='object')return {message:String(e)};return {name:e.name,message:e.message,stack:e.stack,...(e.cause?{cause:errorDetails(e.cause,depth+1)}:{}),...(Array.isArray(e.errors)?{errors:e.errors.map(x=>errorDetails(x,depth+1))}:{})}}
let recording=false,broadcast,receiver,browser;const sources=[]
async function save(){await writeFile(join(root,'results.json'),JSON.stringify(rows,null,2)+'\n')}
async function check(row,name,fn){const start=performance.now();try{const details=await fn();row.checks.push({name,status:'passed',durationMs:performance.now()-start,details})}catch(e){row.checks.push({name,status:'failed',durationMs:performance.now()-start,error:String(e.stack??e),errorDetails:errorDetails(e)})}await save()}
async function setupObs(width,height,fps,audio){
 await obs.request('SetVideoSettings',{baseWidth:width,baseHeight:height,outputWidth:width,outputHeight:height,fpsNumerator:fps,fpsDenominator:1})
 const {sceneItemId}=await obs.request('GetSceneItemId',{sceneName:'Scene',sourceName:'Benchmark display'})
 await obs.request('SetSceneItemEnabled',{sceneName:'Scene',sceneItemId,sceneItemEnabled:true})
 await obs.request('SetSceneItemTransform',{sceneName:'Scene',sceneItemId,sceneItemTransform:{positionX:0,positionY:0,scaleX:1,scaleY:1}})
 await obs.request('SetInputSettings',{inputName:'Benchmark display',inputSettings:{advanced:true,cut_top:87,cut_bot:2300-87-height,cut_left:0,cut_right:3840-width,show_cursor:false},overlay:true})
 const {inputs}=await obs.request('GetInputList')
 if(!inputs.some(i=>i.inputName==='Benchmark audio'))await obs.request('CreateInput',{sceneName:'Scene',inputName:'Benchmark audio',inputKind:'pulse_output_capture',inputSettings:{device_id:'benchmark.monitor'},sceneItemEnabled:true})
 await obs.request('SetInputMute',{inputName:'Benchmark audio',inputMuted:!audio})
 await obs.request('SetRecordDirectory',{recordDirectory:media})
}
async function launch(backend,width,height,fps){
 if(backend==='native'){
  const {sceneItemId}=await obs.request('GetSceneItemId',{sceneName:'Scene',sourceName:'Benchmark display'});await obs.request('SetSceneItemEnabled',{sceneName:'Scene',sceneItemId,sceneItemEnabled:false})
  const source=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width,height,framesPerSecond:fps,pixelFormat:'i420'});source.failure.catch(()=>{});sources.push(source);return {source,page:createNativePage(source)}
 }
 browser=await chromium.launch({headless:false,chromiumSandbox:true,args:['--window-position=0,0','--window-size=3840,2300','--autoplay-policy=no-user-gesture-required']})
 const context=await browser.newContext({viewport:{width,height},deviceScaleFactor:1});const page=await context.newPage();return {page,context}
}
async function shot(backend,page,path){
 await delay(350)
 if(backend==='obs'){await obs.request('SaveSourceScreenshot',{sourceName:'Benchmark display',imageFormat:'png',imageFilePath:path});return}
 const frame=await new Promise(r=>{let off;off=page.source.onFrame(f=>{setTimeout(()=>{off?.();r(f)},0)})})
 const converted=await new Promise((resolve,reject)=>{const p=spawn('ffmpeg',['-v','error','-f','rawvideo','-pixel_format','yuv420p','-video_size',`${frame.width}x${frame.height}`,'-i','pipe:0','-frames:v','1','-f','image2pipe','-vcodec','png','pipe:1']);const chunks=[];p.stdout.on('data',c=>chunks.push(c));p.stderr.resume();p.on('close',code=>code===0?resolve(Buffer.concat(chunks)):reject(new Error('Frame conversion failed')));p.stdin.end(frame.data)})
 await writeFile(path,converted)
}
async function finalizedObs(){const stopped=await obs.request('StopRecord');recording=false;for(let i=0;i<120;i++){const s=await obs.request('GetRecordStatus');if(!s.outputActive){try{await exec('ffprobe',['-v','error','-show_format',stopped.outputPath]);return stopped.outputPath}catch{}}await delay(250)}throw new Error('OBS finalization timeout')}
async function audioEvidence(file,path){
 const probe=JSON.parse((await exec('ffprobe',['-v','error','-show_streams','-of','json',file])).stdout)
 if(!probe.streams.some(s=>s.codec_type==='audio'))return {audioStream:false,rms:0,peak:0,tone440:0,tone880:0}
 const {stdout}=await exec('ffmpeg',['-v','error','-i',file,'-vn','-ac','1','-af','aresample=48000:first_pts=0','-f','f32le','pipe:1'],{encoding:'buffer',maxBuffer:128*1024*1024})
 await writeFile(path,stdout)
 const {stdout:metrics}=await exec('python3',[resolve('benchmarks/obs-cef/analyze-audio.py'),path])
 return JSON.parse(metrics)
}
async function pageAudio(page,on){if(on)return page.evaluate(()=>window.startAudio())}
async function stopPageAudio(page){await page.goto('about:blank')}
async function functionalityMarker(page){await page.evaluate(()=>{
 const canvas=document.createElement('canvas');canvas.width=64;canvas.height=32;Object.assign(canvas.style,{position:'fixed',left:'480px',top:'0px',zIndex:1000});document.body.append(canvas);const ctx=canvas.getContext('2d')
 function draw(){const state=(document.querySelector('#entry').value==='SuiteCut OBS input'?1:0)|(window.clicks>0?2:0)|(document.querySelector('#choice').value==='Second'?4:0)|(window.scrollEvents>0?8:0);for(let bit=0;bit<4;bit++){ctx.fillStyle=state&(1<<bit)?'white':'black';ctx.fillRect(bit*16,0,16,32)}requestAnimationFrame(draw)}draw()
})}
async function pulse(page){await page.evaluate(async()=>{
 const canvas=document.createElement('canvas');canvas.width=64;canvas.height=64;Object.assign(canvas.style,{position:'fixed',left:'400px',top:'0px',zIndex:1000});document.body.append(canvas);const ctx=canvas.getContext('2d')
 const audio=new AudioContext({sampleRate:48000});await audio.resume();const osc=audio.createOscillator(),gain=audio.createGain();osc.frequency.value=440;gain.gain.value=0;osc.connect(gain).connect(audio.destination);osc.start();let next=Math.ceil(audio.currentTime)+1
 setInterval(()=>{while(next<audio.currentTime+2){gain.gain.setValueAtTime(.15,next);gain.gain.setValueAtTime(0,next+.12);next++}},50)
 function draw(){ctx.fillStyle=audio.currentTime%1<.12?'white':'black';ctx.fillRect(0,0,64,64);requestAnimationFrame(draw)}draw()
})}
async function recordFeatures(backend,audio){
 const id=`${backend}-record-audio-${audio}`,directory=join(root,id);await mkdir(directory,{recursive:true})
 const row={id,backend,audio,checks:[],startedAt:new Date().toISOString()};rows.push(row);await save()
 const opened=await launch(backend,1280,720,30);let primary=opened.page,secondary
 async function flow(page,suitecut,addSource){
  await page.goto(`${base}/fixture.html`);await functionalityMarker(page);await delay(500);if(audio)await pageAudio(page,true)
  await check(row,'typing',async()=>{if(suitecut)await suitecut.type(page.locator('#entry'),'SuiteCut OBS input',{delayMs:25});else await page.locator('#entry').pressSequentially('SuiteCut OBS input',{delay:25});const s=await page.evaluate(()=>window.snapshot());assert.equal(s.value,'SuiteCut OBS input');assert(s.keyEvents>0);return s})
  await check(row,'click',async()=>{if(suitecut)await suitecut.click(page.locator('#click'),{moveDurationMs:100});else await page.locator('#click').click();const s=await page.evaluate(()=>window.snapshot());assert.equal(s.clicks,1);await shot(backend,page,join(directory,'input-click.png'));return s})
  await check(row,'in-page select control',async()=>{await page.locator('#choice').click();if(backend==='native'){for(const [key,code,n] of [['ArrowDown','ArrowDown',40],['Enter','Enter',13]])for(const type of ['keyDown','keyUp'])await page.source.sendDevToolsCommand('Input.dispatchKeyEvent',{type,key,code,windowsVirtualKeyCode:n})}else{await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter')}await delay(300);const state=await page.evaluate(()=>window.snapshot());assert.equal(state.choice,'Second');await shot(backend,page,join(directory,'select-control.png'));return state})
  await check(row,'wheel scrolling',async()=>{for(let i=0;i<10;i++){if(backend==='native')await page.source.sendDevToolsCommand('Input.dispatchMouseEvent',{type:'mouseWheel',x:600,y:400,deltaX:0,deltaY:180});else await page.mouse.wheel(0,180);await delay(60)}await delay(300);const s=await page.evaluate(()=>window.snapshot());assert(s.scrollY>1000&&s.scrollEvents>0);await shot(backend,page,join(directory,'scroll.png'));return s})
  await check(row,'scroll to element and back to top',async()=>{if(suitecut){await suitecut.scrollTo(page.locator('#bottom'),{behavior:'auto',settleMs:0});assert(await page.evaluate(()=>scrollY>8000));await suitecut.scrollTop({behavior:'auto',settleMs:0})}else{await page.locator('#bottom').scrollIntoViewIfNeeded();assert(await page.evaluate(()=>scrollY>8000));await page.evaluate(()=>scrollTo(0,0))}await delay(300);assert.equal(await page.evaluate(()=>scrollY),0)})
  await check(row,'navigation and return',async()=>{await page.goto(`${base}/fixture.html?source=secondary`);assert(await page.evaluate(()=>window.snapshot().secondary));await page.goto(`${base}/fixture.html`);assert.equal(await page.evaluate(()=>window.snapshot().secondary),false);if(audio)await pageAudio(page,true);await delay(1500)})
  await check(row,'source switching',async()=>{
   if(backend==='native'){
    const second=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:1280,height:720,framesPerSecond:30,pixelFormat:'i420'});second.failure.catch(()=>{});sources.push(second);await second.navigate(`${base}/fixture.html?source=secondary`);secondary=await addSource(second);await secondary.evaluate(()=>{document.querySelector('#identity').style.background='#304fe0'});if(audio)await pageAudio(secondary,true);suitecut.selectPage(secondary)
   }else{await stopPageAudio(page);secondary=await opened.context.newPage();await secondary.goto(`${base}/fixture.html?source=secondary`);await secondary.evaluate(()=>{document.querySelector('#identity').style.background='#304fe0'});await secondary.bringToFront();if(audio)await pageAudio(secondary,true)}
   await delay(1800);assert(await secondary.evaluate(()=>window.snapshot().secondary));await shot(backend,secondary,join(directory,'secondary.png'))
   if(backend==='native')suitecut.selectPage(page);else{await stopPageAudio(secondary);await page.goto(`${base}/fixture.html`);await page.bringToFront();if(audio)await pageAudio(page,true)}
   await delay(1500);await shot(backend,page,join(directory,'primary-return.png'))
  })
 }
 try{
  if(backend==='obs'){await setupObs(1280,720,30,audio);await obs.request('StartRecord');recording=true;await flow(primary);row.mediaPath=await finalizedObs()}
  else{
   const rec=await recordNative(id,async({page,suitecut,addSource})=>flow(page,suitecut,addSource),{source:opened.source,capture:{audio},output:{directory:join(media,id),manifestPath:join(directory,'manifest.json')}})
   row.mediaPath=join(media,id+'.mp4');const started=performance.now();await renderSuiteCut({manifestPath:rec.manifestPath,outputPath:row.mediaPath,config:{output:{width:1280,height:720,framesPerSecond:30,quality:'high'}}});row.renderMs=performance.now()-started
  }
  await check(row,'encoded audio '+(audio?'tones':'disabled'),async()=>{const metric=await audioEvidence(row.mediaPath,join(directory,'audio.f32'));if(audio)assert(metric.tone440>.001&&metric.tone880>.001);else assert(metric.rms<.0001);return metric})
  await check(row,'decoded source identity',async()=>{
   const {stdout}=await exec('ffmpeg',['-v','error','-i',row.mediaPath,'-an','-vf','crop=64:32:320:0,scale=1:1,format=rgb24','-vsync','0','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:1024*1024});let red=0,green=0,blue=0,changes=[];let previous
   for(let i=0;i<stdout.length;i+=3){const identity=stdout[i+2]>stdout[i]*1.5&&stdout[i+2]>stdout[i+1]*1.5?'second-source':stdout[i]>stdout[i+1]*1.5?'primary':stdout[i+1]>stdout[i]*1.5?'navigation-secondary':'other';if(identity==='primary')red++;if(identity==='navigation-secondary')green++;if(identity==='second-source')blue++;if(identity!==previous){changes.push({frame:i/3,identity});previous=identity}}
   await writeFile(join(directory,'identity.json'),JSON.stringify({red,green,blue,changes},null,2));assert(red>10&&blue>10,'Decoded recording did not show both source identities');const index=changes.findIndex(x=>x.identity==='second-source');assert(changes.slice(0,index).some(x=>x.identity==='primary')&&changes.slice(index+1).some(x=>x.identity==='primary'),'Decoded source selection did not follow primary → second source → primary');return {red,green,blue,changes}
  })
  await check(row,'recorded input and scrolling states',async()=>{
   const {stdout}=await exec('ffmpeg',['-v','error','-i',row.mediaPath,'-an','-vf','crop=64:32:480:0,scale=4:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:1024*1024});const states=[];let observed=0
   for(let n=0;n+4<=stdout.length;n+=4){let state=0;for(let bit=0;bit<4;bit++)if(stdout[n+bit]>128)state|=1<<bit;states.push(state);observed|=state}
   const evidence={observedMask:observed,typedComplete:Boolean(observed&1),clicked:Boolean(observed&2),selected:Boolean(observed&4),scrolled:Boolean(observed&8)};await writeFile(join(directory,'recorded-states.json'),JSON.stringify({evidence,states},null,2)+'\n');assert.equal(observed,15,`Encoded action states missing: ${JSON.stringify(evidence)}`);return evidence
  })
 }catch(e){row.error=String(e.stack??e);row.errorDetails=errorDetails(e)}
 finally{if(recording)await finalizedObs().catch(()=>{});await browser?.close();browser=undefined;await Promise.all(sources.splice(0).map(s=>s.close()));row.completedAt=new Date().toISOString();await save();console.log(JSON.stringify(row))}
}
async function stream(backend,durationMs){
 const id=`${backend}-stream-${durationMs}`,directory=join(root,id);await mkdir(directory,{recursive:true})
 const row={id,backend,durationMs,checks:[],startedAt:new Date().toISOString()};rows.push(row);await save()
 const probe=socketServer();probe.listen(0,'127.0.0.1');await once(probe,'listening');const port=probe.address().port;await new Promise(r=>probe.close(r))
 const rtmp=`rtmp://127.0.0.1:${port}/live/benchmark`,file=join(media,id+'.flv')
 row.mediaPath=file
 const receive=spawn('ffmpeg',['-hide_banner','-loglevel','error','-y','-listen','1','-i',rtmp,'-c','copy',file],{stdio:['ignore','ignore','pipe']});receiver=receive;const logs=[];receive.stderr.on('data',d=>logs.push(d.toString()));const closed=once(receive,'close');closed.catch(()=>{})
 let sampler;const samples=[]
 try{
  const opened=await launch(backend,1920,1080,60);await opened.page.goto(`${base}/fixture.html`);await opened.page.evaluate(()=>window.activate('canvas'));await pulse(opened.page)
  if(backend==='obs')await setupObs(1920,1080,60,true)
  await delay(500)
  sampler=spawn('python3',[resolve('benchmarks/obs-cef/sample-processes.py'),join(directory,'resources.jsonl'),String(process.pid),...[process.env.XVFB_PID,...(backend==='obs'?[process.env.OBS_PID]:[])].filter(v=>Number(v)>0).map(String)],{stdio:'ignore'})
  const start=performance.now()
  if(backend==='native'){broadcast=await createNativeBroadcast({source:opened.source,ffmpegPath:'/usr/bin/ffmpeg',stream:{url:rtmp,audio:true,bitrateKbps:4500}});await broadcast.ready()}
  else{
   for(const [name,value] of Object.entries({Encoder:'obs_x264',TrackIndex:1,ApplyServiceSettings:false}))await obs.request('SetProfileParameter',{parameterCategory:'AdvOut',parameterName:name,parameterValue:String(value)})
   await obs.request('SetStreamServiceSettings',{streamServiceType:'rtmp_custom',streamServiceSettings:{server:`rtmp://127.0.0.1:${port}/live`,key:'benchmark',use_auth:false}})
   await obs.request('StartStream');for(let i=0;i<120;i++){const s=await obs.request('GetStreamStatus');if(s.outputActive)break;await delay(250)}
  }
  row.publisherReadyMs=performance.now()-start
  const runStart=performance.now()
  while(performance.now()-runStart<durationMs){if(backend==='native')await Promise.race([delay(1000),opened.source.failure,broadcast.failure]);else await delay(1000);if(backend==='obs'){const status=await obs.request('GetStreamStatus');samples.push({atMs:performance.now()-runStart,...status,stats:await obs.request('GetStats')});if(!status.outputActive&&!status.outputReconnecting)throw new Error('OBS streaming stopped unexpectedly')}else samples.push({atMs:performance.now()-runStart,state:opened.source.state})}
  await writeFile(join(directory,'stream-status.json'),JSON.stringify(samples,null,2)+'\n')
  row.wallMs=performance.now()-runStart;row.snapshot=await opened.page.evaluate(()=>window.snapshot())
  const stopped=performance.now();if(broadcast){await broadcast.stop();broadcast=undefined}else{await obs.request('StopStream');for(let i=0;i<120;i++){if(!(await obs.request('GetStreamStatus')).outputActive)break;await delay(250)}}
  await Promise.race([closed,delay(10000).then(()=>receive.kill('SIGINT'))]);row.stopMs=performance.now()-stopped;sampler.kill('SIGTERM');sampler=undefined
  row.mediaPath=file
  await check(row,'decoded receiver motion',async()=>{
   const {stdout}=await exec('ffmpeg',['-v','error','-threads','2','-i',file,'-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:4*1024*1024});const ids=[]
   for(let n=0;n+20<=stdout.length;n+=20){let id=0;for(let b=0;b<20;b++)if(stdout[n+b]>128)id|=1<<b;ids.push(id)}
   let unique=0,maxRun=0,run=0,last=-1;for(const id of ids){if(id!==last){unique++;run=1}else run++;last=id;maxRun=Math.max(run,maxRun)}
   await writeFile(join(directory,'marker-ids.json'),JSON.stringify(ids)+'\n');assert(unique>10);const distinctFps=unique/(row.wallMs/1000);return {decodedFrames:ids.length,markerTransitions:unique,distinctFps,targetMotionStatus:distinctFps>=54?'within 10% of 60 fps':'below target',repeatFraction:1-unique/ids.length,longestHoldMs:maxRun/(ids.length/(row.wallMs/1000))*1000}
  })
  await check(row,'receiver media and timestamps',async()=>{
   const {stdout}=await exec('ffprobe',['-v','error','-show_streams','-show_format','-show_entries','packet=pts_time,dts_time,stream_index','-show_packets','-of','json',file],{maxBuffer:32*1024*1024});await writeFile(join(directory,'probe.json'),stdout);const p=JSON.parse(stdout);const v=p.streams.find(s=>s.codec_type==='video');assert.equal(v.width,1920);assert.equal(v.height,1080);assert(p.streams.some(s=>s.codec_type==='audio'));assert(+p.format.duration>=durationMs/1000*.95,'Receiver media duration is shorter than 95% of the requested run');const packets=p.packets.filter(x=>x.stream_index===v.index);const times=packets.filter(x=>x.pts_time!==undefined).map(x=>+x.pts_time),decodeTimes=packets.filter(x=>x.dts_time!==undefined).map(x=>+x.dts_time);const presentationReorders=times.slice(1).filter((t,i)=>t<times[i]).length,duplicatePresentationTimes=times.length-new Set(times).size,nonIncreasingDecodeTimes=decodeTimes.slice(1).filter((t,i)=>t<=decodeTimes[i]).length;assert(times.length>10&&decodeTimes.length>10,'Video packet timestamps missing');assert.equal(duplicatePresentationTimes,0);assert.equal(nonIncreasingDecodeTimes,0);return {durationSeconds:+p.format.duration,bytes:+p.format.size,averageMuxedKbps:+p.format.size*8/+p.format.duration/1000,encodedPackets:times.length,encodedFps:(times.length-1)/(times.at(-1)-times[0]),presentationReorders,duplicatePresentationTimes,nonIncreasingDecodeTimes}
  })
  await check(row,'receiver audible page tone',async()=>{const m=await audioEvidence(file,join(directory,'audio.f32'));assert(m.tone440>.001);return m})
  await check(row,'decoded audio and video sync',async()=>{
   const {stdout}=await exec('ffmpeg',['-v','error','-i',file,'-an','-vf','crop=64:64:400:0,scale=1:1,fps=60:start_time=0,format=gray','-f','rawvideo','pipe:1'],{encoding:'buffer',maxBuffer:1024*1024});await writeFile(join(directory,'flashes.gray'),stdout)
   const result=JSON.parse((await exec('python3',[resolve('benchmarks/obs-cef/analyze-sync.py'),join(directory,'flashes.gray'),join(directory,'audio.f32')])).stdout);await writeFile(join(directory,'sync.json'),JSON.stringify(result,null,2)+'\n');assert(result.matchedFlashes>=5);assert(result.p95AbsoluteOffsetMs<200,`p95 absolute A/V offset ${result.p95AbsoluteOffsetMs}ms exceeds the 200ms fixture target`);return result
  })
 }catch(e){row.error=String(e.stack??e);row.errorDetails=errorDetails(e)}
 finally{sampler?.kill('SIGTERM');await broadcast?.stop().catch(()=>{});broadcast=undefined;await obs.request('StopStream').catch(()=>{});receive.kill('SIGINT');await writeFile(join(directory,'stream-status.json'),JSON.stringify(samples,null,2)+'\n');await writeFile(join(directory,'receiver.log'),logs.join(''));await browser?.close();browser=undefined;await Promise.all(sources.splice(0).map(s=>s.close()));row.completedAt=new Date().toISOString();await save();console.log(JSON.stringify(row))}
}
try{
 const mode=process.env.FEATURE_MODE??'all',backends=(process.env.FEATURE_BACKENDS??'obs,native').split(',')
 assert(['all','record','stream','soak'].includes(mode),'Unknown feature mode');assert(backends.every(b=>['obs','native'].includes(b)),'Unknown feature backend')
 if(mode==='all'||mode==='record')for(const audio of [false,true])for(const backend of backends)await recordFeatures(backend,audio)
 if(mode==='all'||mode==='stream')for(const backend of backends)await stream(backend,20000)
 if(mode==='all'||mode==='soak')for(const backend of backends)await stream(backend,300000)
}finally{obs.close();server.closeAllConnections();await new Promise(r=>server.close(r))}
