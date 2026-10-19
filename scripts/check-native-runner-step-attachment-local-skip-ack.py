# SPDX-License-Identifier: MIT
"""Hold genuine native local-file cause ACK/physical receipt; never invent a result."""
from native_file_markers import read_native_file_marker
import argparse,json,os,pathlib,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--library',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();receipt=a.directory.resolve();receipt.mkdir(parents=True,exist_ok=True);out=pathlib.Path(tempfile.mkdtemp(prefix='cases-',dir=receipt));sdk=a.sdk_directory.resolve();rows=[]
def until(fn,seconds=6):
 end=time.monotonic()+seconds
 while not fn():assert time.monotonic()<end,'actual native ACK/receipt absent';time.sleep(.002)
def state(pid):
 try:return pathlib.Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()[0]
 except FileNotFoundError:return None
proxy_source="import{observeNativeRunnerHost}from '__SUITECUT_OBSERVER_HELPER__';import fs from'node:fs';const s=JSON.parse(fs.readFileSync(new URL('./settings.json',import.meta.url),'utf8'));await observeNativeRunnerHost(s.host,({toHost,toNative})=>{let held=[],blocked=false;const log=(direction,line)=>fs.appendFileSync(s.journal,JSON.stringify({direction,line})+'\\n');const poll=setInterval(()=>{if(blocked&&fs.existsSync(s.release)){blocked=false;for(const line of held)toHost(line);held=[]}},2);return{fromNative(line){log('native',line);if((s.mode==='holdACK'&&line.startsWith('STEP_LOCAL_SKIP_ACK '))||(s.mode==='holdReceipt'&&line.startsWith('STEP_RECEIVED ')))blocked=true;if(blocked){held.push(line);fs.writeFileSync(s.marker,'actual native control held');return}toHost(line);if(s.mode==='holdRequest'&&line.startsWith('STEP_ABORT ')){for(const packet of held)toNative(packet);held=[]}},fromHost(line){log('host',line);if(s.mode==='holdRequest'&&line.startsWith('STEP_LOCAL_SKIP_REQUEST ')){held.push(line);return}toNative(line)},close(){clearInterval(poll)}}});".replace('__SUITECUT_OBSERVER_HELPER__',pathlib.Path(__file__).resolve().with_name('native-runner-observed-host.mjs').as_uri())
for mode in ['holdACK','holdReceipt','holdRequest']:
 root=out/mode;root.mkdir();journal=root/'journal.jsonl';proxy=root/'proxy.mjs';proxy.write_text(proxy_source);(root/'settings.json').write_text(json.dumps({'host':str(sdk/'native-runner-host.js'),'mode':mode,'journal':str(journal),'marker':str(root/'held.marker'),'release':str(root/'release-control')}))
 source="import{defineNativeSuite}from "+json.dumps((sdk/'native.js').as_uri())+";import{strict as assert}from'node:assert';import{existsSync}from'node:fs';import{setTimeout as wait}from'node:timers/promises';export default defineNativeSuite({tests:[{title:'actual authenticated local file cause',run:async(_,i)=>{const work=i.step('source',async s=>{const file=s.attach('canceled',{body:'actual bytes'});while(!existsSync(process.env.SUITECUT_ATTACHMENT_TEST_MARKER))await wait(1);try{s.skip('actual local file cause')}finally{await assert.rejects(file)}},{timeout:"+('300'if mode=='holdRequest'else'4000')+"});"+("await assert.rejects(work,/Step timeout of 300ms exceeded/)"if mode=='holdRequest'else'await work')+"}}]});";suite=root/'suite.mjs';suite.write_text(source)
 env=dict(os.environ,LD_PRELOAD=str(a.library),SUITECUT_ATTACHMENT_TEST_HOLD='file',SUITECUT_ATTACHMENT_TEST_MARKER=str(root/'syscall.marker'),SUITECUT_ATTACHMENT_TEST_RELEASE=str(root/'never-release'))
 process=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(proxy),'--suite',str(suite),'--report',str(root/'actual.json')],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  until(lambda:read_native_file_marker(root/'syscall.marker')or process.poll()is not None);assert process.poll()is None;parts=read_native_file_marker(root/'syscall.marker');assert parts is not None;writer=int(parts[0])
  if mode!='holdRequest':
   until(lambda:(root/'held.marker').exists()or process.poll()is not None);assert process.poll()is None
   if mode=='holdACK':until(lambda:any(r['direction']=='native'and r['line'].startswith('ATTACH_STEP_LOCAL_SKIP_RESULT ')for r in map(json.loads,journal.read_text().splitlines())))
   assert state(writer)is None and not(root/'actual.json').exists();packets=list(map(json.loads,journal.read_text().splitlines()))
   if mode=='holdACK':assert not any(r['direction']=='host'and r['line'].startswith(('STEP_SETTLED_LOCAL_SKIP ','BODY_RESULT '))for r in packets)
   else:assert any(r['direction']=='host'and r['line'].startswith('STEP_SETTLED_LOCAL_SKIP ')for r in packets)
   (root/'release-control').write_text('release the actual ordered native controls')
  stdout,stderr=process.communicate(timeout=10);(root/'stdout.bin').write_bytes(stdout);(root/'stderr.bin').write_bytes(stderr);assert process.returncode==0 and not stderr,(mode,process.returncode,stdout,stderr)
 except BaseException:
  if process.poll()is None:process.terminate()
  stdout,stderr=process.communicate(timeout=6);(root/'failed-stdout.bin').write_bytes(stdout);(root/'failed-stderr.bin').write_bytes(stderr);raise
 v=json.loads((root/'actual.json').read_text());assert state(writer)is None and all(state(w['pid'])is None for w in v['workers'])and not v['attachments']and not v['globalErrors'];packets=list(map(json.loads,journal.read_text().splitlines()));step=v['steps'][0]
 if mode=='holdRequest':assert step['status']=='timedOut'and'stepSkip'not in step and 'Native local-step skip rejected'in step['receiptError']and any(r['direction']=='native'and r['line'].startswith('STEP_LOCAL_SKIP_ACK ')and r['line'].split()[6]=='0'for r in packets)
 else:assert step['status']=='skipped'and step['receiptOK']and step['stepSkip']['requestId']==step['receiptStepSkipRequestId']
 rows.append({'scope':mode,'actualExitCode':process.returncode,'actualWriterPid':writer,'report':str(root/'actual.json'),'genuineNativePacketJournal':str(journal)})
(receipt/'report.json').write_text(json.dumps({'passed':len(rows),'scope':'Genuine native local-step file cancellation controls held over real fd3 without fabricated replies: ACK/ordered-tail hold, final physical receipt hold, and original-deadline late request rejection. Actual syscall writer kill/rollback remains native-owned.','groups':rows},indent=2)+'\n');print('PASS',len(rows),'actual local-step attachment ACK/receipt/deadline groups')
