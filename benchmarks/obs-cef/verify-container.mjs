import assert from 'node:assert/strict'
import {execFile} from 'node:child_process'
import {mkdir,writeFile} from 'node:fs/promises'
import {resolve,join} from 'node:path'
import {setTimeout as delay} from 'node:timers/promises'
import {promisify} from 'node:util'
import {createNativeVideoRecorder} from '../../dist/native-recorder.js'
const exec=promisify(execFile),root=resolve('benchmarks/obs-cef/2026-10-09-optimization/checks/container'),media=resolve('.suitecut/obs-cef-optimization/container')
await mkdir(root,{recursive:true});await mkdir(media,{recursive:true});const rows=[]
for(const pixelFormat of ['bgra','i420']){
 const width=64,height=32,data=Buffer.alloc(width*height*(pixelFormat==='bgra'?4:1.5))
 if(pixelFormat==='bgra'){for(let y=0;y<height;y++)for(let x=0;x<width;x++){const offset=(y*width+x)*4;data[offset+(y<height/2?2:0)]=240;data[offset+3]=255}}
 else{data.fill(32,0,width*height/2);data.fill(200,width*height/2,width*height);data.fill(128,width*height)}
 const outputPath=join(media,`${pixelFormat}.webm`),recorder=createNativeVideoRecorder({source:{width,height,pixelFormat,framesPerSecond:30},ffmpegPath:'/usr/bin/ffmpeg',outputPath,size:{width,height}})
 for(const timestamp of [100,117,500]){recorder.push(data,timestamp);await delay(100)}await recorder.stop(1100)
 const {stdout}=await exec('ffprobe',['-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',outputPath]);const pts=JSON.parse(stdout).frames.map(f=>Number(f.best_effort_timestamp_time))
 assert.equal(pts.length,4);for(let i=0;i<4;i++)assert(Math.abs(pts[i]-[0,.017,.4,1][i])<.002,`Timestamp ${i}: ${pts[i]}`)
 const {stdout:pixels}=await exec('ffmpeg',['-v','error','-i',outputPath,'-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','pipe:1'],{encoding:'buffer'});const top=[...pixels.subarray(3*width*4,3*width*4+3)],bottom=[...pixels.subarray(3*width*24,3*width*24+3)]
 if(pixelFormat==='bgra'){assert(top[0]>220&&top[2]<20);assert(bottom[2]>220&&bottom[0]<20)}else{assert(top.every(x=>x<30));assert(bottom.every(x=>x>200))}
 rows.push({pixelFormat,pts,topRgb:top,bottomRgb:bottom,status:'passed'})
}
await writeFile(join(root,'results.json'),JSON.stringify(rows,null,2));console.log(rows)
