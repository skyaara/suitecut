import {writeHeapSnapshot} from 'node:v8'
import {mkdir,writeFile} from 'node:fs/promises'
import {resolve,join} from 'node:path'
import {pathToFileURL} from 'node:url'
import {createServer} from 'node:http'
import {once} from 'node:events'
import {setTimeout as delay} from 'node:timers/promises'

const label=process.env.REUSE_LABEL??'before-lifecycle-fix',root=resolve(`benchmarks/obs-cef/2026-10-09-optimization/profiles/${label}`),media=resolve(`.suitecut/obs-cef-optimization/${label}`)
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true})
const {launchNativeBrowser,recordNative}=await import(pathToFileURL(join(resolve(process.env.PROFILE_DIST??'dist'),'native.js')))
const server=createServer((_req,res)=>res.end('<style>body{background:#142738;color:white}</style><h1>Source reuse memory test</h1><canvas width=960 height=540></canvas><script>const c=document.querySelector("canvas").getContext("2d");function f(t){c.fillStyle="#17354b";c.fillRect(0,0,960,540);c.fillStyle="yellow";c.fillRect(t%800,30,100,100);requestAnimationFrame(f)}f(0)</script>'))
server.listen(0,'127.0.0.1');await once(server,'listening')
const rows=[];let source
try{
 source=await launchNativeBrowser({executablePath:process.env.SUITECUT_NATIVE_EXECUTABLE,width:960,height:540,framesPerSecond:30,pixelFormat:'i420'});await source.navigate(`http://127.0.0.1:${server.address().port}`)
 for(let cycle=0;cycle<Number(process.env.REUSE_CYCLES??25);cycle++){
  await recordNative('reuse-'+cycle,async()=>delay(250),{source,output:{directory:join(media,String(cycle)),manifestPath:join(root,`manifest-${cycle}.json`)}})
  await delay(100);globalThis.gc?.();await delay(20);globalThis.gc?.()
  if(process.env.REUSE_HEAP_SNAPSHOTS==='1'&&(cycle===10||cycle===Number(process.env.REUSE_CYCLES??25)-1))writeHeapSnapshot(join(root,`heap-${cycle}.heapsnapshot`))
  rows.push({cycle,at:new Date().toISOString(),sourceState:source.state,...process.memoryUsage()});await writeFile(join(root,'memory.json'),JSON.stringify(rows,null,2));console.log(JSON.stringify(rows.at(-1)))
 }
}finally{await source?.close();await delay(100);globalThis.gc?.();await writeFile(join(root,'after-close.json'),JSON.stringify(process.memoryUsage(),null,2));server.closeAllConnections();server.close()}
