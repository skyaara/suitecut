# SPDX-License-Identifier: MIT
import argparse,json,pathlib,subprocess,shutil
parser=argparse.ArgumentParser();parser.add_argument("--executable",type=pathlib.Path,required=True);parser.add_argument("--sdk-directory",type=pathlib.Path,required=True);parser.add_argument("--directory",type=pathlib.Path,required=True);parser.add_argument("--node",required=True);config=parser.parse_args()
out=config.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[];sdk=config.sdk_directory.resolve();executable=config.executable.resolve()
prelude="import assert from 'node:assert/strict';import process from 'node:process';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";const observe=(name,info)=>console.log(JSON.stringify({name,testId:info.testId,title:info.title,titlePath:info.titlePath,retry:info.retry,suiteId:info.suiteId,suiteTitlePath:info.suiteTitlePath,worker:process.pid}));\n"
def run(name,source,code=0,args=(),globals=()):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';report=d/'report.json';suite.write_text(prelude+source)
 p=subprocess.run([str(executable),'--node',config.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),*args],capture_output=True,timeout=15);(d/'stdout.bin').write_bytes(p.stdout);(d/'stderr.bin').write_bytes(p.stderr);assert p.returncode==code,(name,p.returncode,p.stdout,p.stderr);assert not p.stderr,p.stderr
 v=json.loads(report.read_text());assert v['globalErrors']==list(globals),(name,v);lines=[json.loads(line) for w in v['workers'] for line in bytes.fromhex(w['stdout']['data']).decode().splitlines()];groups.append({'name':name,'report':str(report),'code':p.returncode});return v,lines
v,lines=run('all-hook-retry-is-real-active-attempt',"export default defineNativeSuite({hooks:{beforeAll:[{run:(_,info)=>observe('before',info)}],afterAll:[{run:(_,info)=>observe('after',info)}]},tests:[{title:'flat',retries:1,run:(_,info)=>{observe('body',info);if(!info.retry)throw Error('actual first failure')}}]});")
assert v['summary']['flaky']==1 and len(v['workers'])==2
assert [(x['name'],x['retry'],x['testId']) for x in lines]==[('before',0,'1'),('body',0,'1'),('after',0,'1'),('before',1,'1'),('body',1,'1'),('after',1,'1')]
assert all(x['title']=='flat' and x['titlePath']==['flat'] for x in lines)
assert all(x.get('suiteId') is None and x['suiteTitlePath']==[] for x in lines if x['name']!='body')
source="""const rootHooks={beforeAll:[{run:(_,info)=>observe('root before',info)}],beforeEach:[{run:(_,info)=>observe('root each',info)}],afterAll:[{run:(_,info)=>observe('root after',info)}]};export default defineNativeSuite({hooks:rootHooks,entries:[{title:'first',run:(_,info)=>observe('first body',info)},{title:'A › B',hooks:{beforeAll:[{run:(_,info)=>observe('child before',info)}],beforeEach:[{run:(_,info)=>observe('child each',info)}],afterAll:[{run:(_,info)=>observe('child after',info)}]},entries:[{title:'one',run:(_,info)=>observe('inner one',info)},{title:'two',run:(_,info)=>observe('inner two',info)}]},{title:'last',run:(_,info)=>observe('last body',info)}]});"""
v,lines=run('native-actual-owner-and-associated-first-last',source)
byname={x['name']:x for x in lines};assert byname['root before']['testId']=='1' and byname['root after']['testId']=='4'
assert byname['child before']['testId']=='2' and byname['child after']['testId']=='3'
for x in lines:
 if x['name'].startswith('child'):assert x['suiteId']=='2' and x['suiteTitlePath']==['A › B']
 if x['name'].startswith('root'):assert x['suiteId']=='1' and x['suiteTitlePath']==[]
 if x['testId'] in ['2','3']:assert x['titlePath']==['A › B','one' if x['testId']=='2' else 'two']
v,lines=run('native-selection-pins-first-last-hook-metadata',source,args=('--test-id','3'))
assert v['summary']['expected']==1 and all(x['testId']=='3' and x['title']=='two' and x['titlePath']==['A › B','two'] for x in lines),lines
v,lines=run('exact-UTF16-ancestry-no-delimiter-splitting',"const label='x\\ud800\\0 › y';export default defineNativeSuite({entries:[{title:label,hooks:{beforeAll:[{run:(_,info)=>{assert.deepEqual(info.suiteTitlePath,[label]);assert.deepEqual(info.titlePath,[label,'leaf']);observe('unicode before',info)}}],afterAll:[{run:(_,info)=>{assert.deepEqual(info.suiteTitlePath,[label]);observe('unicode after',info)}}]},entries:[{title:'leaf',run:(_,info)=>{assert.deepEqual(info.titlePath,[label,'leaf']);observe('unicode body',info)}}]}]});")
assert len(lines)==3 and all(x['titlePath']==['x\ud800\0 › y','leaf'] for x in lines)
v,lines=run('beforeAll-failure-retry-has-actual-native-retry',"export default defineNativeSuite({entries:[{title:'child',hooks:{beforeAll:[{run:(_,info)=>{observe('before',info);if(!info.retry)throw Error('real beforeAll first failure')}}],afterAll:[{run:(_,info)=>observe('after',info)}]},entries:[{title:'first',retries:1,run:(_,info)=>observe('body',info)}]}]});")
assert v['summary']['flaky']==1 and [(x['name'],x['retry']) for x in lines]==[('before',0),('after',0),('before',1),('body',1),('after',1)]
v,lines=run('callback-local-mutation-does-not-change-native-declarations',"export default defineNativeSuite({entries:[{title:'child',hooks:{beforeAll:[{run:(_,info)=>{info.titlePath.push('local mutation');info.suiteTitlePath.push('local mutation')}}],afterAll:[{run:(_,info)=>{assert.deepEqual(info.titlePath,['child','leaf']);assert.deepEqual(info.suiteTitlePath,['child']);observe('after',info)}}]},entries:[{title:'leaf',run:(_,info)=>{assert.deepEqual(info.titlePath,['child','leaf']);observe('body',info)}}]}]});")
assert len(lines)==2
v,lines=run('empty-suite-name-is-actually-pinned-across-retry',"export default defineNativeSuite({entries:[{title:'empty '+process.argv[3],entries:[]},{title:'retry',retries:1,run:(_,info)=>{observe('body',info);throw Error('actual first failure')}}]});",1,globals=('Actual retry worker suite metadata changed','Actual host worker failed during final teardown'))
assert len(v['attempts'])==1 and len(v['workers'])==2 and len(lines)==1
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual all-hook retry/associated ancestry groups')
