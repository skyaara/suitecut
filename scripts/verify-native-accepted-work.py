#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual native Engine work barrier proof. CEF reuse intentionally remains off.

Supply the compiled private/native production paths explicitly. Original SDK
NativeBrowser stdin/stdout and real process-channel receipts are never replaced.
"""
import argparse,hashlib,json,os,subprocess,sys,traceback
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('runner','browser','sdk','supervisor','directory'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--input-manifest',type=Path,action='append',default=[])
p.add_argument('--node',default='node')
p.add_argument('--case',choices=['resident-timer','pending-command','test-watchdog','beforeAll','afterAll'],default='resident-timer')
a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(f.resolve()):sha(f) for f in (a.runner,a.browser,a.supervisor,Path(__file__))}
for file in a.sdk.glob('*.js'):inputs[str(file.resolve())]=sha(file)
for manifest in a.input_manifest:
 inputs[str(manifest.resolve())]=sha(manifest)
 for k,v in json.loads(manifest.read_bytes()).items():
  assert Path(k).is_absolute(),k
  inputs[k]=v if isinstance(v,str)else v['sha256']
assert all(Path(k).is_file()and sha(Path(k))==v for k,v in inputs.items()),'actual compiled input drift before launch'
(out/'inputs-before.json').write_text(json.dumps(inputs,indent=2)+'\n')
prelude="""import assert from 'node:assert/strict';
import process from 'node:process';
import {defineNativeSuite} from SUITE;
import {launchNativeBrowser} from BROWSER;
import {NativeBrowserContexts} from CONTEXT;
let held;
const emit=(kind,data={})=>console.log(JSON.stringify({kind,host:process.pid,...data}));
async function setup({},use,info){
 let diagnostics='';const browser=await launchNativeBrowser({executablePath:EXE,headless:true,onDiagnostic:s=>{diagnostics+=s;emit('diagnostic',{line:s});assert(diagnostics.length<65536)}});
 try{emit('ready',{workerIndex:info.workerIndex,scope:info.scope});await use(browser)}
 finally{await browser.close();await browser.processClosed;assert.deepEqual(browser.processExit(),{code:0,signal:null});if(held)await held;assert.equal(diagnostics,'');emit('closed')}
}
""".replace('SUITE',json.dumps((a.sdk/'native-suite.js').resolve().as_uri())).replace('BROWSER',json.dumps((a.sdk/'native-browser.js').resolve().as_uri())).replace('CONTEXT',json.dumps((a.sdk/'native-context.js').resolve().as_uri())).replace('EXE',json.dumps(str(a.browser.resolve())))
if a.case in ('resident-timer','pending-command','test-watchdog'):
 body="""export default defineNativeSuite({fullyParallel:true,fixtures:{browser:{scope:'worker',setup}},tests:[1,2,3].map(n=>({title:'real worker browser '+n,fixtures:['browser'],timeout:15000,run:async({browser},info)=>{const result=await browser.sendDevToolsCommand('Runtime.evaluate',{expression:'globalThis.__residentTimer=setInterval(()=>{},10000);42',returnByValue:true});assert.equal(result.result.value,42);emit('body',{n,workerIndex:info.workerIndex,value:result.result.value})}}))});"""
 if a.case=='pending-command':
  body=body.replace("const result=await browser.sendDevToolsCommand('Runtime.evaluate',{expression:'globalThis.__residentTimer=setInterval(()=>{},10000);42',returnByValue:true});assert.equal(result.result.value,42);emit('body',{n,workerIndex:info.workerIndex,value:result.result.value})", "held=browser.sendDevToolsCommand('Runtime.evaluate',{expression:'new Promise(()=>{})',awaitPromise:true,returnByValue:true}).then(()=>{throw Error('held native command unexpectedly settled')},error=>{assert.equal(error.message,'Native browser is closing');emit('pendingRetired',{error:error.message})});const actual=await browser.runAutomation('diagnostics');assert(actual.nativeAcceptedInspectorSubmitted>0);assert.equal(actual.nativeAcceptedInspectorReceiptLaneReady,false);emit('body',{n,workerIndex:info.workerIndex,submitted:actual.nativeAcceptedInspectorSubmitted})")
 if a.case=='test-watchdog':
  body=body.replace("const result=await browser.sendDevToolsCommand('Runtime.evaluate',{expression:'globalThis.__residentTimer=setInterval(()=>{},10000);42',returnByValue:true});assert.equal(result.result.value,42);emit('body',{n,workerIndex:info.workerIndex,value:result.result.value})", "const contexts=new NativeBrowserContexts(browser);const page=await contexts.newPage();await page.close();emit('body',{n,workerIndex:info.workerIndex,actualPageClosed:true})")
else:
 body="export default defineNativeSuite({fixtures:{browser:{scope:'worker',setup}},hooks:{"+a.case+":[{fixtures:['browser'],timeout:15000,run:async({browser},info)=>{assert.equal(info.hook,"+json.dumps(a.case)+");const result=await browser.sendDevToolsCommand('Runtime.evaluate',{expression:'42',returnByValue:true});assert.equal(result.result.value,42);emit('hook',{hook:info.hook})}}]},tests:[{title:'genuine hook-only worker fixture',run:()=>emit('body')}]});"
suite=out/'suite.mjs';suite.write_text(prelude+body+'\n');inputs[str(suite)]=sha(suite)
command=[str(a.supervisor.resolve()),'--report',str(out/'supervisor.json'),'--timeout-ms','60000','--cleanup-grace-ms','1000','--physical-ms','6000','--',a.node,str((a.sdk/'native-runner-cli.js').resolve()),'--executable',str(a.runner.resolve()),'--suite',str(suite),'--report',str(out/'result.json'),'--workers','2' if a.case in ('resident-timer','pending-command','test-watchdog') else '1','--global-timeout','45000']
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n');failure=None
try:
 result=subprocess.run(command,capture_output=True,timeout=75);(out/'stdout').write_bytes(result.stdout);(out/'stderr').write_bytes(result.stderr);(out/'process.json').write_text(json.dumps({'exit':result.returncode,'stderrBytes':len(result.stderr)},indent=2)+'\n')
 assert result.returncode==0 and not result.stderr,(result.returncode,result.stderr.decode())
 report=json.loads((out/'result.json').read_bytes());receipt=json.loads((out/'supervisor.json').read_bytes())
 assert receipt['childExit']=={'signaled':False,'code':0,'rawWaitpidStatus':0}and receipt['descendantsRetired']and not any(receipt[k]for k in ('cleanupFailed','timeout','interruption','error')),receipt
 assert not report['globalErrors']and all(t['status']=='passed'and t['expected']for t in report['attempts']),report
 assert all(w['exitCode']==0 and w['signal']is None and w['stderr']['data']==''and not Path('/proc',str(w['pid'])).exists()for w in report['workers']),report['workers']
 rows=report['retainedSources'];assert rows,report
 assert all(r['pidfdExitObserved']and r['channelDrained']and r['physicalRetirement']['receipt']=='hostWaitpidAndDescendantBatchRetired'and r['shutdownBoundary']['plannedExitCode']==0 and not Path('/proc',str(r['startup']['pid'])).exists()for r in rows),rows
 if a.case in ('resident-timer','pending-command','test-watchdog'):
  assert len(rows)==3 and len({r['hostPID']for r in rows})==3,rows
  work=[r['acceptedWork']for r in rows if 'acceptedWork'in r];assert work,rows
  if a.case=='resident-timer':
   acknowledged=[w for w in work if w['acknowledged']];assert acknowledged,rows
   for w in acknowledged:
    assert w['receipt']=='originalStdinAndDecodedStdout'and w['cuts']and w['decoderObserved']and not w['lateAcknowledged']and w['refusal']=='0',w
    assert 0<=w['observedMs']<=w['acknowledgedMs']<w['originalDeadlineMs'],w
  else:
   assert all(not w['acknowledged']and not w['decoderObserved']and w['cuts']is None for w in work),work
   assert all(w['canceled']or w['refusal']=='1'for w in work),work
  assert all(g['receipt']=='physicalRetired'for g in report['workerReuse']['borrows']),report['workerReuse']
 else:
  assert len(rows)==1 and rows[0]['associatedTestId']=='0'and rows[0]['callbackScope']==a.case and int(rows[0]['callbackOwner'])>0,rows
 print('PASS actual native '+a.case+' barrier/registration; all owned processes physically retired',flush=True)
except BaseException as error:
 failure=repr(error);(out/'first-failure.txt').write_text(traceback.format_exc());print('FAIL '+failure,flush=True)
finally:
 after={k:sha(Path(k))for k in inputs};(out/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n');assert inputs==after,'actual compiled input drift during proof'
 (out/'summary.json').write_text(json.dumps({'case':a.case,'firstFailure':failure,'inputsUnchanged':inputs==after,'scope':'Actual original-stdin/stdout/native process-channel barrier; CEF cohort reuse remains disabled; explicit private compiled development pair'},indent=2)+'\n')
if failure:sys.exit(1)
