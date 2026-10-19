# SPDX-License-Identifier: MIT
"""Withhold only actual native notInvoked admission replies, never fabricate receipt."""
import argparse,pathlib,json,subprocess,time,os
p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True);p.add_argument('--sdk-directory',type=pathlib.Path,required=True);p.add_argument('--directory',type=pathlib.Path,required=True);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);sdk=a.sdk_directory.resolve();groups=[]
source="import{observeNativeRunnerHost}from '__SUITECUT_OBSERVER_HELPER__';import fs from'node:fs';import process from'node:process';const s=JSON.parse(fs.readFileSync(new URL('./settings.json',import.meta.url),'utf8'));await observeNativeRunnerHost(s.host,({toHost,toNative})=>{const host={write:toHost},native={write:toNative};fs.writeFileSync(s.childPID,String(process.pid));let a='',b='',held=[];const log=(direction,line)=>fs.appendFileSync(s.journal,JSON.stringify({direction,line})+'\\n');\nconst fromNative=line=>{log('native',line);if(s.mode!=='holdRequest'&&line.startsWith('STEP_ADMITTED_NOT_INVOKED ')){held.push(line);fs.writeFileSync(s.marker,'genuine native declaration ACK held');return}host.write(line+'\\n');if(s.mode==='holdRequest'&&line.startsWith('ABORT ')){for(const packet of held)native.write(packet+'\\n');held=[];}};\nconst fromHost=line=>{log('host',line);if(s.mode==='holdRequest'&&line.startsWith('STEP_BEGIN_SKIP ')){held.push(line);return}native.write(line+'\\n');};\nconst poll=setInterval(()=>{if(s.mode==='holdACK'&&held.length&&fs.existsSync(s.release)){for(const packet of held)host.write(packet+'\\n');held=[];}},5);return{fromNative:fromNative,fromHost:fromHost,close(){clearInterval(poll)}}});".replace('__SUITECUT_OBSERVER_HELPER__',pathlib.Path(__file__).resolve().with_name('native-runner-observed-host.mjs').as_uri())
for mode in ['holdACK','dropACK','holdRequest']:
 d=out/mode;d.mkdir(exist_ok=True);journal=d/'journal.jsonl';marker=d/'marker';release=d/'release';called=d/'callback-called';childPID=d/'child.pid'
 for p in [journal,marker,release,called,childPID]:p.unlink(missing_ok=True)
 (d/'proxy.mjs').write_text(source);(d/'settings.json').write_text(json.dumps({'mode':mode,'host':str(sdk/'native-runner-host.js'),'journal':str(journal),'marker':str(marker),'release':str(release),'childPID':str(childPID)}));suite=d/'suite.mjs'
 suite.write_text("import fs from 'node:fs';import {defineNativeSuite} from "+json.dumps((sdk/'native-suite.js').as_uri())+";export default defineNativeSuite({tests:[{title:'native declaration admission',timeout:"+('5000' if mode=='holdACK' else '500')+",run:async(_,i)=>{await i.step.skip('intentionally omitted',()=>fs.writeFileSync("+json.dumps(str(called))+",'incorrect invocation'));console.log('actual body continued')}}]});")
 report=d/'report.json';report.unlink(missing_ok=True);child=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str(d/'proxy.mjs'),'--suite',str(suite),'--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  if mode=='holdACK':
   end=time.monotonic()+5
   while not marker.exists():assert child.poll() is None and time.monotonic()<end;time.sleep(.005)
   time.sleep(.15);rows=[json.loads(x) for x in journal.read_text().splitlines()];assert not any(x['direction']=='host' and x['line'].startswith(('BODY_RESULT ', 'BODY_RESULT_UTF16 ')) for x in rows) and child.poll() is None and not report.exists() and not called.exists();release.write_text('deliver only genuine native ACK')
  stdout,stderr=child.communicate(timeout=10);(d/'stdout.bin').write_bytes(stdout);(d/'stderr.bin').write_bytes(stderr);assert child.returncode==(0 if mode=='holdACK' else 1) and not stderr,(child.returncode,stdout,stderr)
 except BaseException:
  if child.poll() is None:child.terminate()
  stdout,stderr=child.communicate(timeout=5);(d/'failed-stdout.bin').write_bytes(stdout);(d/'failed-stderr.bin').write_bytes(stderr);raise
 rows=[json.loads(x) for x in journal.read_text().splitlines()];v=json.loads(report.read_text());assert not called.exists() and not any(x['direction']=='host' and x['line'].startswith(('STEP_SETTLED ', 'STEP_SETTLED_LOCAL_SKIP ', 'STEP_SETTLED_TEST_SKIP ')) for x in rows)
 assert all(not bytes.fromhex(w['stderr']['data']) for w in v['workers'])
 if mode=='holdACK':
  assert v['attempts'][0]['status']=='passed' and v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'] and v['steps'][0]['receipt']=='notInvoked' and not v['steps'][0]['receiptOK'] and 'physicalDurationMs' not in v['steps'][0]
  ack=next(i for i,x in enumerate(rows) if x['direction']=='native' and x['line'].startswith('STEP_ADMITTED_NOT_INVOKED '));body=next(i for i,x in enumerate(rows) if x['direction']=='host' and x['line'].startswith('BODY_RESULT '));assert body>ack
 elif mode=='dropACK':assert v['attempts'][0]['status']=='timedOut' and not v['attempts'][0]['expected'] and v['workers'][0]['signal']==9 and v['steps'][0]['receipt']=='notInvoked' and not v['steps'][0]['receiptOK']
 else:
  negative=[x for x in rows if x['direction']=='native' and x['line'].startswith('STEP_ADMITTED_NOT_INVOKED ') and x['line'].split()[6]=='0'];assert len(negative)==1 and v['steps']==[] and v['attempts'][0]['status']=='timedOut' and v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal']
 pid=int(childPID.read_text());path=pathlib.Path('/proc')/str(pid)/'stat';end=time.monotonic()+3
 while path.exists() and path.read_text().split(') ',1)[1][0]!='Z' and time.monotonic()<end:time.sleep(.01)
 state=path.read_text().split(') ',1)[1][0] if path.exists() else None;assert state in [None,'Z'],('actual SDK host remains executing',pid,state)
 groups.append({'name':mode,'report':str(report),'actualNativePackets':str(journal),'actualSDKChildPID':pid,'sdkChildProcStateAfterNativeWorkerExit':state,'qualification':'No waitpid claim for reparented SDK child after genuine proxy SIGKILL; proc state observed directly'})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual notInvoked admission ACK hold/deadline/late-request ownership groups')
