# SPDX-License-Identifier: MIT
"""Actual C++ nested routing and real Node host callbacks; no browser involved."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse,json,pathlib,subprocess,shutil,os,signal,time
parser=argparse.ArgumentParser();parser.add_argument("--executable",type=pathlib.Path,required=True);parser.add_argument("--sdk-directory",type=pathlib.Path,required=True);parser.add_argument("--directory",type=pathlib.Path,required=True);parser.add_argument("--node",required=True);args=parser.parse_args()
sdk=args.sdk_directory.resolve();executable=args.executable.resolve();out=args.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
prelude="import assert from 'node:assert/strict';import process from 'node:process';import fs from 'node:fs';import {Buffer} from 'node:buffer';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const say=x=>()=>console.log(x);\n"
def run(name,source,code=0,options=(),expected_globals=(),expected_secondary=None):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(prelude+source);report=d/'report.json'
 p=subprocess.run([str(executable),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),*options],capture_output=True,timeout=15)
 (d/'stdout.bin').write_bytes(p.stdout);(d/'stderr.bin').write_bytes(p.stderr);assert p.returncode==code,(name,p.returncode,p.stdout,p.stderr);assert not p.stderr,p.stderr
 v=json.loads(report.read_text());assert v['globalErrors']==list(expected_globals),(name,v)
 if expected_secondary is not None:assert_attempt_secondary(v,expected_secondary)
 for w in v['workers']:
  assert not bytes.fromhex(w['stderr']['data']),w
  try:os.kill(w['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('actual host survived report',w['pid']))
 groups.append({'name':name,'report':str(report),'exitCode':p.returncode});return v

def stdout(v):return b''.join(bytes.fromhex(w['stdout']['data']) for w in v['workers']).decode()
rootHooks="hooks:{beforeAll:[{run:say('root before')}],beforeEach:[{run:say('root beforeEach')}],afterEach:[{run:say('root afterEach')}],afterAll:[{run:say('root after')}]}"
v=run('real-native-hierarchy-order',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'first',run:say('first')},{title:'child',hooks:{beforeAll:[{run:say('child before')}],beforeEach:[{run:say('child beforeEach')}],afterEach:[{run:say('child afterEach')}],afterAll:[{run:say('child after')}]},entries:[{title:'inner',run:say('inner')}]},{title:'last',run:say('last')}]});")
assert v['summary']['expected']==3 and len(v['workers'])==1
assert stdout(v)=='root before\nroot beforeEach\nfirst\nroot afterEach\nchild before\nroot beforeEach\nchild beforeEach\ninner\nchild afterEach\nroot afterEach\nchild after\nroot beforeEach\nlast\nroot afterEach\nroot after\n',stdout(v)
assert v['attempts'][1]['title']=='child › inner'
v=run('native-selection-inactive-scopes',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'inactive',hooks:{beforeAll:[{run:()=>{throw Error('inactive suite admitted')}}]},entries:[{title:'one',run:()=>{throw Error('inactive body')}}]},{title:'active',hooks:{beforeAll:[{run:say('active before')}],afterAll:[{run:say('active after')}]},entries:[{title:'two',run:say('two')}]},{title:'inactive last',run:()=>{throw Error('inactive last body')}}]});",options=('--test-id','2'))
assert v['summary']['expected']==1 and stdout(v)=='root before\nactive before\nroot beforeEach\ntwo\nroot afterEach\nactive after\nroot after\n'
v=run('native-skipped-subtree-no-callbacks',"export default defineNativeSuite({entries:[{title:'inactive',hooks:{beforeAll:[{run:()=>{throw Error('skipped hook admitted')}}]},entries:[{title:'skipped',skip:'declared skip',run:()=>{throw Error('skipped body admitted')}}]}]});")
assert v['summary']['skipped']==1 and not v['attempts'] and stdout(v)==''
v=run('beforeAll-failure-only-blocks-real-subtree',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'bad',hooks:{beforeAll:[{run:()=>{console.log('bad before');throw Error('actual child beforeAll error')}}],afterAll:[{run:say('bad after')}]},entries:[{title:'first blocked',run:()=>{throw Error('body should never run')}},{title:'unrun descendant',run:()=>{throw Error('body should never run')}}]},{title:'real sibling',run:say('real sibling')}]});",1)
assert [(a['testId'],a['status']) for a in v['attempts']]==[('1','failed'),('3','passed')],v
assert v['summary']['didNotRun']==1 and v['unrun'][0]['testId']=='2' and len(v['workers'])==2
assert stdout(v)=='root before\nbad before\nbad after\nroot after\nroot before\nroot beforeEach\nreal sibling\nroot afterEach\nroot after\n'
v=run('actual-afterAll-error-attributed-final-test',"export default defineNativeSuite({entries:[{title:'child',hooks:{afterAll:[{run:()=>{throw Error('actual nested afterAll error')}}]},entries:[{title:'body passed',run:()=>{assert(true)}}]},{title:'sibling still executes',run:()=>{assert(true)}}]});",1)
assert [(a['testId'],a['status']) for a in v['attempts']]==[('1','failed'),('2','passed')]
assert 'actual nested afterAll error' in v['attempts'][0]['error'] and len(v['workers'])==2
v=run('beforeEach-error-still-runs-ancestor-cleanup',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'child',hooks:{beforeEach:[{run:()=>{console.log('child fails');throw Error('real beforeEach')}}],afterEach:[{run:say('child cleanup')}],afterAll:[{run:say('child after')}]},entries:[{title:'no body',run:()=>{throw Error('body admitted unexpectedly')}}]}]});",1)
assert stdout(v)=='root before\nroot beforeEach\nchild fails\nchild cleanup\nroot afterEach\nchild after\nroot after\n',stdout(v)
v=run('actual-worker-retry-reenters-needed-scopes',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'child',hooks:{beforeAll:[{run:say('child before')}],afterAll:[{run:say('child after')}]},entries:[{title:'retry',retries:1,run:(_,info)=>{console.log('retry '+info.retry);if(!info.retry)throw Error('real retry failure')}},{title:'next',run:say('next')}]},{title:'outer',run:say('outer')}]});")
assert v['summary']['flaky']==1 and len(v['workers'])==2 and v['attempts'][1]['retry']==1
assert stdout(v)=='root before\nchild before\nroot beforeEach\nretry 0\nroot afterEach\nchild after\nroot after\nroot before\nchild before\nroot beforeEach\nretry 1\nroot afterEach\nroot beforeEach\nnext\nroot afterEach\nchild after\nroot beforeEach\nouter\nroot afterEach\nroot after\n',stdout(v)
v=run('genuine-per-test-and-all-hook-fixture-scopes',"""
let workerValue,testValue,beforeAllValue;let sequence=0;
const fixtures={shared:{scope:'worker',setup:async(_,use)=>{const value={pid:process.pid};workerValue=value;await use(value);console.log('worker release')}},local:{dependencies:['shared'],setup:async({shared},use,info)=>{assert.equal(shared,workerValue);const value={sequence:++sequence,info};console.log('setup '+value.sequence+' '+info.scope);await use(value);console.log('release '+value.sequence)}}};
export default defineNativeSuite({fixtures,entries:[{title:'child',hooks:{beforeAll:[{fixtures:['local'],run:({local})=>{beforeAllValue=local;assert.equal(local.info.scope,'hook')}}],beforeEach:[{fixtures:['local'],run:({local})=>{assert.notEqual(local,beforeAllValue);testValue=local;}}],afterEach:[{fixtures:['local'],run:({local})=>{assert.equal(local,testValue)}}],afterAll:[{fixtures:['local'],run:({local})=>{assert.notEqual(local,testValue);assert.notEqual(local,beforeAllValue)}}]},entries:[{title:'actual scoped body',fixtures:['local'],run:({local})=>{assert.equal(local,testValue);assert.equal(local.info.scope,'test')}}]}]});
""")
assert stdout(v)=='setup 1 hook\nrelease 1\nsetup 2 test\nrelease 2\nsetup 3 hook\nrelease 3\nworker release\n',stdout(v)
v=run('raw-pending-callback-remains-owned-until-real-exit',"export default defineNativeSuite({entries:[{title:'child',hooks:{afterAll:[{run:say('cleanup after canceled raw body')}]},entries:[{title:'pending',timeout:30,run:async()=>{console.log('raw callback started');await new Promise(()=>{})}}]}]});",1,expected_globals=('Hook raw callback physical cleanup deadline expired',),expected_secondary='Fixture scope original deadline expired')
assert v['attempts'][0]['status']=='timedOut' and v['workers'][0]['signal']==9
assert stdout(v)=='raw callback started\ncleanup after canceled raw body\n',stdout(v)
v=run('nested-maxFailures-does-not-admit-pending-siblings',"export default defineNativeSuite({"+rootHooks+",entries:[{title:'child',hooks:{afterAll:[{run:say('child cleanup')}]},entries:[{title:'first final failure',run:()=>{throw Error('actual failure limit trigger')}},{title:'not admitted',run:()=>{throw Error('forbidden admission')}}]},{title:'later sibling',run:()=>{throw Error('forbidden sibling admission')}}]});",1,options=('--max-failures','1'))
assert len(v['attempts'])==1 and v['summary']['didNotRun']==2 and all(x['reason']=='Native maximum failure count reached' for x in v['unrun']),v
assert stdout(v)=='root before\nroot beforeEach\nroot afterEach\nchild cleanup\nroot after\n',stdout(v)
v=run('subtree-unrun-excludes-already-static-skipped-declarations',"export default defineNativeSuite({entries:[{title:'bad',hooks:{beforeAll:[{run:()=>{throw Error('actual beforeAll')}}]},entries:[{title:'actual first failure',run:()=>{}},{title:'already skipped',skip:'actual declaration skip',run:()=>{}},{title:'unrun',run:()=>{}}]},{title:'outside',run:()=>{}}]});",1)
assert v['summary']['skipped']==1 and v['summary']['didNotRun']==1 and v['summary']['expected']==1 and v['summary']['unexpected']==1,v
d=out/'actual-nested-SIGINT';d.mkdir(exist_ok=True);marker=d/'actual-host-pid';marker.unlink(missing_ok=True)
suite=d/'suite.mjs';suite.write_text("import fs from 'node:fs';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";export default defineNativeSuite({hooks:{afterAll:[{run:()=>console.log('root cleanup')}]},entries:[{title:'child',hooks:{afterAll:[{run:()=>console.log('child cleanup')}]},entries:[{title:'actual interrupted body',timeout:0,retries:2,run:async(_,info)=>{fs.writeFileSync("+json.dumps(str(marker))+",String(process.pid));await new Promise(resolve=>info.signal.addEventListener('abort',()=>{console.log('actual abort');resolve()}));throw info.signal.reason;}},{title:'unrun child',run:()=>{throw Error('admitted forbidden body')}}]},{title:'unrun sibling',run:()=>{throw Error('admitted forbidden sibling')}}]});")
p=subprocess.Popen([str(executable),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(d/'report.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE);end=time.monotonic()+5
while not marker.exists():
 assert time.monotonic()<end and p.poll() is None,'actual host marker never arrived';time.sleep(.005)
actual_host_pid=int(marker.read_text());os.kill(p.pid,signal.SIGINT);stdout,stderr=p.communicate(timeout=5);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);v=json.loads((d/'report.json').read_text());assert p.returncode==130 and not stderr;assert len(v['workers'])==1 and v['workers'][0]['pid']==actual_host_pid and v['workers'][0]['exitCode']==0;assert len(v['attempts'])==1 and v['attempts'][0]['status']=='interrupted';assert v['summary']['didNotRun']==2;assert v['globalErrors']==['Native runner interrupted by actual SIGINT'];assert bytes.fromhex(v['workers'][0]['stdout']['data'])==b'actual abort\nchild cleanup\nroot cleanup\n';groups.append({'name':'actual-nested-SIGINT','report':str(d/'report.json'),'exitCode':p.returncode})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual public nested suite groups')
