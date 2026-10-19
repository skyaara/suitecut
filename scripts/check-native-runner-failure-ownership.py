# SPDX-License-Identifier: MIT
"""Fault-inject only real fd3 packet delivery; never manufacture a native ACK."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse,pathlib,json,subprocess,time,os
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--sdk-directory',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk_directory.resolve();groups=[]
proxy_source="import{observeNativeRunnerHost}from '__SUITECUT_OBSERVER_HELPER__';import fs from'node:fs';import process from'node:process';const settings=JSON.parse(fs.readFileSync(new URL('./settings.json',import.meta.url),'utf8'));await observeNativeRunnerHost(settings.host,({toHost,toNative})=>{const host={write:toHost},native={write:toNative};let a='',b='',held=[];const log=(direction,line)=>fs.appendFileSync(settings.journal,JSON.stringify({direction,line})+'\\n');\nconst parentLine=line=>{log('native',line);if(settings.mode==='holdACK'&&line.startsWith('FAIL_RESULT ')){held.push(line);fs.writeFileSync(settings.marker,'actual native ACK held');return}host.write(line+'\\n');if(settings.mode==='holdRequest'&&line.startsWith('ABORT_CALL ')){for(const packet of held)native.write(packet+'\\n');held=[];}};\nconst childLine=line=>{log('host',line);if(settings.mode==='holdRequest'&&line.startsWith('FAIL_REQUEST ')){held.push(line);return}native.write(line+'\\n');};\nconst poll=setInterval(()=>{if(settings.mode==='holdACK'&&held.length&&fs.existsSync(settings.release)){for(const line of held)host.write(line+'\\n');held=[];}},5);return{fromNative:parentLine,fromHost:childLine,close(){clearInterval(poll)}}});".replace('__SUITECUT_OBSERVER_HELPER__',pathlib.Path(__file__).resolve().with_name('native-runner-observed-host.mjs').as_uri())
for mode in ['holdACK','holdRequest']:
 d=out/mode;d.mkdir(exist_ok=True);journal=d/'journal.jsonl';journal.unlink(missing_ok=True);marker=d/'marker';marker.unlink(missing_ok=True);release=d/'release';release.unlink(missing_ok=True);proxy=d/'proxy.mjs';proxy.write_text(proxy_source);(d/'settings.json').write_text(json.dumps({'host':str(sdk/'native-runner-host.js'),'mode':mode,'journal':str(journal),'marker':str(marker),'release':str(release)}));suite=d/'suite.mjs'
 body="i.fail('real reason');"
 hooks="" if mode=='holdACK' else "hooks:{afterEach:[{run:(_,i)=>{if(i.status!=='timedOut')throw Error(i.status)}}]},"
 suite.write_text("import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";export default defineNativeSuite({"+hooks+"tests:[{title:'real expected-failure physical ACK',timeout:"+('5000' if mode=='holdACK' else '100')+",run:(_,i)=>{"+body+"}}]});")
 report=d/'report.json';report.unlink(missing_ok=True);child=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(proxy),'--suite',str(suite),'--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  if mode=='holdACK':
   end=time.monotonic()+5
   while not marker.exists():
    assert child.poll() is None and time.monotonic()<end;time.sleep(.005)
   time.sleep(.15);rows=[json.loads(x) for x in journal.read_text().splitlines()];assert not any(x['line'].startswith(('BODY_RESULT ', 'BODY_RESULT_UTF16 ')) for x in rows),rows;assert child.poll() is None and not report.exists();release.write_text('release the actual native ACK')
  stdout,stderr=child.communicate(timeout=8);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);assert not stderr,stderr;assert child.returncode==1,(child.returncode,stdout,stderr)
 except BaseException:
  if child.poll() is None:child.terminate()
  stdout,stderr=child.communicate(timeout=5)
  (d/'failed-stdout.bin').write_bytes(stdout);(d/'failed-stderr.bin').write_bytes(stderr)
  raise
 v=json.loads(report.read_text());rows=[json.loads(x) for x in journal.read_text().splitlines()];assert v['globalErrors']==[],v
 assert v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'],v
 if mode=='holdRequest':assert_attempt_secondary(v,'Fixture scope original deadline expired')
 if mode=='holdACK':
  ack=next(i for i,x in enumerate(rows) if x['direction']=='native' and x['line'].startswith('FAIL_RESULT '));completion=next(i for i,x in enumerate(rows) if x['direction']=='host' and x['line'].startswith(('BODY_RESULT ', 'BODY_RESULT_UTF16 ')));assert completion>ack and v['attempts'][0]['status']=='passed' and not v['attempts'][0]['expected'] and v['attempts'][0]['expectedStatus']=='failed'
 else:
  negative=[x for x in rows if x['direction']=='native' and x['line'].startswith('FAIL_RESULT ') and x['line'].split()[5]=='0'];assert len(negative)==1,rows
  errors=[x['line'].split() for x in rows if x['direction']=='host' and x['line'].startswith(('CALL_RESULT ', 'CALL_RESULT_UTF16 ')) and x['line'].split()[4]=='0'];assert len(errors)==1,rows;assert 'Native expected failure rejected' in bytes.fromhex(errors[0][5]).decode('utf-16le' if errors[0][0]=='CALL_RESULT_UTF16' else 'utf-8');assert v['attempts'][0]['status']=='timedOut'
 groups.append({'name':mode,'report':str(report),'actualNativePackets':str(journal)})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS real native expected-failure ACK hold and genuine late rejection ownership',len(groups))
