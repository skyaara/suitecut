# SPDX-License-Identifier: MIT
"""Actual native focus admission and independent process/report observations."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--executable',type=Path,required=True)
p.add_argument('--host-script',type=Path,required=True)
p.add_argument('--suite-api',type=Path,required=True)
p.add_argument('--directory',type=Path,required=True)
p.add_argument('--node',default=shutil.which('node'))
a=p.parse_args();root=a.directory.resolve();root.mkdir(parents=True,exist_ok=True);groups=[]

def execute(name,extra,bodies,skipped,forbidden=False,frontend='native',focused=True):
 folder=root/name;folder.mkdir(exist_ok=True);events=folder/'events.jsonl';events.unlink(missing_ok=True)
 definitions=[{'title':'unfocused first','only':False},{'title':'focused first','only':True},{'title':'focused declared skip','only':True,'skip':'Actual selected skip'},{'title':'focused actual retry','only':True,'retries':1},{'title':'unfocused last'}]
 if not focused:
  for definition in definitions:definition.pop('only',None)
 suite=folder/'suite.mjs';suite.write_text('import fs from "node:fs";import process from "node:process";import {defineNativeSuite} from '+json.dumps(a.suite_api.resolve().as_uri())+';const mark=(kind,info)=>fs.appendFileSync('+json.dumps(str(events))+',JSON.stringify({kind,id:info.testId??null,pid:process.pid})+"\\n");export default defineNativeSuite({fixtures:{automatic:{automatic:true,setup:async({},use,info)=>{mark("setup",info);await use(42);mark("teardown",info)}}},hooks:{beforeAll:[{run:async(_,info)=>mark("beforeAll",info)}],afterAll:[{run:async(_,info)=>mark("afterAll",info)}]},tests:'+json.dumps(definitions)+'.map(test=>({...test,run:async(_,info)=>{mark("body",info);if(info.testId==="4"&&info.retry===0)throw Error("Actual focused retry");}}))});')
 report=folder/'result.json'
 if frontend=='native':command=[str(a.executable),'--node',a.node,'--host-script',str(a.host_script)]
 elif frontend=='cli':command=[a.node,str(a.host_script.parent/'native-runner-cli.js'),'--executable',str(a.executable)]
 else:
  launcher=folder/'launch.mjs';launcher.write_text('import process from "node:process";import {runNativeSuite} from '+json.dumps((a.host_script.parent/'native-runner.js').resolve().as_uri())+';const actual=await runNativeSuite('+json.dumps({'executablePath':str(a.executable),'suitePath':str(suite),'reportPath':str(report),'forbidOnly':True})+');if(actual.signal!==null)throw Error(actual.signal);process.exitCode=actual.exitCode;');command=[a.node,str(launcher)]
 if frontend!='api':command+=['--suite',str(suite),'--report',str(report)]+extra
 actual=subprocess.run(command,capture_output=True,timeout=15);(folder/'stdout').write_bytes(actual.stdout);(folder/'stderr').write_bytes(actual.stderr)
 assert actual.returncode==(1 if forbidden else 0),(name,actual.returncode,actual.stderr)
 assert not actual.stderr,(name,actual.stderr)
 data=json.loads(report.read_text());assert [x['testId'] for x in data['attempts']]==bodies,(name,data)
 assert [x['testId'] for x in data['skipped']]==skipped,(name,data)
 assert data['unrun']==[],data
 observed=[json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
 assert [x['id'] for x in observed if x['kind']=='body']==bodies,(name,observed)
 if not bodies:assert observed==[],observed
 if forbidden:assert any('Focused native test declarations are forbidden' in e for e in data['globalErrors']),data
 else:assert data['globalErrors']==[],data
 retries=[x for x in data['attempts'] if x['testId']=='4']
 if retries:
  assert [x['status'] for x in retries]==['failed','passed']
  assert retries[0]['pid']!=retries[1]['pid']
 for worker in data['workers']:
  assert worker['exitStatusKnown'] is True
  try:os.kill(worker['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('Actual native worker survives report',worker))
 groups.append({'name':name,'passed':True,'report':str(report)})

execute('focus-original-order-real-retry',[],['2','4','4'],['3'])
execute('focus-before-balanced-shard',['--shard','1/2'],['2'],['3'])
execute('focus-last-shard-real-retry',['--shard','2/2'],['4','4'],[])
execute('exact-filter-intersection',['--test-id','5','--test-id','1','--test-id','2'],['2'],[])
execute('unfocused-known-id-no-admission',['--test-id','1','--pass-with-no-tests'],[],[])
execute('forbid-native-before-admission',['--forbid-only'],[],[],True)
execute('forbid-even-with-filter',['--test-id','1','--forbid-only'],[],[],True)
execute('forbid-cli',['--forbid-only'],[],[],True,'cli')
execute('forbid-typed-api',[],[],[],True,'api')
execute('forbid-no-focus-runs',['--forbid-only'],['1','2','4','4','5'],['3'],focused=False)
(root/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print(json.dumps({'passed':len(groups)}))
