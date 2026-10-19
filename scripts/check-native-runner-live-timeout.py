# SPDX-License-Identifier: MIT
"""Actual native timeout amendments and callback completion ownership."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse,json,pathlib,subprocess,os
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();exe=a.executable.resolve();sdk=a.sdk_directory.resolve();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
pre="import assert from 'node:assert/strict';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const wait=ms=>new Promise(r=>setTimeout(r,ms));const aborted=i=>new Promise((resolve,reject)=>{if(i.signal.aborted){reject(i.signal.reason);return}i.signal.addEventListener('abort',()=>reject(i.signal.reason),{once:true})});\n"
def run(name,source,exit=0,options=()):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(pre+source);report=d/'report.json';cmd=[str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),*options];r=subprocess.run(cmd,capture_output=True,timeout=12);(d/'stdout.bin').write_bytes(r.stdout);(d/'stderr.bin').write_bytes(r.stderr);assert r.returncode==exit,(name,r.returncode,r.stderr);assert not r.stderr,r.stderr;v=json.loads(report.read_text())
 for w in v['workers']:
  assert not bytes.fromhex(w['stderr']['data'])
  try:os.kill(w['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('host alive',w['pid']))
 groups.append({'name':name,'report':str(report),'exitCode':r.returncode});return v
v=run('flat-body-native-amendment-ACK',"export default defineNativeSuite({tests:[{title:'extend actual body',timeout:100,run:async(_,i)=>{assert.equal(i.timeout,100);await i.setTimeout(400);assert.equal(i.timeout,400);await wait(150)}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[]
v=run('beforeEach-main-and-active-fixture-deadline',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await wait(30);await use(42);}}},hooks:{beforeEach:[{run:async(_,i)=>{await i.setTimeout(300);assert.equal(i.timeout,300)}}],afterEach:[{run:(_,i)=>{assert.equal(i.timeout,300);assert.equal(i.status,'passed')}}]},tests:[{title:'real shared main budget',timeout:100,run:async(_,i)=>{assert.equal(i.timeout,300);await wait(120)}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[]
v=run('shortening-measured-from-original-main-start',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{assert.equal(i.status,'timedOut');assert.equal(i.timeout,80)}}]},tests:[{title:'real shorter deadline',timeout:1000,run:async(_,i)=>{await wait(30);await i.setTimeout(80);await aborted(i)}}]});",1)
assert v['attempts'][0]['status']=='timedOut' and v['globalErrors']==[]
assert_attempt_secondary(v,'Fixture scope original deadline expired')
v=run('cleanup-has-own-original-phase-start-and-fixture-budget',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);await wait(90)}}},hooks:{afterEach:[{run:async(_,i)=>{await i.setTimeout(200);assert.equal(i.timeout,200);await wait(20)}}]},tests:[{title:'real cleanup',timeout:100,run:async()=>{await wait(60)}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[]
v=run('all-hook-original-independent-phase-budget',"export default defineNativeSuite({hooks:{beforeAll:[{timeout:100,run:async(_,i)=>{await i.setTimeout(400);await wait(150)}}],afterAll:[{timeout:100,run:async(_,i)=>{await i.setTimeout(400);await wait(150)}}]},tests:[{title:'actual all-hook budgets',timeout:100,run:()=>{}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[]
v=run('fresh-retry-keeps-original-declared-timeout',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{assert.equal(i.status,i.retry?'passed':'failed');assert.equal(i.timeout,i.retry?500:60)}}]},tests:[{title:'actual retry',timeout:500,retries:1,run:async(_,i)=>{assert.equal(i.timeout,500);if(!i.retry){await i.setTimeout(60);throw Error('genuine first retry failure')}}}]});")
assert v['summary']['flaky']==1 and len(v['workers'])==2 and [x['status'] for x in v['attempts']]==['failed','passed']
v=run('zero-disables-main-budget-without-resetting-global-cap',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>assert.equal(i.status,'interrupted')}]},tests:[{title:'real global cap',timeout:100,retries:1,run:async(_,i)=>{await i.setTimeout(0);assert.equal(i.timeout,0);await aborted(i)}}]});",1,('--global-timeout','1000'))
assert len(v['attempts'])==1 and v['attempts'][0]['status']=='interrupted' and v['globalErrors']==['Native suite original global timeout of 1000ms exceeded']
v=run('ignored-successful-mutation-ACK-precedes-completion',"export default defineNativeSuite({tests:[{title:'actual unawaited mutation',run:(_,i)=>{i.setTimeout(300);i.setTimeout(400)}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[] and v['workers'][0]['exitCode']==0
v=run('native-validation-negative-timeouts-and-closed-callback',"let saved;export default defineNativeSuite({hooks:{afterEach:[{run:()=>{assert.throws(()=>saved.setTimeout(100),/no longer active/)}}]},tests:[{title:'real validation',run:(_,i)=>{saved=i;for(const t of [-1,0.5,NaN,Infinity,120001])assert.throws(()=>i.setTimeout(t));}}]});")
assert v['summary']['expected']==1 and v['globalErrors']==[]
v=run('cleanup-amendment-after-main-timeout',"export default defineNativeSuite({hooks:{afterEach:[{run:async(_,i)=>{assert.equal(i.status,'timedOut');await i.setTimeout(200);await wait(80)}}]},tests:[{title:'real timed out main',timeout:50,run:async(_,i)=>aborted(i)}]});",1)
assert v['attempts'][0]['status']=='timedOut' and v['globalErrors']==[] and v['workers'][0]['exitCode']==0
assert_attempt_secondary(v,'Fixture scope original deadline expired')
v=run('bounded-native-ACK-ledger-retains-ignored-mutations',"export default defineNativeSuite({tests:[{title:'real bounded mutation admission',timeout:0,run:(_,i)=>{for(let n=0;n<129;n++)i.setTimeout(1000)}}]});",1)
assert 'Native live timeout amendment capacity exceeded' in v['attempts'][0]['error'] and v['workers'][0]['exitCode']==0 and v['globalErrors']==[]
v=run('settled-amendments-do-not-impose-cumulative-quota',"export default defineNativeSuite({tests:[{title:'real sequential native amendments',timeout:0,run:async(_,i)=>{for(let n=0;n<4100;n++)await i.setTimeout(0);assert.equal(i.timeout,0)}},{title:'same actual worker remains usable',run:async(_,i)=>{assert.equal(i.timeout,30000);await i.setTimeout(400)}}]});")
assert v['summary']['expected']==2 and len(v['workers'])==1 and v['globalErrors']==[]
v=run('earlier-main-timeout-is-not-overwritten-by-global-cleanup-stop',"export default defineNativeSuite({hooks:{afterEach:[{run:async(_,i)=>{assert.equal(i.status,'timedOut');await i.setTimeout(2000);await aborted(i)}}]},tests:[{title:'earlier native main deadline',timeout:50,run:async(_,i)=>aborted(i)}]});",1,('--global-timeout','1000'))
assert v['attempts'][0]['status']=='timedOut' and v['workers'][0]['signal']==9 and v['globalErrors']==['Native suite original global timeout of 1000ms exceeded']
assert_attempt_secondary(v,'Host worker physically exited')
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native live-timeout process groups')
