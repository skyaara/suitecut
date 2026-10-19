#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Opt-in actual retained Source registration proof; never enables CEF reuse.

Run only with an explicitly pinned compiled binder and matching SDK. Each real
Source/SDK worker/native runner process runs under the supplied sealed native
supervisor. Outputs must use a fresh directory; no completed scope is replayed.
This checker is separate from ordinary CEF-free runner CTest controls.
"""
import argparse,hashlib,json,os,subprocess,sys,traceback,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--epoch',type=Path,required=True)
p.add_argument('--executable',type=Path,required=True)
p.add_argument('--runner-sha256',required=True)
p.add_argument('--sdk-directory',type=Path,required=True)
p.add_argument('--directory',type=Path,required=True)
p.add_argument('--node',default='node')
p.add_argument('--source-root',type=Path)
p.add_argument('--input-manifest',type=Path,action='append',default=[])
a=p.parse_args()
R=a.directory.resolve();R.mkdir(parents=True,exist_ok=False)
E=a.epoch.resolve();S=a.sdk_directory.resolve();executable=a.executable.resolve()
node=shutil.which(a.node);assert node,'Actual Node executable unavailable';N=Path(node).resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((E/'manifest.json').read_bytes())
assert len(m['runtimeAssets'])==230,'Pinned CEF runtime asset inventory differs'
assert sha(executable)==a.runner_sha256,'Compiled native runner identity differs'
inputs={str(E/k):v for k,v in m['sha256'].items()}
inputs.update({str(E/k):v['sha256'] for k,v in m['runtimeAssets'].items()})
inputs[str(E/'manifest.json')]=sha(E/'manifest.json')
for file in [*S.rglob('*'),executable,N,Path(__file__).resolve()]:
 if file.is_file():inputs[str(file)]=sha(file)
# Resolve installed package closure from the actual SDK module, not a guessed
# dist/node_modules path. This is read-only preflight, never a scheduler.
resolution=subprocess.run([str(N),'--input-type=module','-e',"import {createRequire} from 'node:module';const r=createRequire(process.argv[1]);process.stdout.write(JSON.stringify(['zod','yaml'].map(n=>[n,r.resolve(n)])));",str(S/'native-runner-cli.js')],capture_output=True,timeout=10)
(R/'dependency-resolution.json').write_text(json.dumps({'exit':resolution.returncode,'stdout':resolution.stdout.decode(),'stderr':resolution.stderr.decode()},indent=2)+'\n')
assert resolution.returncode==0 and not resolution.stderr,'Actual SDK dependency resolution failed'
for name,entry in json.loads(resolution.stdout):
 package=None
 for parent in Path(entry).resolve().parents:
  metadata=parent/'package.json'
  if metadata.is_file() and json.loads(metadata.read_bytes()).get('name')==name:package=parent;break
 assert package is not None,('Missing resolved dependency root',name)
 for file in package.rglob('*'):
  if file.is_file():inputs[str(file)]=sha(file)
for manifest in a.input_manifest:
 inputs[str(manifest.resolve())]=sha(manifest)
 for key,value in json.loads(manifest.read_bytes()).items():
  file=Path(key)
  if not file.is_absolute():
   assert a.source_root,'Relative input manifest requires --source-root'
   file=a.source_root/file
  assert file.is_file(),('Missing compiled input',str(file))
  inputs[str(file.resolve())]=value
before={key:sha(Path(key)) for key in inputs}
assert before==inputs,'Pinned source/SDK/native/epoch inputs differ before launch'
(R/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
prelude='''import assert from 'node:assert/strict';
import fs from 'node:fs'; import process from 'node:process';
import {defineNativeSuite} from SUITE;
import {launchNativeBrowser} from BROWSER;
const executable=EXE;
const emit=(kind,data={})=>process.stdout.write(JSON.stringify({kind,host:process.pid,...data})+'\\n');
const owned=()=>fs.readdirSync('/proc').filter(s=>/^[0-9]+$/.test(s)).flatMap(s=>{try{const stat=fs.readFileSync('/proc/'+s+'/stat','utf8');const fields=stat.slice(stat.lastIndexOf(')')+2).split(' ');if(Number(fields[1])!==process.pid)return[];const exe=fs.statSync('/proc/'+s+'/exe');const expected=fs.statSync(executable);return exe.dev===expected.dev&&exe.ino===expected.ino?[{pid:Number(s),start:fields[19],parent:Number(fields[1])}]:[]}catch{return[]}});
async function setupBrowser({},use,info){
 let diagnostics='',source;const existing=owned();assert.equal(existing.length,0);
 try {
  source=await launchNativeBrowser({executablePath:executable,headless:true,onDiagnostic:line=>{diagnostics+=line;assert(Buffer.byteLength(diagnostics)<65536)}});
  const children=owned();assert.equal(children.length,1,'Exactly one genuine direct Source browser child');
  const evaluated=await source.sendDevToolsCommand('Runtime.evaluate',{expression:'21*2',returnByValue:true});
  assert.equal(evaluated.result.value,42);
  emit('browserReady',{scope:info.scope,workerIndex:info.workerIndex,child:children[0],value:evaluated.result.value});
  await use({helper:children[0].pid,host:process.pid});
 } finally {
  if(source){await source.close();await source.processClosed;assert.deepEqual(source.processExit(),{code:0,signal:null});emit('browserClosed',{exit:source.processExit()});}
  assert.equal(diagnostics,'');assert.equal(owned().length,0);
 }
}
'''.replace('SUITE',json.dumps((S/'native-suite.js').as_uri())).replace('BROWSER',json.dumps((S/'native-browser.js').as_uri())).replace('EXE',json.dumps(str(E/'runtime/suitecut-browser')))
cases={
 'worker-registration':("export default defineNativeSuite({fixtures:{browser:{scope:'worker',setup:setupBrowser}},tests:[{title:'actual worker Source registration',fixtures:['browser'],timeout:15000,run:({browser},i)=>{assert.equal(browser.host,process.pid);emit('body',{workerIndex:i.workerIndex,helper:browser.helper})}}]});",1,1,1),
 'configured-literal-no-callback':("export default defineNativeSuite({fixtures:{browser:{scope:'worker',option:true,setup:()=>{throw new Error('NATIVE_CONSTANT_EXECUTED_CALLBACK')}}},fixtureOptions:{browser:{configured:true}},tests:[{title:'actual configured callback excluded',fixtures:['browser'],run:({browser})=>{assert.deepEqual(browser,{configured:true});assert.equal(owned().length,0);emit('constantBody')}}]});",1,0,0),
 'declared-literal-no-callback':("export default defineNativeSuite({fixtures:{browser:{scope:'worker',value:{declared:true}}},tests:[{title:'actual literal has no Source setup',fixtures:['browser'],run:({browser})=>{assert.deepEqual(browser,{declared:true});assert.equal(owned().length,0);emit('constantBody')}}]});",1,0,0),
 'test-scope-fresh-browser':("export default defineNativeSuite({fixtures:{browser:{scope:'test',setup:setupBrowser}},tests:[{title:'actual test callback fresh Source',fixtures:['browser'],timeout:15000,run:({browser},i)=>{assert.equal(browser.host,process.pid);emit('body',{workerIndex:i.workerIndex,helper:browser.helper})}}]});",1,0,1),
 'worker-browser-no-reuse':("export default defineNativeSuite({fullyParallel:true,fixtures:{browser:{scope:'worker',setup:setupBrowser}},tests:[1,2].map(n=>({title:'registered CEF child forces fresh host '+n,lock:'one-real-browser',fixtures:['browser'],timeout:15000,run:({browser},i)=>{assert.equal(browser.host,process.pid);emit('body',{workerIndex:i.workerIndex,helper:browser.helper})}}))});",2,2,2),
}
results=[];failure=None
try:
 for name,(body,workers,expected_rows,expected_browsers) in cases.items():
  d=R/name;d.mkdir();suite=d/'suite.mjs';suite.write_text(prelude+body+'\n')
  cmd=[str(E/'runtime/tools/suitecut-proof-supervisor'),'--report',str(d/'supervisor.json'),'--timeout-ms','60000','--cleanup-grace-ms','1000','--physical-ms','6000','--',str(N),str(S/'native-runner-cli.js'),'--executable',str(executable),'--suite',str(suite),'--report',str(d/'result.json'),'--junit',str(d/'result.xml'),'--html',str(d/'result.html'),'--workers',str(workers),'--global-timeout','45000']
  (d/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
  p=subprocess.run(cmd,capture_output=True,timeout=75)
  (d/'stdout').write_bytes(p.stdout);(d/'stderr').write_bytes(p.stderr)
  (d/'process.json').write_text(json.dumps({'returncode':p.returncode,'stdoutBytes':len(p.stdout),'stderrBytes':len(p.stderr)},indent=2)+'\n')
  assert p.returncode==0 and not p.stderr,(name,p.returncode,p.stderr.decode())
  report=json.loads((d/'result.json').read_bytes());supervisor=json.loads((d/'supervisor.json').read_bytes())
  assert supervisor['childExit']=={'signaled':False,'code':0,'rawWaitpidStatus':0},supervisor
  assert not supervisor['cleanupFailed'] and supervisor['descendantsRetired'] and not supervisor['timeout'] and supervisor['interruption']==0 and supervisor['error']=='',supervisor
  assert not Path('/proc',str(supervisor['childPID'])).exists(),supervisor
  assert not report['globalErrors'],report
  assert report['attempts'] and all(a['status']=='passed' and a['expected'] for a in report['attempts']),report
  ws=report['workers'];assert all(w['exitCode']==0 and w['signal'] is None for w in ws),ws
  assert all(not Path('/proc',str(w['pid'])).exists() for w in ws),ws
  logs=[]
  for w in ws:
   assert w['stderr']['data']=='',w
   for line in bytes.fromhex(w['stdout']['data']).decode().splitlines():
    logs.append(json.loads(line))
  ready=[x for x in logs if x['kind']=='browserReady'];closed=[x for x in logs if x['kind']=='browserClosed'];rows=report.get('retainedSources',[])
  assert len(rows)==expected_rows,(name,rows)
  assert len(ready)==len(closed)==expected_browsers,(name,logs)
  assert all(not Path('/proc',str(x['child']['pid'])).exists() for x in ready),ready
  if expected_rows:
   for row in rows:
    actual=next(x for x in ready if x['host']==row['hostPID'])
    assert row['startup']=={'receipt':'credentialedKernelStartup','pid':actual['child']['pid'],'parentPID':actual['child']['parent'],'start':actual['child']['start']},(row,actual)
    assert row['channelDrained'] and row['pidfdExitObserved'] and row['physicalRetirement']['receipt']=='hostWaitpidAndDescendantBatchRetired',row
    assert row['shutdownBoundary'] and row['shutdownBoundary']['receipt']=='shutdownReachedLabel' and row['shutdownBoundary']['plannedExitCode']==0,row
    assert int(row['fixtureToken'])>0 and int(row['declarationId'])>0 and int(row['catalogId'])>0,row
  if name=='worker-browser-no-reuse':assert len({x['host'] for x in ready})==2 and len({x['child']['pid'] for x in ready})==2,logs
  results.append({'name':name,'returncode':p.returncode,'browsers':len(ready),'registrations':len(rows),'workers':[w['pid'] for w in ws]})
  print('PASS '+name,flush=True)
except BaseException as error:
 failure=repr(error);(R/'first-failure.txt').write_text(traceback.format_exc());print('FAIL '+failure,flush=True)
finally:
 after={k:sha(Path(k)) for k in inputs};(R/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n');assert before==after,'Inputs changed'
 (R/'summary.json').write_text(json.dumps({'scope':'Actual native binder + explicitly supplied SDK and sealed browser development pair; registration only; no Engine accepted-work/CEF reuse claim','scopes':results,'firstFailure':failure,'inputsUnchanged':before==after,'epochFiles':len(m['sha256']),'runtimeAssets':len(m['runtimeAssets'])},indent=2)+'\n')
if failure:sys.exit(1)
print('PASS all actual binder scopes; physical children closed',flush=True)
