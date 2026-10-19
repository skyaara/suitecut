# SPDX-License-Identifier: MIT
"""Fault-inject only real runtime-skip fd3 packet delivery; never manufacture a native ACK."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse,pathlib,json,subprocess,time,os
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--sdk-directory',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk_directory.resolve();groups=[]
proxy_source="import{observeNativeRunnerHost}from '__SUITECUT_OBSERVER_HELPER__';import fs from'node:fs';import process from'node:process';const settings=JSON.parse(fs.readFileSync(new URL('./settings.json',import.meta.url),'utf8'));await observeNativeRunnerHost(settings.host,({toHost,toNative})=>{const host={write:toHost},native={write:toNative};let a='',b='',held=[];const log=(direction,line)=>fs.appendFileSync(settings.journal,JSON.stringify({direction,line})+'\\n');\nconst parentLine=line=>{log('native',line);if((settings.mode==='holdACK'&&line.startsWith('SKIP_RESULT '))||(settings.mode==='holdReceipt'&&line.startsWith('STEP_RECEIVED '))){held.push(line);fs.writeFileSync(settings.marker,'actual native ACK held');return}host.write(line+'\\n');if(settings.mode==='holdRequest'&&line.startsWith('ABORT_CALL ')){for(const packet of held)native.write(packet+'\\n');held=[];}};\nconst childLine=line=>{log('host',line);if(settings.mode==='holdRequest'&&line.startsWith('SKIP_REQUEST ')){held.push(line);return}native.write(line+'\\n');};\nconst poll=setInterval(()=>{if((settings.mode==='holdACK'||settings.mode==='holdReceipt')&&held.length&&fs.existsSync(settings.release)){for(const line of held)host.write(line+'\\n');held=[];}},5);return{fromNative:parentLine,fromHost:childLine,close(){clearInterval(poll)}}});".replace('__SUITECUT_OBSERVER_HELPER__',pathlib.Path(__file__).resolve().with_name('native-runner-observed-host.mjs').as_uri())
for mode in ['holdACK','holdReceipt','holdRequest']:
 d=out/mode;d.mkdir(exist_ok=True);journal=d/'journal.jsonl';journal.unlink(missing_ok=True);marker=d/'marker';marker.unlink(missing_ok=True);release=d/'release';release.unlink(missing_ok=True);proxy=d/'proxy.mjs';proxy.write_text(proxy_source);(d/'settings.json').write_text(json.dumps({'host':str(sdk/'native-runner-host.js'),'mode':mode,'journal':str(journal),'marker':str(marker),'release':str(release)}));suite=d/'suite.mjs'
 body="await i.step('real step',()=>i.skip('real reason'));"
 hooks="" if mode!='holdRequest' else "hooks:{afterEach:[{run:async(_,i)=>{if(i.status!=='timedOut')throw Error(i.status)}}]},"
 suite.write_text("import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";export default defineNativeSuite({"+hooks+"tests:[{title:'real skip physical ACK',timeout:"+('5000' if mode!='holdRequest' else '100')+",run:async(_,i)=>{"+body+"}}]});")
 report=d/'report.json';report.unlink(missing_ok=True);child=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(proxy),'--suite',str(suite),'--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  if mode in ['holdACK','holdReceipt']:
   end=time.monotonic()+5
   while not marker.exists():
    assert child.poll() is None and time.monotonic()<end;time.sleep(.005)
   time.sleep(.15);rows=[json.loads(x) for x in journal.read_text().splitlines()];assert mode!='holdACK' or not any(x['line'].startswith(('BODY_SKIPPED ','STEP_SETTLED_TEST_SKIP ')) for x in rows),rows;assert child.poll() is None and not report.exists();release.write_text('release the actual native ACK')
  stdout,stderr=child.communicate(timeout=8);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);assert not stderr,stderr;assert child.returncode==(0 if mode!='holdRequest' else 1),(child.returncode,stdout,stderr)
 except BaseException:
  if child.poll() is None:child.terminate()
  stdout,stderr=child.communicate(timeout=5)
  (d/'failed-stdout.bin').write_bytes(stdout);(d/'failed-stderr.bin').write_bytes(stderr)
  raise
 v=json.loads(report.read_text());rows=[json.loads(x) for x in journal.read_text().splitlines()];assert v['globalErrors']==[],v
 assert v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'],v
 if mode=='holdRequest':assert_attempt_secondary(v,'Fixture scope original deadline expired')
 if mode in ['holdACK','holdReceipt']:
  assert v['attempts'][0]['status']=='skipped' and v['steps'][0]['status']=='skipped' and v['steps'][0]['receiptOK'] and v['steps'][0]['testSkip']['requestId']==v['steps'][0]['receiptTestSkipRequestId'];
  if mode=='holdACK':
   ack=next(i for i,x in enumerate(rows) if x['direction']=='native' and x['line'].startswith('SKIP_RESULT '));completion=next(i for i,x in enumerate(rows) if x['direction']=='host' and x['line'].startswith('STEP_SETTLED_TEST_SKIP '));assert completion>ack and v['steps'][0]['physicalDurationMs']>=140
 else:
  negative=[x for x in rows if x['direction']=='native' and x['line'].startswith('SKIP_RESULT ') and x['line'].split()[5]=='0'];assert len(negative)==1,rows
  errors=[x['line'].split() for x in rows if x['direction']=='host' and x['line'].startswith(('CALL_RESULT ', 'CALL_RESULT_UTF16 ')) and x['line'].split()[4]=='0'];assert len(errors)==1,rows;assert 'Native runtime skip rejected' in bytes.fromhex(negative[0]['line'].split()[6]).decode('utf-8');assert 'Native owning callback or ancestor original deadline exceeded' in bytes.fromhex(errors[0][5]).decode('utf-16le' if errors[0][0]=='CALL_RESULT_UTF16' else 'utf-8');assert v['attempts'][0]['status']=='timedOut' and v['steps'][0]['status']=='timedOut' and 'testSkip' not in v['steps'][0]
 groups.append({'name':mode,'report':str(report),'actualNativePackets':str(journal)})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS real native test-skip ACK/step-receipt hold and late-request ownership',len(groups))
