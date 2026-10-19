# SPDX-License-Identifier: MIT
"""Real dedicated-fd native runner callbacks; metadata values come from C++."""
import argparse,json,pathlib,subprocess,os,signal,time
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args()
exe=a.executable.resolve();sdk=a.sdk_directory.resolve();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
prelude="import assert from 'node:assert/strict';import process from 'node:process';import fs from 'node:fs';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const check=(i,s)=>{assert.equal(i.status,s);assert(Number.isFinite(i.duration)&&i.duration>=0);console.log(JSON.stringify({phase:i.hook??'body',status:i.status,duration:i.duration,retry:i.retry}));};const delay=ms=>new Promise(r=>setTimeout(r,ms));\n"
def run(name,source,code=0,options=()):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(prelude+source);report=d/'report.json';cmd=[str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),*options]
 r=subprocess.run(cmd,capture_output=True,timeout=12);(d/'stdout.bin').write_bytes(r.stdout);(d/'stderr.bin').write_bytes(r.stderr);assert r.returncode==code,(name,r.returncode,r.stderr);assert not r.stderr,r.stderr
 v=json.loads(report.read_text());rows=[]
 for w in v['workers']:
  assert not bytes.fromhex(w['stderr']['data']);rows.extend(json.loads(x) for x in bytes.fromhex(w['stdout']['data']).decode().splitlines())
  try:os.kill(w['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('host survived',w['pid']))
 groups.append({'name':name,'report':str(report),'exitCode':r.returncode});return v,rows
v,r=run('failed-body-observed-before-cleanup',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{check(i,'failed');assert(i.duration>=15)}}],afterAll:[{run:(_,i)=>check(i,'failed')}]},tests:[{title:'actual failure',run:async(_,i)=>{check(i,'passed');await delay(20);throw Error('real body failure')}}]});",1)
assert v['attempts'][0]['status']=='failed' and [x['status'] for x in r]==['passed','failed','failed'] and r[2]['duration']>=r[1]['duration']
v,r=run('native-timeout-observed-before-cleanup',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{check(i,'timedOut');assert(i.duration>=35)}}],afterAll:[{run:(_,i)=>check(i,'timedOut')}]},tests:[{title:'actual timeout',timeout:40,run:async(_,i)=>{check(i,'passed');await new Promise(resolve=>i.signal.addEventListener('abort',resolve,{once:true}));throw i.signal.reason;}}]});",1)
assert v['attempts'][0]['status']=='timedOut' and len(r)==3
v,r=run('expected-failure-remains-raw-failed',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>check(i,'failed')}],afterAll:[{run:(_,i)=>check(i,'failed')}]},tests:[{title:'expected actual failure',expectedFailure:true,run:()=>{throw Error('real expected failure')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expected'] is True and len(r)==2
v,r=run('cleanup-errors-change-later-native-snapshot',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{check(i,'passed');throw Error('actual cleanup error')}},{run:(_,i)=>check(i,'failed')}]},tests:[{title:'body passes',run:async()=>delay(5)}]});",1)
assert [x['status'] for x in r]==['passed','failed'] and r[1]['duration']>=r[0]['duration'] and v['attempts'][0]['status']=='failed'
v,r=run('beforeEach-failure-no-body-native-state',"export default defineNativeSuite({hooks:{beforeEach:[{run:()=>{throw Error('actual beforeEach')}}],afterEach:[{run:(_,i)=>check(i,'failed')}]},tests:[{title:'no body',run:()=>{throw Error('forbidden body')}}]});",1)
assert len(r)==1 and 'actual beforeEach' in v['attempts'][0]['error']
v,r=run('retry-resets-native-status-and-clock',"export default defineNativeSuite({hooks:{beforeEach:[{run:(_,i)=>check(i,'passed')}],afterEach:[{run:(_,i)=>check(i,i.retry?'passed':'failed')}],afterAll:[{run:(_,i)=>check(i,i.retry?'passed':'failed')}]},tests:[{title:'real retry',retries:1,run:async(_,i)=>{await delay(10);if(!i.retry)throw Error('first actual retry')}}]});")
assert v['summary']['flaky']==1 and len(v['workers'])==2 and [x['status'] for x in r]==['passed','failed','failed','passed','passed','passed']
v,r=run('nested-owning-hooks-associated-native-state',"export default defineNativeSuite({entries:[{title:'child',hooks:{beforeAll:[{run:(_,i)=>check(i,'passed')}],afterEach:[{run:(_,i)=>check(i,'failed')}],afterAll:[{run:(_,i)=>check(i,'failed')}]},entries:[{title:'body',run:async()=>{await delay(5);throw Error('nested actual failure')}}]}]});",1)
assert len(r)==3 and r[2]['duration']>=r[1]['duration']
v,r=run('flat-body-without-hooks-native-snapshot',"export default defineNativeSuite({fixtures:{value:{setup:async(_,use)=>{await delay(10);await use(1)}}},tests:[{title:'actual body',fixtures:['value'],run:(_,i)=>{check(i,'passed');assert(i.duration>=5)}}]});")
assert len(r)==1 and v['summary']['expected']==1
v,r=run('global-timeout-original-semantics-retained',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>check(i,'interrupted')}]},tests:[{title:'actual global stop',timeout:0,run:async(_,i)=>{await new Promise(resolve=>i.signal.addEventListener('abort',resolve,{once:true}));throw i.signal.reason;}}]});",1,('--global-timeout','1000'))
assert v['attempts'][0]['status']=='interrupted' and len(r)==1 and any('global' in e.lower() for e in v['globalErrors'])
v,r=run('real-fixture-release-error-visible-to-afterAll',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);throw Error('real fixture release error')}}},hooks:{afterEach:[{run:(_,i)=>check(i,'passed')}],afterAll:[{run:(_,i)=>check(i,'failed')}]},tests:[{title:'real fixture release',run:()=>{}}]});",1)
assert [x['status'] for x in r]==['passed','failed'] and 'real fixture release error' in v['attempts'][0]['error']
v,r=run('author-host-clock-overrides-cannot-provide-metadata',"Date.now=()=>{throw Error('author Date.now forbidden')};Object.defineProperty(performance,'now',{value:()=>{throw Error('author performance.now forbidden')}});export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{check(i,'passed');assert(i.duration>=10)}}]},tests:[{title:'actual native monotonic clock',run:async()=>delay(15)}]});")
assert len(r)==1 and v['attempts'][0]['status']=='passed'
# Actual SIGINT ingress, separate from the ordinary callback rejection path.
d=out/'actual-native-SIGINT';d.mkdir(exist_ok=True);marker=d/'marker';marker.unlink(missing_ok=True);suite=d/'suite.mjs';suite.write_text(prelude+"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>check(i,'interrupted')}]},tests:[{title:'actual signal',timeout:0,run:async(_,i)=>{fs.writeFileSync("+json.dumps(str(marker))+",String(process.pid));await new Promise(resolve=>i.signal.addEventListener('abort',resolve,{once:true}));throw i.signal.reason;}}]});")
p=subprocess.Popen([str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(d/'report.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE);end=time.monotonic()+5
while not marker.exists():
 assert time.monotonic()<end and p.poll() is None;time.sleep(.005)
host=int(marker.read_text());os.kill(p.pid,signal.SIGINT);stdout,stderr=p.communicate(timeout=5);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);v=json.loads((d/'report.json').read_text());assert p.returncode==130 and not stderr and v['workers'][0]['pid']==host and v['workers'][0]['exitCode']==0;assert v['attempts'][0]['status']=='interrupted';r=[json.loads(x) for x in bytes.fromhex(v['workers'][0]['stdout']['data']).decode().splitlines()];assert len(r)==1 and r[0]['status']=='interrupted';groups.append({'name':'actual-native-SIGINT','report':str(d/'report.json'),'exitCode':p.returncode})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native phase observation groups')
