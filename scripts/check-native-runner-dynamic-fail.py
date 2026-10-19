# SPDX-License-Identifier: MIT
"""Real native scheduler/host dynamic expected-failure behavior, without browser or stock runner."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse,json,pathlib,subprocess,os,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();exe=a.executable.resolve();sdk=a.sdk_directory.resolve();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
pre="import assert from 'node:assert/strict';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const wait=ms=>new Promise(r=>setTimeout(r,ms));\n"
def run(name,source,exit=0,options=()):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(pre+source);report=d/'report.json';junit=d/'junit.xml';html=d/'report.html'
 for f in [report,junit,html]:f.unlink(missing_ok=True)
 r=subprocess.run([str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),'--junit',str(junit),'--html',str(html),*options],capture_output=True,timeout=15)
 (d/'stdout.bin').write_bytes(r.stdout);(d/'stderr.bin').write_bytes(r.stderr);assert r.returncode==exit,(name,r.returncode,r.stderr);assert not r.stderr,r.stderr;v=json.loads(report.read_text());v['_output']=''.join(bytes.fromhex(w['stdout']['data']).decode() for w in v['workers']);v['_xml']=ET.parse(junit);v['_html']=html.read_text()
 for w in v['workers']:
  assert not bytes.fromhex(w['stderr']['data'])
  try:os.kill(w['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('host alive',w['pid']))
 groups.append({'name':name,'report':str(report),'exitCode':r.returncode});return v

v=run('native-body-mark-preserves-actual-error-and-failed-status',"export default defineNativeSuite({tests:[{title:'actual marked failure',run:(_,i)=>{i.fail('real expected failure');throw Error('original actual error')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expected'] and v['attempts'][0]['expectedStatus']=='failed' and 'original actual error' in v['attempts'][0]['error'] and v['attempts'][0]['expectedFailureReason']=='real expected failure'
assert v['_xml'].find(".//property[@name='native.attempt0.expectedStatus']").attrib['value']=='failed'
v=run('false-condition-does-not-mark',"export default defineNativeSuite({tests:[{title:'false',run:async(_,i)=>{await i.fail(false,'unused');}}]});")
assert v['attempts'][0]['status']=='passed' and v['attempts'][0]['expectedStatus']=='passed' and 'expectedFailureReason' not in v['attempts'][0]
v=run('beforeEach-mark-applies-to-actual-test-attempt',"export default defineNativeSuite({hooks:{beforeEach:[{run:async(_,i)=>i.fail('hook reason')}],afterEach:[{run:(_,i)=>assert.equal(i.status,'failed')}]},tests:[{title:'actual body failure',run:()=>{throw Error('genuine body error')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expected'] and v['attempts'][0]['expectedFailureReason']=='hook reason'
v=run('unexpected-pass-is-a-real-failed-expectation',"export default defineNativeSuite({tests:[{title:'actual unexpected pass',run:async(_,i)=>i.fail()}]});",1)
assert v['attempts'][0]['status']=='passed' and not v['attempts'][0]['expected'] and v['attempts'][0]['error']=='Expected to fail, but passed.'
v=run('retry-resets-native-runtime-mark-to-pinned-declaration',"export default defineNativeSuite({tests:[{title:'no leaked mark',retries:1,run:(_,i)=>{if(!i.retry)i.fail('first attempt only')}}]});")
assert [x['status'] for x in v['attempts']]==['passed','passed'] and [x['expected'] for x in v['attempts']]==[False,True] and [x['expectedStatus'] for x in v['attempts']]==['failed','passed'] and len(v['workers'])==2 and v['summary']['flaky']==1
v=run('retry-real-failure-then-expected-real-failure',"export default defineNativeSuite({tests:[{title:'retry then mark',retries:1,run:(_,i)=>{if(i.retry)i.fail('second attempt');throw Error('actual failure each attempt')}}]});")
assert [x['status'] for x in v['attempts']]==['failed','failed'] and [x['expected'] for x in v['attempts']]==[False,True] and len(v['workers'])==2
v=run('cleanup-exception-not-excused-by-dynamic-mark',"export default defineNativeSuite({hooks:{afterEach:[{run:()=>{throw Error('actual cleanup failure')}}]},tests:[{title:'marked actual error plus cleanup',run:(_,i)=>{i.fail('body expectation');throw Error('actual body error')}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'actual body error' in v['attempts'][0]['error'] and not v['globalErrors']
assert_attempt_secondary(v,'Error: actual cleanup failure',first_line=True)
v=run('cleanup-only-error-is-not-expected-body-failure',"export default defineNativeSuite({hooks:{afterEach:[{run:()=>{throw Error('cleanup only failure')}}]},tests:[{title:'marked successful body',run:(_,i)=>{i.fail('does not excuse cleanup')}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'cleanup only failure' in v['attempts'][0]['error']
v=run('actual-fixture-teardown-is-not-expected-body-failure',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);throw Error('real fixture teardown failure')}}},tests:[{title:'fixture actual error',run:(_,i)=>i.fail('does not excuse teardown')}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real fixture teardown failure' in v['attempts'][0]['error']
v=run('native-timeout-is-not-expected-failure',"export default defineNativeSuite({tests:[{title:'real timeout',timeout:100,run:async(_,i)=>{await i.fail();await new Promise((_,reject)=>i.signal.addEventListener('abort',()=>reject(i.signal.reason),{once:true}))}}]});",1)
assert v['attempts'][0]['status']=='timedOut' and not v['attempts'][0]['expected']
v=run('native-global-cap-is-not-expected-failure',"export default defineNativeSuite({tests:[{title:'real global stop',timeout:0,retries:1,run:async(_,i)=>{await i.fail('not global');await new Promise((_,reject)=>i.signal.addEventListener('abort',()=>reject(i.signal.reason),{once:true}))}}]});",1,('--global-timeout','700'))
assert v['attempts'][0]['status']=='interrupted' and not v['attempts'][0]['expected'] and len(v['attempts'])==1
v=run('skip-remains-real-skip-after-expectation-mark',"export default defineNativeSuite({tests:[{title:'native skip wins',run:async(_,i)=>{await i.fail('retained failure reason');i.skip('actual skip')}}]});")
assert v['attempts'][0]['status']=='skipped' and v['attempts'][0]['expected'] and v['attempts'][0]['expectedStatus']=='skipped' and v['attempts'][0]['skipReason']=='actual skip' and v['attempts'][0]['expectedFailureReason']=='retained failure reason'
v=run('closed-and-allhook-scopes-reject-dynamic-mark',"let saved;export default defineNativeSuite({hooks:{afterEach:[{run:()=>assert.throws(()=>saved.fail(),/no longer active/)}],afterAll:[{run:(_,i)=>assert.throws(()=>i.fail(),/supports test bodies and beforeEach/)}]},tests:[{title:'actual scope validation',run:(_,i)=>{saved=i}}]});")
assert v['attempts'][0]['status']=='passed' and v['attempts'][0]['expectedStatus']=='passed'
v=run('native-final-unexpected-pass-counts-maxFailures',"export default defineNativeSuite({tests:[{title:'actual unexpected pass',run:(_,i)=>i.fail('marked')},{title:'unrun declaration',run:()=>{throw Error('not admitted')}}]});",1,('--max-failures','1'))
assert len(v['attempts'])==1 and len(v['unrun'])==1 and v['attempts'][0]['status']=='passed' and not v['attempts'][0]['expected']
v=run('maximum-isolated-unit-failure-reason',"export default defineNativeSuite({tests:[{title:'actual exact reason',run:(_,i)=>{i.fail('\\ud800'.repeat(8192));throw Error('actual failure')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expectedFailureReason']=='\ud800'*8192
v=run('repeated-native-marks-retain-first-actual-reason',"export default defineNativeSuite({tests:[{title:'actual repeated marks',run:async(_,i)=>{await i.fail('first actual reason');await i.fail('second actual reason');throw Error('real body')}}]});")
assert v['attempts'][0]['expected'] and v['attempts'][0]['expectedFailureReason']=='first actual reason'
v=run('actual-body-and-fixture-errors-both-retained',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);throw Error('real second fixture error')}}},tests:[{title:'both actual failures',run:(_,i)=>{i.fail('marks body only');throw Error('real first body error')}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real first body error' in v['attempts'][0]['error'] and not v['globalErrors']
assert_attempt_secondary(v,'Error: real second fixture error',origin='fixtureRaw',first_line=True)
v=run('actual-beforeEach-marked-exception-suppresses-body',"export default defineNativeSuite({hooks:{beforeEach:[{run:(_,i)=>{i.fail('actual prebody reason');throw Error('actual beforeEach failure')}}],afterEach:[{run:(_,i)=>assert.equal(i.status,'failed')}]},tests:[{title:'body not admitted',run:()=>{throw Error('not admitted body')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expected'] and 'actual beforeEach failure' in v['attempts'][0]['error'] and 'not admitted body' not in v['attempts'][0]['error']
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Native body/beforeEach dynamic expected failure, unchanged raw outcome/errors and actual native ACK; cleanup/global/deadline/retry fences; no allhook/fixture mark support','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native expected-failure process groups')
