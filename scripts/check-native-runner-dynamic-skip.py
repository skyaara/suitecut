# SPDX-License-Identifier: MIT
"""Real native scheduler/host runtime-skip behavior, without browser or stock runner."""
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
v=run('body-stops-after-native-skip-and-real-attempt',"export default defineNativeSuite({tests:[{title:'runtime',run:(_,i)=>{i.skip('actual reason');throw Error('unreachable')}},{title:'next same worker',run:()=>{console.log('next')}}]});")
assert [x['status'] for x in v['attempts']]==['skipped','passed'] and v['attempts'][0]['skipReason']=='actual reason'
assert v['summary']['skipped']==1 and v['summary']['expected']==1 and v['skipped']==[] and len(v['workers'])==1 and v['globalErrors']==[]
assert v['_xml'].find(".//property[@name='native.attempt0.skipReason']").attrib['value']=='actual reason' and 'Runtime skip reason' in v['_html']
v=run('conditional-false-and-noarg',"export default defineNativeSuite({tests:[{title:'false',run:(_,i)=>{i.skip(false,'no');console.log('ran')}},{title:'noargs',run:(_,i)=>i.skip()}]});")
assert [x['status'] for x in v['attempts']]==['passed','skipped'] and v['attempts'][1]['skipReason']=='' and 'ran' in v['_output']
v=run('beforeEach-skip-preserves-cleanup-and-worker-fixtures',"let ready=0;export default defineNativeSuite({fixtures:{resource:{scope:'worker',automatic:true,setup:async(_,use)=>{console.log('worker setup');await use(++ready);console.log('worker teardown')}}},hooks:{beforeEach:[{run:(_,i)=>{i.skip(i.testId==='1','from beforeEach')}},{run:()=>console.log('before second')}],afterEach:[{run:(_,i)=>console.log('after '+i.status)}],afterAll:[{run:()=>console.log('afterAll')}]},tests:[{title:'skip before',run:()=>{throw Error('not admitted')}},{title:'next',fixtures:['resource'],run:({resource})=>assert.equal(resource,1)}]});")
assert [x['status'] for x in v['attempts']]==['skipped','passed'] and len(v['workers'])==1 and v['_output'].count('before second')==1
assert 'after skipped' in v['_output'] and v['_output'].count('worker setup')==1 and v['_output'].count('worker teardown')==1 and v['_output'].count('afterAll')==1
v=run('cleanup-failure-overrides-skip-and-expected-failure',"export default defineNativeSuite({hooks:{afterEach:[{run:()=>{throw Error('actual cleanup failure')}}]},tests:[{title:'real failure',expectedFailure:true,run:(_,i)=>i.skip('still retains reason')}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'actual cleanup failure' in v['attempts'][0]['error'] and v['attempts'][0]['skipReason']=='still retains reason'
v=run('caught-skip-real-exception-overrides',"export default defineNativeSuite({tests:[{title:'catch',run:(_,i)=>{try{i.skip('caught')}catch{}throw Error('real after caught skip')}}]});",1)
assert v['attempts'][0]['status']=='failed' and 'real after caught skip' in v['attempts'][0]['error']
v=run('ordinary-error-cannot-forge-native-skip',"export default defineNativeSuite({tests:[{title:'genuine exception',run:()=>{throw Error('Native runtime skip requested')}}]});",1)
assert v['attempts'][0]['status']=='failed' and 'skipReason' not in v['attempts'][0]
v=run('caught-skip-physical-callback-drains-before-next-test',"let active=false;export default defineNativeSuite({hooks:{afterEach:[{run:()=>console.log('cleanup')}]},tests:[{title:'caught slow',run:async(_,i)=>{active=true;try{i.skip('wait for raw return')}catch{}await wait(90);active=false;console.log('raw settled')}},{title:'next physical',run:()=>{assert.equal(active,false);console.log('next admitted')}}]});")
assert [x['status'] for x in v['attempts']]==['skipped','passed'] and len(v['workers'])==1 and v['_output'].index('raw settled')<v['_output'].index('next admitted')
v=run('never-settling-caught-skip-is-physical-failure',"export default defineNativeSuite({hooks:{afterEach:[{run:()=>console.log('real cleanup')}]},tests:[{title:'physical hung',timeout:5000,run:async(_,i)=>{try{i.skip('hang')}catch{}await new Promise(()=>{})}}]});",1)
assert v['attempts'][0]['status']=='failed' and v['workers'][0]['signal']==9 and v['attempts'][0]['skipReason']=='hang' and 'real cleanup' in v['_output']
v=run('cleanup-timeout-is-not-clean-skip',"export default defineNativeSuite({hooks:{afterEach:[{run:async()=>new Promise(()=>{})}]},tests:[{title:'cleanup hangs',timeout:80,run:(_,i)=>i.skip('cleanup timeout')}]});",1)
assert v['attempts'][0]['status']=='timedOut' and not v['attempts'][0]['expected'] and v['attempts'][0]['skipReason']=='cleanup timeout'
v=run('failure-retry-then-runtime-skip',"export default defineNativeSuite({tests:[{title:'retry skip',retries:1,run:(_,i)=>{if(!i.retry)throw Error('first real failure');i.skip('retry reason')}}]});")
assert [x['status'] for x in v['attempts']]==['failed','skipped'] and len(v['workers'])==2 and v['summary']['skipped']==1
v=run('nested-last-skipped-test-runs-actual-afterAll',"export default defineNativeSuite({entries:[{title:'parent',hooks:{afterAll:[{run:(_,i)=>{assert.equal(i.status,'skipped');console.log('nested afterAll')}}]},entries:[{title:'skip last',run:(_,i)=>i.skip('nested')}]}]});")
assert v['attempts'][0]['status']=='skipped' and 'nested afterAll' in v['_output'] and v['globalErrors']==[]
v=run('afterAll-runtime-skip-explicitly-rejected',"export default defineNativeSuite({hooks:{afterAll:[{run:(_,i)=>i.skip('unsupported all hook')}]},tests:[{title:'not admitted',run:()=>{}}]});",1)
assert v['attempts'][0]['status']=='failed' and 'supports test bodies, beforeEach and beforeAll' in v['attempts'][0]['error'] and 'skipReason' not in v['attempts'][0]
v=run('exact-reason-UTF16-and-native-reporter-properties',"export default defineNativeSuite({tests:[{title:'reason',run:(_,i)=>i.skip('exact\\ud800\\u0000<&')}]});")
assert v['attempts'][0]['skipReason']=='exact\ud800\x00<&'
prop=v['_xml'].find(".//property[@name='native.attempt0.skipReason']");assert prop.attrib['value']=='exact\ufffd\ufffd<&'
assert v['_xml'].find(".//property[@name='native.attempt0.skipReasonXmlSanitizedUnits']").attrib['value']=='2'
v=run('test-fixture-teardown-error-overrides-runtime-skip',"export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{await use(1);throw Error('real fixture teardown failure')}}},tests:[{title:'actual fixture cleanup',run:(_,i)=>i.skip('fixture cleanup')}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real fixture teardown failure' in v['attempts'][0]['error']
v=run('maxFailures-counts-final-cleanup-failure-not-clean-skip',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{if(i.testId==='2')throw Error('count actual cleanup failure')}}]},tests:[{title:'clean skip',run:(_,i)=>i.skip('does not count')},{title:'cleanup failure',run:(_,i)=>i.skip('does count as failed')},{title:'unrun',run:()=>{throw Error('not admitted')}}]});",1,('--max-failures','1'))
assert [x['status'] for x in v['attempts']]==['skipped','failed'] and len(v['unrun'])==1 and v['summary']['didNotRun']==1
for hooks in [False,True]:
 v=run('global-stop-during-skip-raw-drain-'+str(hooks),"export default defineNativeSuite({"+("hooks:{afterEach:[{run:()=>{}}]}," if hooks else "")+"tests:[{title:'actual raw skip callback',timeout:5000,retries:1,run:async(_,i)=>{try{i.skip('pending callback')}catch{}await wait(1500)}}]});",1,('--global-timeout','800'))
 assert v['attempts'][0]['status']=='interrupted' and len(v['attempts'])==1 and v['workers'][0]['signal']==9 and 'Native suite original global timeout of 800ms exceeded' in v['globalErrors']
v=run('maximum-isolated-unit-reason-fits-control-budget',"export default defineNativeSuite({tests:[{title:'maximum exact reason',run:(_,i)=>i.skip('\\ud800'.repeat(8192))}]});")
assert v['attempts'][0]['status']=='skipped' and v['attempts'][0]['skipReason']=='\ud800'*8192 and v['globalErrors']==[]
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Native body/beforeEach runtime skip; genuine attempts, cleanup, raw callbacks and process-exit ownership; afterAll and fixture skip unsupported','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native runtime-skip process groups')
