# SPDX-License-Identifier: MIT
"""Real native nested selection, callback admission and process retirement."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--executable',type=Path,required=True)
p.add_argument('--sdk-directory',type=Path,required=True)
p.add_argument('--node',required=True)
p.add_argument('--directory',type=Path,required=True)
a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);groups=[]
prelude="import assert from 'node:assert/strict';import process from 'node:process';import fs from 'node:fs';import {defineNativeSuite} from "+json.dumps((a.sdk_directory.resolve()/'native-suite.js').as_uri())+";const mark=value=>()=>console.log(value);const forbidden=()=>{throw Error('Excluded callback was admitted')};\n"
def run(name,source,ids=(),skips=(),options=(),code=0,global_fragment=None):
 d=out/name;d.mkdir(exist_ok=True);suite=d/'suite.mjs';suite.write_text(prelude+source);report=d/'report.json'
 actual=subprocess.run([str(a.executable.resolve()),'--node',a.node,'--host-script',str(a.sdk_directory.resolve()/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),*options],capture_output=True,timeout=15)
 (d/'stdout.bin').write_bytes(actual.stdout);(d/'stderr.bin').write_bytes(actual.stderr)
 assert actual.returncode==code,(name,actual.returncode,actual.stdout,actual.stderr)
 assert not actual.stderr,(name,actual.stderr)
 v=json.loads(report.read_text());assert [x['testId'] for x in v['attempts']]==list(ids),(name,v)
 assert [x['testId'] for x in v['skipped']]==list(skips),(name,v)
 assert not v['unrun'],(name,v)
 if global_fragment:assert any(global_fragment in x for x in v['globalErrors']),(name,v)
 else:assert not v['globalErrors'],(name,v)
 for worker in v['workers']:
  assert worker['exitStatusKnown'] and not bytes.fromhex(worker['stderr']['data']),(name,worker)
  try:os.kill(worker['pid'],0)
  except ProcessLookupError:pass
  else:raise AssertionError(('Native worker survived publication',worker['pid']))
 groups.append({'name':name,'report':str(report),'exitCode':actual.returncode})
 return v,b''.join(bytes.fromhex(w['stdout']['data']) for w in v['workers']).decode()

v,text=run('suite-static-skip-no-fixtures-or-hooks',"export default defineNativeSuite({fixtures:{auto:{automatic:true,setup:async(_,use)=>{console.log('setup');await use(1);console.log('release')}}},entries:[{title:'skipped',skip:'actual inherited reason',hooks:{beforeAll:[{run:forbidden}],afterAll:[{run:forbidden}]},entries:[{title:'first',run:forbidden},{title:'child',entries:[{title:'second',skip:false,run:forbidden}]}]},{title:'outside',run:mark('outside')}]});",('3',),('1','2'))
assert text=='setup\noutside\nrelease\n',text
assert [s['reason'] for s in v['skipped']]==['actual inherited reason']*2
v,text=run('nearest-reason-and-empty-string',"export default defineNativeSuite({entries:[{title:'outer',skip:'outer',entries:[{title:'one',skip:'test',run:forbidden},{title:'inner',skip:'',entries:[{title:'two',skip:false,run:forbidden}]},{title:'three',run:forbidden}]}]});",skips=('1','2','3'))
assert [s['reason'] for s in v['skipped']]==['test','','outer'] and text==''
v,text=run('focused-suite-selected-skips',"export default defineNativeSuite({entries:[{title:'unfocused',hooks:{beforeAll:[{run:forbidden}]},entries:[{title:'one',run:forbidden}]},{title:'focused',only:true,hooks:{beforeAll:[{run:mark('before')}],afterAll:[{run:mark('after')}]},entries:[{title:'two',run:mark('two')},{title:'skipped',skip:true,run:forbidden},{title:'three',run:mark('three')}]},{title:'last',run:forbidden}]});",('2','4'),('3',))
assert text=='before\ntwo\nthree\nafter\n',text
source="export default defineNativeSuite({entries:[{title:'outer',only:true,entries:[{title:'excluded',run:forbidden},{title:'focused test',only:true,run:mark('inner only')},{title:'excluded child',entries:[{title:'excluded',run:forbidden}]}]},{title:'other focused',only:true,entries:[{title:'other',run:mark('other')}]},{title:'excluded root',run:forbidden}]});"
v,text=run('test-focus-narrows-focused-parent',source,('2','4'));assert text=='inner only\nother\n',text
v,text=run('focus-before-exact-id-filter',source,('4',),options=('--test-id','1','--test-id','4'));assert text=='other\n'
v,text=run('focus-before-balanced-shard',source,('4',),options=('--shard','2/2'));assert text=='other\n'
v,text=run('nested-suite-focus-narrows-parent',"export default defineNativeSuite({entries:[{title:'outer',only:true,entries:[{title:'excluded',run:forbidden},{title:'inner',only:true,entries:[{title:'one',run:mark('one')},{title:'deeper',entries:[{title:'two',run:mark('two')}]}]}]},{title:'excluded root',run:forbidden}]});",('2','3'));assert text=='one\ntwo\n'
empty="export default defineNativeSuite({fixtures:{auto:{automatic:true,setup:forbidden}},hooks:{beforeAll:[{run:forbidden}],afterAll:[{run:forbidden}]},entries:[{title:'excluded',run:forbidden},{title:'empty focused suite',only:true,entries:[]}]});"
v,text=run('empty-focus-selects-no-tests',empty,options=('--pass-with-no-tests',));assert text==''
v,text=run('forbid-empty-suite-focus-before-admission',empty,options=('--forbid-only',),code=1,global_fragment='Focused native test declarations are forbidden');assert text==''
v,text=run('skip-and-focus-still-forbidden',"export default defineNativeSuite({entries:[{title:'both',only:true,skip:true,entries:[{title:'never',run:forbidden}]}]});",options=('--forbid-only',),code=1,global_fragment='Focused native test declarations are forbidden');assert text==''
v,text=run('retry-retains-native-suite-modifiers',"export default defineNativeSuite({entries:[{title:'focused',only:true,hooks:{beforeAll:[{run:mark('before')}],afterAll:[{run:mark('after')}]},entries:[{title:'retry',retries:1,run:(_,info)=>{console.log('try '+info.retry);if(!info.retry)throw Error('actual retry failure')}},{title:'static skipped',skip:'still skipped',run:forbidden}]},{title:'excluded',run:forbidden}]});",('1','1'),('2',));assert text=='before\ntry 0\nafter\nbefore\ntry 1\nafter\n' and len(v['workers'])==2 and v['attempts'][0]['pid']!=v['attempts'][1]['pid']
# An unselected modifier is still retry metadata. Do not execute a changed tree.
v,text=run('retry-rejects-changed-suite-modifier',"export default defineNativeSuite({entries:[{title:'actual',entries:[{title:'retry',retries:1,run:()=>{throw Error('first actual failure')}}]},{title:'empty unselected',skip:process.argv[3]==='1'?'before':'after',entries:[]}]});",('1',),code=1,global_fragment='Actual retry worker suite metadata changed');assert len(v['workers'])==2
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native suite modifier groups')
