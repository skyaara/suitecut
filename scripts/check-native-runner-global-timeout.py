# SPDX-License-Identifier: MIT
"""Real native suite deadlines, callback cancellation, and actual exit fences."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--executable',type=Path,required=True)
parser.add_argument('--host-script',type=Path,required=True)
parser.add_argument('--suite-api',type=Path,required=True)
parser.add_argument('--directory',type=Path,required=True)
parser.add_argument('--node',default=shutil.which('node'))
a=parser.parse_args();root=a.directory.resolve();root.mkdir(parents=True,exist_ok=True);groups=[]

def execute(name,body,limit=500,frontend='native',expected_exit=1):
 folder=root/name;folder.mkdir(exist_ok=True);events=folder/'events.jsonl';events.unlink(missing_ok=True)
 prefix='import fs from "node:fs";import process from "node:process";import {defineNativeSuite} from '+json.dumps(a.suite_api.resolve().as_uri())+';const mark=kind=>fs.appendFileSync('+json.dumps(str(events))+',JSON.stringify({kind,pid:process.pid})+"\\n");const wait=info=>new Promise((resolve,reject)=>{if(info.signal.aborted){mark("abort");reject(info.signal.reason);return}info.signal.addEventListener("abort",()=>{mark("abort");reject(info.signal.reason)},{once:true})});'
 suite=folder/'suite.mjs';suite.write_text(prefix+body)
 report=folder/'result.json'
 if frontend=='api':
  launch=folder/'launch.mjs';launch.write_text('import process from "node:process";import {runNativeSuite} from '+json.dumps((a.host_script.parent/'native-runner.js').resolve().as_uri())+';const result=await runNativeSuite('+json.dumps({'executablePath':str(a.executable),'suitePath':str(suite),'reportPath':str(report),'globalTimeout':limit})+');if(result.signal!==null)throw Error(result.signal);process.exitCode=result.exitCode;');cmd=[a.node,str(launch)]
 elif frontend=='cli':cmd=[a.node,str(a.host_script.parent/'native-runner-cli.js'),'--executable',str(a.executable),'--suite',str(suite),'--report',str(report),'--global-timeout',str(limit)]
 else:cmd=[str(a.executable),'--node',a.node,'--host-script',str(a.host_script),'--suite',str(suite),'--report',str(report),'--global-timeout',str(limit)]
 actual=subprocess.run(cmd,capture_output=True,timeout=12);(folder/'stdout').write_bytes(actual.stdout);(folder/'stderr').write_bytes(actual.stderr)
 assert actual.returncode==expected_exit and not actual.stderr,(name,actual.returncode,actual.stderr)
 data=json.loads(report.read_text());observed=[json.loads(x) for x in events.read_text().splitlines()] if events.exists() else []
 for worker in data['workers']:
  assert worker['exitStatusKnown'] is True
  try:os.kill(worker['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('Actual worker survives report',name,worker))
 return folder,data,[x['kind'] for x in observed]

flat='export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{mark("setup");await use(42);mark("teardown")}}},tests:[{title:"actual pending callback",timeout:0,retries:2,run:async(_,info)=>{mark("body");await wait(info)}},{title:"not admitted",run:()=>mark("never")} ]});'
for name,frontend in [('flat-cooperative','native'),('public-cli','cli'),('typed-api','api')]:
 folder,data,events=execute(name,flat,frontend=frontend)
 assert len(data['attempts'])==1 and data['attempts'][0]['status']=='interrupted' and data['attempts'][0]['retry']==0,(name,data)
 assert [x['testId'] for x in data['unrun']]==['2'] and data['summary']['didNotRun']==1,data
 assert data['globalErrors']==['Native suite original global timeout of 500ms exceeded'],data
 assert events==['setup','body','abort','teardown'],(name,events)
 assert data['workers'][0]['exitCode']==0 and data['workers'][0]['signal'] is None,data
 assert data['unrun'][0]['reason']==data['globalErrors'][0]
 groups.append({'name':name,'passed':True,'report':str(folder/'result.json')})

folder,data,events=execute('global-zero-disabled','export default defineNativeSuite({tests:[{title:"actual success",run:()=>mark("body")}]});',limit=0,expected_exit=0)
assert events==['body'] and data['globalErrors']==[] and data['attempts'][0]['status']=='passed',data
groups.append({'name':'global-zero-disabled','passed':True,'report':str(folder/'result.json')})

folder,data,events=execute('original-test-timeout-precedes-suite','export default defineNativeSuite({tests:[{title:"original test timeout",timeout:50,run:async(_,info)=>{mark("body");await wait(info)}},{title:"next remains admitted",run:()=>mark("next")}]});',limit=2000)
assert [x['status'] for x in data['attempts']]==['timedOut','passed'] and data['globalErrors']==[] and events==['body','abort','next'],(data,events)
groups.append({'name':'original-test-timeout-precedes-suite','passed':True,'report':str(folder/'result.json')})

folder,data,events=execute('flat-stubborn-physical-callback','export default defineNativeSuite({tests:[{title:"actual stubborn callback",timeout:0,retries:2,run:()=>{mark("body");return new Promise(()=>{})}},{title:"never admitted",run:()=>mark("never")}]});')
assert events==['body'] and len(data['attempts'])==1 and data['attempts'][0]['status']=='interrupted',data
assert data['workers'][0]['signal']==signal.SIGKILL and data['workers'][0]['exitCode'] is None,data
assert data['globalErrors']==['Native suite original global timeout of 500ms exceeded'] and [x['testId'] for x in data['unrun']]==['2'],data
groups.append({'name':'flat-stubborn-physical-callback','passed':True,'report':str(folder/'result.json')})

folder,data,events=execute('genuine-module-await','mark("module-entered");await new Promise(()=>{});export default defineNativeSuite({tests:[]});')
assert events==['module-entered'] and data['attempts']==[] and data['unrun']==[],data
assert data['globalErrors']==['Native suite original global timeout of 500ms exceeded','Actual host worker failed during final teardown'],data
assert data['workers'][0]['signal']==signal.SIGKILL,data
groups.append({'name':'genuine-module-await','passed':True,'report':str(folder/'result.json')})

for kind in ['beforeAll','afterAll']:
 body='export default defineNativeSuite({entries:[{title:"actual nested scope",hooks:{'+kind+':[{timeout:0,run:async(_,info)=>{mark("'+kind+'");await wait(info)}}]},entries:[{title:"actual test",timeout:0,run:()=>mark("body")}]},{title:"later untouched",run:()=>mark("never")}]});'
 folder,data,events=execute('nested-'+kind,body)
 assert len(data['attempts'])==1 and data['attempts'][0]['status']=='interrupted' and [x['testId'] for x in data['unrun']]==['2'],(kind,data)
 assert data['globalErrors']==['Native suite original global timeout of 500ms exceeded'] and 'abort' in events and 'never' not in events,(kind,data,events)
 assert ('body' in events)==(kind=='afterAll'),events
 assert data['workers'][0]['exitCode']==0 and data['workers'][0]['signal'] is None,data
 groups.append({'name':'nested-'+kind,'passed':True,'report':str(folder/'result.json')})

(root/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print(json.dumps({'passed':len(groups)}))
