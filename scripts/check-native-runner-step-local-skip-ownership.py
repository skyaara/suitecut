# SPDX-License-Identifier: MIT
"""Delay only genuine native local-step controls/receipts over real fd3."""
import argparse,pathlib,json,subprocess,time,os
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--sdk-directory',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk_directory.resolve();groups=[]
proxy_source="import{observeNativeRunnerHost}from '__SUITECUT_OBSERVER_HELPER__';import fs from'node:fs';import process from'node:process';const settings=JSON.parse(fs.readFileSync(new URL('./settings.json',import.meta.url),'utf8'));await observeNativeRunnerHost(settings.host,({toHost,toNative})=>{const host={write:toHost},native={write:toNative};let a='',b='',held=[];const log=(direction,line)=>fs.appendFileSync(settings.journal,JSON.stringify({direction,line})+'\\n');\nconst parentLine=line=>{log('native',line);if((settings.mode==='holdACK'&&line.startsWith('STEP_LOCAL_SKIP_ACK '))||(settings.mode==='holdReceipt'&&line.startsWith('STEP_RECEIVED '))){held.push(line);fs.writeFileSync(settings.marker,'actual native ACK held');return}host.write(line+'\\n');if(settings.mode==='holdRequest'&&line.startsWith('STEP_ABORT ')){for(const packet of held)native.write(packet+'\\n');held=[];}};\nconst childLine=line=>{log('host',line);if(settings.mode==='holdRequest'&&line.startsWith('STEP_LOCAL_SKIP_REQUEST ')){held.push(line);return}native.write(line+'\\n');};\nconst poll=setInterval(()=>{if((settings.mode==='holdACK'||settings.mode==='holdReceipt')&&held.length&&fs.existsSync(settings.release)){for(const line of held)host.write(line+'\\n');held=[];}},5);return{fromNative:parentLine,fromHost:childLine,close(){clearInterval(poll)}}});".replace('__SUITECUT_OBSERVER_HELPER__',pathlib.Path(__file__).resolve().with_name('native-runner-observed-host.mjs').as_uri())
for mode in ['holdACK','holdReceipt','holdRequest']:
 d=out/mode;d.mkdir(exist_ok=True);journal=d/'journal.jsonl';journal.unlink(missing_ok=True);marker=d/'marker';marker.unlink(missing_ok=True);release=d/'release';release.unlink(missing_ok=True);proxy=d/'proxy.mjs';proxy.write_text(proxy_source);(d/'settings.json').write_text(json.dumps({'host':str(sdk/'native-runner-host.js'),'mode':mode,'journal':str(journal),'marker':str(marker),'release':str(release)}));suite=d/'suite.mjs'
 suite.write_text("import assert from 'node:assert/strict';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";export default defineNativeSuite({tests:[{title:'actual native local skip',run:async(_,i)=>{await i.step('actual step',s=>s.skip('real local reason'),{timeout:"+('100' if mode=='holdRequest' else '4000')+"});console.log('actual body continued')}}]});")
 report=d/'report.json';report.unlink(missing_ok=True);child=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(proxy),'--suite',str(suite),'--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  if mode in ['holdACK','holdReceipt']:
   end=time.monotonic()+5
   while not marker.exists():
    assert child.poll() is None and time.monotonic()<end;time.sleep(.005)
   time.sleep(.15);rows=[json.loads(x) for x in journal.read_text().splitlines()];assert mode!='holdACK' or not any(x['direction']=='host' and x['line'].startswith(('BODY_RESULT ', 'STEP_SETTLED_LOCAL_SKIP ')) for x in rows),rows;assert child.poll() is None and not report.exists();release.write_text('release actual native ACK')
  stdout,stderr=child.communicate(timeout=8);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);assert not stderr,stderr;assert child.returncode==(0 if mode!='holdRequest' else 1),(child.returncode,stdout,stderr)
 except BaseException:
  if child.poll() is None:child.terminate()
  stdout,stderr=child.communicate(timeout=5);(d/'failed-stdout.bin').write_bytes(stdout);(d/'failed-stderr.bin').write_bytes(stderr);raise
 v=json.loads(report.read_text());rows=[json.loads(x) for x in journal.read_text().splitlines()];assert v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'] and not bytes.fromhex(v['workers'][0]['stderr']['data']),v
 if mode in ['holdACK','holdReceipt']:
  assert v['attempts'][0]['status']=='passed' and v['steps'][0]['status']=='skipped' and v['steps'][0]['receiptOK'] and v['steps'][0]['stepSkip']['requestId']==v['steps'][0]['receiptStepSkipRequestId'];assert not v['globalErrors']
  if mode=='holdACK':
   ack=next(i for i,x in enumerate(rows) if x['direction']=='native' and x['line'].startswith('STEP_LOCAL_SKIP_ACK '));completion=next(i for i,x in enumerate(rows) if x['direction']=='host' and x['line'].startswith('STEP_SETTLED_LOCAL_SKIP '));assert completion>ack and v['steps'][0]['physicalDurationMs']>=140
 else:
  negative=[x for x in rows if x['direction']=='native' and x['line'].startswith('STEP_LOCAL_SKIP_ACK ') and x['line'].split()[6]=='0'];assert len(negative)==1,rows
  assert v['steps'][0]['status']=='timedOut' and 'stepSkip' not in v['steps'][0] and not v['steps'][0]['receiptOK'] and 'Native local-step skip rejected' in v['steps'][0]['receiptError']
 groups.append({'name':mode,'report':str(report),'actualNativePackets':str(journal)})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'genuine local-step skip native ACK/receipt hold and original-deadline late-request groups')
