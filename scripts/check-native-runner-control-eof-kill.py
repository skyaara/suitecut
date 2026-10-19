# SPDX-License-Identifier: MIT
import tempfile
"""Confirmed real SIGKILL after bounded closed-control drain, distinct from normal exit."""
import argparse,json,os,pathlib,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--library',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True);out=pathlib.Path(tempfile.mkdtemp(prefix="run-",dir=a.directory.resolve()));sdk=a.sdk_directory.resolve();rows=[]
def state(pid):
 try:return pathlib.Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()[0]
 except FileNotFoundError:return None
common="import{defineNativeSuite}from "+json.dumps((sdk/'native.js').as_uri())+";import{strict as assert}from'node:assert';import{closeSync,existsSync}from'node:fs';import{setTimeout as wait}from'node:timers/promises';const linger=()=>Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,10000);"
def run(name,source,held=False):
 root=out/name;root.mkdir();suite=root/'suite.mjs';suite.write_text(common+source);env=dict(os.environ);env.pop('LD_PRELOAD',None)
 if held:env.update(LD_PRELOAD=str(a.library),SUITECUT_ATTACHMENT_TEST_HOLD='file',SUITECUT_ATTACHMENT_TEST_MARKER=str(root/'fsync.marker'),SUITECUT_ATTACHMENT_TEST_RELEASE=str(root/'never-release'))
 started=time.monotonic();v=subprocess.run([str(a.executable),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(root/'actual.json'),'--junit',str(root/'actual.xml'),'--html',str(root/'actual.html')],env=env,capture_output=True,timeout=8);elapsed=(time.monotonic()-started)*1000;(root/'stdout.bin').write_bytes(v.stdout);(root/'stderr.bin').write_bytes(v.stderr);assert v.returncode==1 and not v.stderr,(name,v.returncode,v.stdout,v.stderr);r=json.loads((root/'actual.json').read_text());attempt=r['attempts'][0];assert attempt['status']=='interrupted' and not attempt['expected'] and r['summary']['unexpected']==1 and r['globalErrors'] and any('exit confirmed SIGKILL after native bounded control cleanup' in e for e in r['globalErrors']);assert len(r['workers'])==1 and r['workers'][0]['signal']==9 and r['workers'][0]['exitCode'] is None and state(r['workers'][0]['pid']) is None;assert not r['attachments'] and not list(root.glob('suitecut-attachments-*/*')) and 900<=elapsed<3000,(name,elapsed,r)
 row={'scope':name,'actualExitCode':v.returncode,'report':str(root/'actual.json'),'elapsedMs':elapsed}
 if held:
  parts=(root/'fsync.marker').read_text().split();assert len(parts)==5 and parts[3]=='0';writer=int(parts[0]);assert state(writer) is None;row['actualWriterPid']=writer
 rows.append(row)
run('raw-body-kill-cannot-be-excused-by-static-expected-failure',"export default defineNativeSuite({tests:[{title:'actual bounded control cleanup',timeout:0,expectedFailure:true,run:()=>{closeSync(3);linger()}}]});")
run('hook-kill-preserves-actual-infrastructure-interruption',"export default defineNativeSuite({hooks:{beforeEach:[{run:()=>{closeSync(3);linger()}}]},tests:[{title:'uninvoked body after failed control',timeout:0,expectedFailure:true,run:()=>assert.fail('body must not be admitted')}]});")
run('pending-file-kill-has-no-fake-attachment-or-receipt',"export default defineNativeSuite({tests:[{title:'actual pending file and closed control',timeout:0,expectedFailure:true,run:async(_,i)=>{void i.attach('held',{body:'actual writer bytes'});while(!existsSync(process.env.SUITECUT_ATTACHMENT_TEST_MARKER))await wait(1);closeSync(3);linger()}}]});",True)
(out/'report.json').write_text(json.dumps({'passed':len(rows),'scope':'Actual native cleanup SIGKILL only after worker remains alive through bounded EOF drain; static expected failure cannot excuse native infrastructure; pending file physically rolled back, raw writer/host waitpid retained','groups':rows},indent=2)+'\n');print('PASS',len(rows),'actual closed-control cleanup-kill groups')
