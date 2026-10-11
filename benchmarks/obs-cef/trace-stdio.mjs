// Benchmark-only observation of child stdin writes; preserves their return values.
import {createRequire, syncBuiltinESMExports} from 'node:module'
import {createWriteStream} from 'node:fs'
import {join} from 'node:path'
import {monitorEventLoopDelay} from 'node:perf_hooks'
const cp=createRequire(import.meta.url)('node:child_process'),original=cp.spawn
const output=createWriteStream(join(process.env.INVESTIGATE_OUTPUT,'stdio.jsonl'))
const children=[]
const loop=monitorEventLoopDelay({resolution:20});loop.enable()
cp.spawn=function(command,args,options){
 const child=original.call(this,command,args,options)
 const row={pid:child.pid,command,args,startedMs:performance.now(),writes:0,bytes:0,drains:0,maxDrainMs:0,pendingAt:null,closed:false}
 children.push(row);output.write(JSON.stringify({type:'spawn',...row})+'\n')
 if(child.stdin){
  const input=child.stdin,write=input.write
  input.write=function(...values){
   const ok=write.apply(this,values);row.writes++;row.bytes+=Buffer.byteLength(values[0])
   if(!ok && row.pendingAt===null)row.pendingAt=performance.now()
   return ok
  }
  input.on('drain',()=>{if(row.pendingAt!==null){row.drains++;row.maxDrainMs=Math.max(row.maxDrainMs,performance.now()-row.pendingAt);row.pendingAt=null}})
 }
 child.once('close',(code,signal)=>{row.closed=true;row.code=code;row.signal=signal;output.write(JSON.stringify({type:'close',atMs:performance.now(),...row})+'\n')})
 return child
}
syncBuiltinESMExports()
const timer=setInterval(()=>{
 const now=performance.now()
 output.write(JSON.stringify({type:'sample',atMs:now,loopMaxMs:loop.max/1e6,loopP99Ms:loop.percentile(99)/1e6,children:children.map(r=>({...r,currentDrainMs:r.pendingAt===null?0:now-r.pendingAt}))})+'\n');loop.reset()
},1000);timer.unref()
process.once('beforeExit',()=>{clearInterval(timer);loop.disable();output.end()})
