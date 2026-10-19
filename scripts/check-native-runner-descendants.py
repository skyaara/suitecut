# SPDX-License-Identifier: MIT
import argparse, json, pathlib, subprocess, os, time
parser=argparse.ArgumentParser()
parser.add_argument("--executable",type=pathlib.Path,required=True)
parser.add_argument("--host-script",type=pathlib.Path,required=True)
parser.add_argument("--suite-api",type=pathlib.Path,required=True)
parser.add_argument("--directory",type=pathlib.Path,required=True)
parser.add_argument("--node",required=True)
args=parser.parse_args()
root=args.directory.resolve();root.mkdir(parents=True,exist_ok=True)
api=args.suite_api.resolve().as_uri()
prelude='''import assert from 'node:assert/strict';import {spawn} from 'node:child_process';import fs from 'node:fs';import process from 'node:process';import {defineNativeSuite} from API;
const spawnHelper=async()=>{const child=spawn('/bin/sh',['-c','sleep 60 & printf ready >&4; wait'],{detached:true,stdio:['ignore','ignore','ignore','ignore','pipe']});child.unref();await new Promise((resolve,reject)=>{child.once('error',reject);child.stdio[4].once('data',resolve)});child.stdio[4].destroy();return child.pid;};
'''.replace('API',json.dumps(api))
results=[]
def run(name,body,code):
 d=root/'evidence'/name;d.mkdir(parents=True,exist_ok=True);suite=d/'suite.mjs';suite.write_text(prelude+body)
 report=d/'report.json';start=time.monotonic();p=subprocess.run([str(args.executable.resolve()),'--node',args.node,'--host-script',str(args.host_script.resolve()),'--suite',str(suite),'--report',str(report)],capture_output=True,timeout=20)
 (d/'stdout.bin').write_bytes(p.stdout);(d/'stderr.bin').write_bytes(p.stderr);assert p.returncode==code,(p.returncode,p.stderr,p.stdout)
 value=json.loads(report.read_text());assert not p.stderr,p.stderr
 results.append({'name':name,'exitCode':p.returncode,'elapsedMs':(time.monotonic()-start)*1000,'report':str(report)})
 return value,d
v,d=run('detached-descendant-cleanup',"export default defineNativeSuite({tests:[{title:'actual leaked helper',run:async()=>{const pid=await spawnHelper();console.log('actual detached helper '+pid)}}]});",1)
assert len(v['attempts'])==1 and v['attempts'][0]['status']=='passed';assert len(v['globalErrors'])==2,v
assert all('signal 9 after native cleanup SIGKILL' in x for x in v['globalErrors']),v
marker=root/'evidence'/'retry-pid.json'
v,d=run('retry-admission-after-descendants-exit',"const marker="+json.dumps(str(marker))+";export default defineNativeSuite({tests:[{title:'real retry after subtree fence',retries:1,run:async({},info)=>{if(info.retry===0){const pid=await spawnHelper();fs.writeFileSync(marker,JSON.stringify({pid,worker:process.pid}));throw new Error('real first body failure')}const old=JSON.parse(fs.readFileSync(marker,'utf8'));assert.notEqual(old.worker,process.pid);assert.throws(()=>process.kill(old.pid,0),{code:'ESRCH'});}}]});",1)
assert len(v['attempts'])==2 and v['attempts'][1]['status']=='passed';assert v['summary']['flaky']==1,v
v,d=run('ordinary-suite-no-descendants',"export default defineNativeSuite({tests:[{title:'ordinary',run:async()=>{assert.equal(1+2,3)}}]});",0)
assert v['summary']['expected']==1 and not v['globalErrors']
(root/'evidence'/'report.json').write_text(json.dumps({'passed':len(results),'groups':results},indent=2)+'\n')
print('PASS',len(results),'actual native CLI descendant ownership groups')
# A real runner signal cannot skip actual helper retirement.
import signal
name='signal-retains-descendants';d=root/'evidence'/name;d.mkdir(parents=True,exist_ok=True)
marker=d/'ready.json';suite=d/'suite.mjs';report=d/'report.json'
suite.write_text(prelude+"export default defineNativeSuite({tests:[{title:'signal with helper',timeout:0,run:async({},info)=>{const pid=await spawnHelper();fs.writeFileSync("+json.dumps(str(marker))+",JSON.stringify({pid,host:process.pid}));await new Promise((resolve,reject)=>info.signal.addEventListener('abort',()=>reject(info.signal.reason),{once:true}));}}]});")
marker.unlink(missing_ok=True)
p=subprocess.Popen([str(args.executable.resolve()),'--node',args.node,'--host-script',str(args.host_script.resolve()),'--suite',str(suite),'--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
deadline=time.monotonic()+5
while not marker.exists() and time.monotonic()<deadline:time.sleep(.01)
assert marker.exists(),'actual callback readiness missing'
p.send_signal(signal.SIGINT);out,err=p.communicate(timeout=10);assert p.returncode==130,(p.returncode,out,err);assert not err,err
value=json.loads(report.read_text());old=json.loads(marker.read_text());assert value['attempts'][0]['status']=='interrupted'
try:os.kill(old['pid'],0)
except ProcessLookupError:pass
else:raise AssertionError('actual helper remains alive after runner report')
(d/'stdout.bin').write_bytes(out);(d/'stderr.bin').write_bytes(err)
results.append({'name':name,'exitCode':p.returncode,'report':str(report)})
(root/'evidence'/'report.json').write_text(json.dumps({'passed':len(results),'groups':results},indent=2)+'\n')
print('PASS',len(results),'actual CLI groups including real SIGINT and helper retirement')
