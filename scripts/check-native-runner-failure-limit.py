# SPDX-License-Identifier: MIT
"""Real native admission limits after retries, with independent report decoding."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser()
p.add_argument('--executable',type=Path,required=True)
p.add_argument('--host-script',type=Path,required=True)
p.add_argument('--suite-api',type=Path,required=True)
p.add_argument('--directory',type=Path,required=True)
p.add_argument('--node',default=shutil.which('node'))
a=p.parse_args();root=a.directory.resolve();root.mkdir(parents=True,exist_ok=True);groups=[]

def retired(data):
 for worker in data['workers']:
  assert worker['exitStatusKnown'] is True
  try:os.kill(worker['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('Actual worker survives report',worker))

def execute(name,limit,bodies,unrun,mode='fail',hooks=False,frontend='native',skip=False):
 folder=root/name;folder.mkdir(exist_ok=True);events=folder/'events.jsonl';events.unlink(missing_ok=True)
 definitions=[{'title':'first actual test','mode':mode,'retries':1},{'title':'second actual test','mode':'pass'},{'title':'third actual failure','mode':'fail'},{'title':'last actual test','mode':'pass'}]
 if mode in ['expected-fail','unexpected-pass']:definitions[0]['expectedFailure']=True
 if skip:definitions[1]['skip']='Actual declaration skip'
 suite=folder/'suite.mjs';suite.write_text('import fs from "node:fs";import process from "node:process";import {defineNativeSuite} from '+json.dumps(a.suite_api.resolve().as_uri())+';const mark=(kind,info)=>fs.appendFileSync('+json.dumps(str(events))+',JSON.stringify({kind,id:info.testId??null,pid:process.pid})+"\\n");export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async({},use,info)=>{mark("setup",info);await use(42);mark("teardown",info)}}},'+('hooks:{beforeAll:[{run:async(_,info)=>mark("beforeAll",info)}],afterEach:[{run:async(_,info)=>mark("afterEach",info)}],afterAll:[{run:async(_,info)=>mark("afterAll",info)}]},' if hooks else '')+'tests:'+json.dumps(definitions)+'.map(({mode,...test})=>({...test,run:async(_,info)=>{mark("body",info);if(mode==="fail"||mode==="expected-fail"||mode==="flaky"&&info.retry===0)throw Error("Actual declared failure");}}))});')
 report=folder/'result.json';junit=folder/'result.xml';html=folder/'result.html'
 if frontend=='native':command=[str(a.executable),'--node',a.node,'--host-script',str(a.host_script)]
 elif frontend=='cli':command=[a.node,str(a.host_script.parent/'native-runner-cli.js'),'--executable',str(a.executable)]
 else:
  launcher=folder/'launch.mjs';launcher.write_text('import process from "node:process";import {runNativeSuite} from '+json.dumps((a.host_script.parent/'native-runner.js').resolve().as_uri())+';const result=await runNativeSuite('+json.dumps({'executablePath':str(a.executable),'suitePath':str(suite),'reportPath':str(report),'junitPath':str(junit),'htmlPath':str(html),'maxFailures':limit})+');if(result.signal!==null)throw Error(result.signal);process.exitCode=result.exitCode;');command=[a.node,str(launcher)]
 if frontend!='api':command+=['--suite',str(suite),'--report',str(report),'--junit',str(junit),'--html',str(html),'--max-failures',str(limit)]
 actual=subprocess.run(command,capture_output=True,timeout=20);(folder/'stdout').write_bytes(actual.stdout);(folder/'stderr').write_bytes(actual.stderr)
 assert actual.returncode==1 and actual.stderr==b'',(name,actual.returncode,actual.stderr)
 data=json.loads(report.read_text());assert [x['testId'] for x in data['attempts']]==bodies,(name,data)
 assert [x['testId'] for x in data['unrun']]==unrun,(name,data)
 assert data['summary']['didNotRun']==len(unrun) and data['summary']['tests']==4,data
 assert [x['testId'] for x in data['skipped']]==(['2'] if skip else []),data
 assert data['globalErrors']==[],data
 for record in data['unrun']:
  assert set(record)=={'testId','title','reason'} and record['reason']=='Native maximum failure count reached'
 observed=[json.loads(line) for line in events.read_text().splitlines()]
 assert [x['id'] for x in observed if x['kind']=='body']==bodies,(name,observed)
 assert not any(x['id'] in unrun for x in observed),observed
 first=[x for x in data['attempts'] if x['testId']=='1']
 if mode in ['fail','flaky','unexpected-pass']:
  assert len(first)==2 and first[0]['pid']!=first[1]['pid'],first
 if mode=='flaky':assert data['summary']['flaky']==1
 if mode=='expected-fail':assert len(first)==1 and first[0]['expected'] is True
 declared=ET.parse(junit).findall('.//testcase')
 unrun_xml=[case for case in declared if case.find('skipped[@type="didNotRun"]') is not None]
 assert len(unrun_xml)==len(unrun) and all('time' not in case.attrib for case in unrun_xml)
 assert html.is_file();retired(data)
 groups.append({'name':name,'passed':True,'report':str(report)})

execute('final-after-all-retries',1,['1','1'],['2','3','4'])
execute('unlimited-zero',0,['1','1','2','3','4'],[])
execute('two-final-failures',2,['1','1','2','3'],['4'])
execute('flaky-does-not-consume-limit',1,['1','1','2','3'],['4'],mode='flaky')
execute('expected-failure-does-not-consume-limit',1,['1','2','3'],['4'],mode='expected-fail')
execute('unexpected-pass-consumes-after-retries',1,['1','1'],['2','3','4'],mode='unexpected-pass')
execute('hooks-clean-before-stop',1,['1','1'],['2','3','4'],hooks=True)
execute('declared-skips-distinct-from-unrun',1,['1','1'],['3','4'],skip=True)
execute('public-cli',1,['1','1'],['2','3','4'],frontend='cli')
execute('typed-api',1,['1','1'],['2','3','4'],frontend='api')

# Real interruption also records declarations that never entered a test scope,
# including flat suites without hooks. Readiness is from this actual worker.
folder=root/'flat-sigint-unrun';folder.mkdir(exist_ok=True);marker=folder/'started';marker.unlink(missing_ok=True)
suite=folder/'suite.mjs';suite.write_text('import fs from "node:fs";import process from "node:process";import {defineNativeSuite} from '+json.dumps(a.suite_api.resolve().as_uri())+';export default defineNativeSuite({tests:[{title:"actual interrupted test",timeout:0,run:async(_,info)=>{fs.writeFileSync('+json.dumps(str(marker))+',String(process.pid));await new Promise((resolve,reject)=>info.signal.addEventListener("abort",()=>reject(info.signal.reason),{once:true}))}},{title:"never admitted",run:()=>{throw Error("Must not run")}}]});')
actual=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(a.host_script),'--suite',str(suite),'--report',str(folder/'result.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
deadline=time.monotonic()+5
while not marker.exists():
 assert actual.poll() is None and time.monotonic()<deadline
 time.sleep(0.005)
os.kill(actual.pid,signal.SIGINT);stdout,stderr=actual.communicate(timeout=6)
(folder/'stdout').write_bytes(stdout);(folder/'stderr').write_bytes(stderr)
assert actual.returncode==130 and not stderr,(actual.returncode,stderr)
data=json.loads((folder/'result.json').read_text());assert data['attempts'][0]['status']=='interrupted'
assert data['workers'][0]['pid']==int(marker.read_text())
assert data['summary']['tests']==2 and data['summary']['didNotRun']==1
assert [x['testId'] for x in data['unrun']]==['2'];retired(data)
groups.append({'name':'flat-sigint-unrun','passed':True,'report':str(folder/'result.json')})
(root/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print(json.dumps({'passed':len(groups)}))
