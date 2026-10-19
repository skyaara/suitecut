# SPDX-License-Identifier: MIT
from native_error_attribution_proof import assert_attempt_secondary
import argparse,json,pathlib,subprocess,os,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();sdk=a.sdk_directory.resolve();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
pre="import assert from 'node:assert/strict';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const wait=ms=>new Promise(r=>setTimeout(r,ms));\n"
def run(name,source,exit=0,options=()):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(pre+source);report=d/'report.json';junit=d/'junit.xml';html=d/'report.html';subprocess.run([a.node,'--check',str(suite)],check=True,capture_output=True)
 for f in [report,junit,html]:f.unlink(missing_ok=True)
 r=subprocess.run([a.executable,'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),'--junit',str(junit),'--html',str(html),*options],capture_output=True,timeout=20)
 (d/'stdout.bin').write_bytes(r.stdout);(d/'stderr.bin').write_bytes(r.stderr);assert r.returncode==exit,(name,r.returncode,r.stderr);assert not r.stderr,r.stderr
 v=json.loads(report.read_text());v['_output']=''.join(bytes.fromhex(w['stdout']['data']).decode() for w in v['workers']);v['_xml']=ET.parse(junit);v['_html']=html.read_text()
 for w in v['workers']:
  assert not bytes.fromhex(w['stderr']['data'])
  try:os.kill(w['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('host alive',w['pid']))
 for attempt in v['attempts']:
  assert 'AssertionError' not in attempt['error'],(name,attempt['error'])
 groups.append({'name':name,'report':str(report),'exitCode':r.returncode});return v
v=run('actual-static-native-expectation-at-admission',"export default defineNativeSuite({tests:[{title:'static native mark',expectedFailure:true,run:(_,i)=>{assert.equal(i.expectedStatus,'failed');assert.deepEqual(i.annotations,[]);throw Error('actual body failure')}}]});")
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expectedStatus']=='failed' and v['attempts'][0]['expected'] and v['attempts'][0]['annotations']==[] and 'actual body failure' in v['attempts'][0]['error']
v=run('getter-updates-only-after-actual-native-fail-ACK',"export default defineNativeSuite({tests:[{title:'actual live native snapshot',run:async(_,i)=>{const old=i.annotations;assert.equal(i.expectedStatus,'passed');const request=i.fail('actual reason');assert.equal(i.expectedStatus,'passed');assert.deepEqual(i.annotations,[]);await request;assert.equal(i.expectedStatus,'failed');assert.equal(i.annotations.length,1);assert.equal(old.length,0);assert(Object.isFrozen(i.annotations));assert(Object.isFrozen(i.annotations[0]));assert.equal(i.annotations[0].type,'fail');assert.equal(i.annotations[0].description,'actual reason');throw Error('actual original error')}}]});")
x=v['attempts'][0];assert x['expected'] and x['status']=='failed' and 'actual original error' in x['error'] and x['annotations'][0]['type']=='fail' and x['annotations'][0]['description']=='actual reason';location=x['annotations'][0]['location'];assert pathlib.Path(location['file']).resolve()==pathlib.Path(groups[-1]['report']).parent/'suite.mjs' and location['line']==2 and location['column']>0
v=run('native-beforeEach-ledger-supplies-body-and-cleanup-admission',"export default defineNativeSuite({hooks:{beforeEach:[{run:async(_,i)=>{assert.equal(i.expectedStatus,'passed');await i.fail('actual beforeEach reason');assert.equal(i.expectedStatus,'failed')}}],afterEach:[{run:(_,i)=>{assert.equal(i.expectedStatus,'failed');assert.equal(i.annotations[0].description,'actual beforeEach reason')}}]},tests:[{title:'body uses native observation',run:(_,i)=>{assert.equal(i.expectedStatus,'failed');assert.equal(i.annotations[0].type,'fail');throw Error('real body error')}}]});")
assert v['attempts'][0]['expected'] and 'real body error' in v['attempts'][0]['error'] and len(v['attempts'][0]['annotations'])==1
v=run('genuine-beforeAll-callsite-propagated-through-native-cause',"export default defineNativeSuite({hooks:{beforeAll:[{run:(_,i)=>i.skip('actual beforeAll reason')}],afterAll:[{run:(_,i)=>{assert.equal(i.expectedStatus,'skipped');assert.equal(i.annotations[0].type,'skip');assert.equal(i.annotations[0].description,'actual beforeAll reason')}}]},tests:[{title:'actual associated',run:()=>assert.fail('body')},{title:'native no-attempt declaration',run:()=>assert.fail('body')}]});")
assert v['summary']['skipped']==2 and len(v['runtimeSkipped'])==1 and v['runtimeSkipped'][0]['annotations']==v['skipCauses'][0]['annotations']==v['attempts'][0]['annotations'];assert v['attempts'][0]['annotations'][0]['location']['line']==2
v=run('missing-description-stays-absent',"export default defineNativeSuite({tests:[{title:'actual absent reason',run:async(_,i)=>{await i.fail();assert.equal(i.expectedStatus,'failed');assert.deepEqual(Object.keys(i.annotations[0]).sort(),['location','type']);throw Error('real error')}}]});")
assert 'description' not in v['attempts'][0]['annotations'][0]
v=run('explicit-empty-description-remains-present',"export default defineNativeSuite({tests:[{title:'actual empty reason',run:async(_,i)=>{await i.fail('');assert.equal(i.annotations[0].description,'');throw Error('real error')}}]});")
assert v['attempts'][0]['annotations'][0]['description']==''
v=run('V8-location-is-independent-from-user-Error-stack-formatter',"Error.prepareStackTrace=()=> 'forged nonexistent.ts:99:99';export default defineNativeSuite({tests:[{title:'actual V8 metadata',run:async(_,i)=>{await i.fail('genuine call');throw Error('actual body')}}]});")
assert v['attempts'][0]['annotations'][0]['location']['line']==2 and v['attempts'][0]['annotations'][0]['location']['file'].endswith('/suite.mjs')
v=run('retry-begins-with-fresh-native-expectation-and-annotations',"export default defineNativeSuite({tests:[{title:'per-attempt native state',retries:1,run:async(_,i)=>{assert.equal(i.expectedStatus,'passed');assert.deepEqual(i.annotations,[]);if(!i.retry)await i.fail('attempt zero only')}}]});")
assert [x['expectedStatus'] for x in v['attempts']]==['failed','passed'] and len(v['attempts'][0]['annotations'])==1 and not v['attempts'][1]['annotations'] and len(v['workers'])==2
v=run('false-modifier-condition-creates-no-native-annotation',"export default defineNativeSuite({tests:[{title:'actual no-op',run:async(_,i)=>{await i.fail(false,'not admitted');i.skip(false,'not admitted');assert.equal(i.expectedStatus,'passed');assert.deepEqual(i.annotations,[])}}]});")
assert v['attempts'][0]['status']=='passed' and not v['attempts'][0]['annotations']
v=run('maximum-UTF16-reason-keeps-packets-and-reporter-exact',"export default defineNativeSuite({tests:[{title:'exact annotation units',run:async(_,i)=>{await i.fail('\\ud800'.repeat(8192));assert.equal(i.annotations[0].description,'\\ud800'.repeat(8192));throw Error('real body')}}]});")
assert v['attempts'][0]['annotations'][0]['description']=='\ud800'*8192 and v['attempts'][0]['expected']
v=run('accepted-annotations-retained-through-real-cleanup-failure',"export default defineNativeSuite({hooks:{afterEach:[{run:(_,i)=>{assert.equal(i.expectedStatus,'failed');assert.equal(i.annotations[0].description,'body mark');throw Error('genuine cleanup failure')}}]},tests:[{title:'actual cleanup attribution',run:async(_,i)=>{await i.fail('body mark');throw Error('real body failure')}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and len(v['attempts'][0]['annotations'])==1 and not v['globalErrors']
assert_attempt_secondary(v,'Error: genuine cleanup failure',first_line=True)
v=run('accepted-beforeAll-annotation-survives-failed-afterAll',"export default defineNativeSuite({hooks:{beforeAll:[{run:(_,i)=>i.skip('before cleanup')}],afterAll:[{run:()=>{throw Error('actual afterAll failure')}}]},tests:[{title:'actual associated',run:()=>assert.fail('body')},{title:'not runtime skipped',run:()=>assert.fail('body')}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and not v['runtimeSkipped'] and len(v['unrun'])==1 and v['skipCauses'][0]['annotations']==v['attempts'][0]['annotations']
v=run('each-native-accepted-mark-preserves-own-reason-and-location',"export default defineNativeSuite({tests:[{title:'two actual native requests',run:async(_,i)=>{await i.fail('first actual');await i.fail('second actual');assert.deepEqual(i.annotations.map(a=>a.description),['first actual','second actual']);throw Error('actual body')}}]});")
assert v['attempts'][0]['expectedFailureReason']=='first actual' and [a['description'] for a in v['attempts'][0]['annotations']]==['first actual','second actual'] and v['attempts'][0]['annotations'][0]['location']['column']!=v['attempts'][0]['annotations'][1]['location']['column']
assert v['_xml'].find(".//property[@name='native.attempt0.annotation0.type']").attrib['value']=='fail'
assert v['_xml'].find(".//property[@name='native.attempt0.annotation1.description']").attrib['value']=='second actual'
assert v['_xml'].find(".//property[@name='native.attempt0.annotation1.location.line']").attrib['value']==str(v['attempts'][0]['annotations'][1]['location']['line']) and 'native-annotation' in v['_html']
v=run('true-afterAll-mark-remains-unsupported-without-annotation',"export default defineNativeSuite({hooks:{afterAll:[{run:(_,i)=>assert.throws(()=>i.fail('not accepted'),/supports test bodies and beforeEach/)}]},tests:[{title:'actual body',run:()=>{}}]});")
assert v['attempts'][0]['status']=='passed' and not v['attempts'][0]['annotations']
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native expectedStatus and admitted runtime modifier annotations with real V8 caller locations, no inference before ACK','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native expectation/annotation process groups')
