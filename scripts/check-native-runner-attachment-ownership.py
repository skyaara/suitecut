# SPDX-License-Identifier: MIT
"""Native actual filesystem fault/hold ownership; no synthetic syscall result."""
from native_file_markers import read_native_file_marker
import tempfile
import argparse,json,os,pathlib,signal,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--library',required=True,type=pathlib.Path);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);a=p.parse_args();out=a.directory.resolve();out.mkdir(parents=True,exist_ok=True);invocation=pathlib.Path(tempfile.mkdtemp(prefix='cases-',dir=out));groups=[]
if(out/'report.json').exists():(out/'report.json').replace(invocation/'previous-report.json')
def until(fn,seconds=5):
 end=time.monotonic()+seconds
 while not fn():
  assert time.monotonic()<end,'actual readiness/exit did not arrive'
  time.sleep(.005)
def state(pid):
 try:return pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[0]
 except FileNotFoundError:return None
def start(name,source,hold=None,efbig=False,exit_zero=False):
 root=invocation/name;root.mkdir(exist_ok=False);suite=root/'suite.mjs';suite.write_text("import { defineNativeSuite } from "+json.dumps((a.sdk_directory/'native.js').resolve().as_uri())+";\nimport { strict as assert } from 'node:assert';\n"+source)
 env=dict(os.environ);env['LD_PRELOAD']=str(a.library)
 if efbig:env['SUITECUT_ATTACHMENT_TEST_EFBIG']='1'
 if exit_zero:env['SUITECUT_ATTACHMENT_TEST_EXIT_ZERO']='1'
 if hold:env.update(SUITECUT_ATTACHMENT_TEST_HOLD=hold,SUITECUT_ATTACHMENT_TEST_MARKER=str(root/'syscall.marker'),SUITECUT_ATTACHMENT_TEST_RELEASE=str(root/'release'))
 process=subprocess.Popen([str(a.executable),'--node',a.node,'--host-script',str((a.sdk_directory/'native-runner-host.js').resolve()),'--suite',str(suite),'--report',str(root/'actual.json'),'--junit',str(root/'actual.xml'),'--html',str(root/'actual.html')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
 return root,process
def marker(root,process):
 until(lambda:read_native_file_marker(root/'syscall.marker') or process.poll() is not None)
 assert process.poll() is None
 parts=read_native_file_marker(root/'syscall.marker');assert parts is not None;assert len(parts)==5
 value={'actualPid':int(parts[0]),'device':parts[1],'inode':parts[2],'directory':parts[3]=='1','path':bytes.fromhex(parts[4]).decode()};assert state(value['actualPid']) not in [None,'Z'];return value
def finish(root,process,code):
 stdout,stderr=process.communicate(timeout=12);(root/'stdout.bin').write_bytes(stdout);(root/'stderr.bin').write_bytes(stderr);assert process.returncode==code and not stderr,(root.name,process.returncode,stdout,stderr)
 value=json.loads((root/'actual.json').read_text());groups.append({'name':root.name,'report':str(root/'actual.json'),'actualExitCode':process.returncode});return value
def files(root):return [p for d in root.glob('suitecut-attachments-*') for p in d.rglob('*') if p.is_file()]
root,process=start('original-test-deadline-kills-actual-anonymous-writer',"export default defineNativeSuite({tests:[{title:'actual fsync hold',timeout:300,run:(_,i)=>i.attach('held',{body:'actual anonymous bytes'})}]});",'file');m=marker(root,process);assert not m['directory'] and not files(root) and not(root/'actual.json').exists();v=finish(root,process,1);assert v['attempts'][0]['status']=='timedOut' and not v['attachments'] and not files(root) and state(m['actualPid']) is None
root,process=start('cancel-after-real-link-rolls-back-actual-owned-bytes',"export default defineNativeSuite({tests:[{title:'actual link then fsync hold',timeout:300,run:(_,i)=>i.attach('held',{body:'published before native final receipt'})}]});",'directory');m=marker(root,process);assert m['directory'] and len(files(root))==1 and files(root)[0].read_bytes()==b'published before native final receipt';v=finish(root,process,1);assert v['attempts'][0]['status']=='timedOut' and not v['attachments'] and not files(root) and state(m['actualPid']) is None
root,process=start('cleanup-syscall-retains-parent-and-phase-until-real-release',"export default defineNativeSuite({tests:[{title:'actual rollback fsync hold',timeout:300,run:(_,i)=>i.attach('missing',{path:''})}]});",'cleanup');m=marker(root,process);assert m['directory'];time.sleep(.45);assert process.poll() is None and state(m['actualPid']) not in [None,'Z'] and not(root/'actual.json').exists();(root/'release').write_text('actual native fsync can resume');v=finish(root,process,1);assert v['attempts'][0]['status']=='timedOut' and not v['attachments'] and not files(root) and state(m['actualPid']) is None
root,process=start('actual-live-timeout-amendment-keeps-one-native-operation-budget',"import { setTimeout as wait } from 'node:timers/promises';export default defineNativeSuite({tests:[{title:'actual amendment while writer held',timeout:300,run:async(_,i)=>{const p=i.attach('held',{body:'actual native held bytes'});await wait(100);await i.setTimeout(1200);await p}}]});",'file');m=marker(root,process);time.sleep(.45);assert process.poll() is None and state(m['actualPid']) not in [None,'Z'] and not(root/'actual.json').exists();(root/'release').write_text('actual native fsync resume');v=finish(root,process,0);assert v['summary']['expected']==1 and len(v['attachments'])==1 and pathlib.Path(v['attachments'][0]['path']).read_bytes()==b'actual native held bytes' and state(m['actualPid']) is None
root,process=start('real-EFBIG-byte-write-is-caught-after-native-rollback',"export default defineNativeSuite({tests:[{title:'actual native file limit',run:async(_,i)=>{await assert.rejects(i.attach('real file limit',{body:'actual bytes'}),/errno=27/);await i.attach('zero-byte file')}}]});",efbig=True);v=finish(root,process,0);assert v['summary']['expected']==1 and len(v['attachments'])==1 and v['attachments'][0]['byteLength']==0 and files(root)[0].stat().st_size==0
root,process=start('real-cleanup-EACCES-overrides-static-expected-failure',"export default defineNativeSuite({tests:[{title:'actual native cleanup permission fault',timeout:300,expectedFailure:true,run:(_,i)=>i.attach('held',{body:'cannot be falsely cleaned'})}]});",'directory');m=marker(root,process);directory=pathlib.Path(m['path']);assert directory.is_dir() and len(files(root))==1;directory.chmod(0o500)
try:
 v=finish(root,process,1);assert not v['attachments'] and not v['attempts'][0]['expected'] and v['globalErrors'] and any('rollback failed' in x for x in v['globalErrors']) and len(files(root))==1
finally:directory.chmod(0o700)
root,process=start('genuine-owner-exit-zero-does-not-hide-pending-artifact',"import{access}from'node:fs/promises';import{setTimeout as wait}from'node:timers/promises';export default defineNativeSuite({tests:[{title:'actual owner exit0',timeout:3000,expectedFailure:true,run:async(_,i)=>{void i.attach('held',{body:'actual bytes'});for(;;){try{await access(process.env.SUITECUT_ATTACHMENT_TEST_MARKER);break}catch{await wait(1)}}process.exit(0)}}]});",'file');m=marker(root,process);v=finish(root,process,1);assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and not v['attachments'] and v['workers'][0]['exitCode']==0 and v['workers'][0]['signal'] is None and not files(root) and state(m['actualPid']) is None
root,process=start('genuine-native-writer-exit-zero-is-infrastructure-not-caught-API-success',"export default defineNativeSuite({tests:[{title:'caught native writer failure',run:(_,i)=>assert.rejects(i.attach('held',{body:'actual bytes'}),/without successful commit/)}]});",'file',exit_zero=True);m=marker(root,process);(root/'release').write_text('actual native writer exit0');v=finish(root,process,1);assert v['summary']['unexpected']==1 and not v['attachments'] and any('writer PID '+str(m['actualPid'])+' exit 0' in error for error in v['globalErrors']) and not files(root) and state(m['actualPid']) is None
root,process=start('actual-runner-SIGKILL-couples-native-anonymous-writer-death',"export default defineNativeSuite({tests:[{title:'actual parent death',timeout:5000,run:(_,i)=>i.attach('held',{body:'anonymous native bytes'})}]});",'file');m=marker(root,process);children=[]
for entry in pathlib.Path('/proc').iterdir():
 if entry.name.isdigit():
  try:
   stat=(entry/'stat').read_text().rsplit(')',1)[1].split()
   if int(stat[1])==process.pid:children.append(entry.name)
  except FileNotFoundError:pass
assert str(m['actualPid']) in children;process.kill();stdout,stderr=process.communicate(timeout=5);assert process.returncode==-signal.SIGKILL and not(root/'actual.json').exists() and not files(root)
for child in children:until(lambda:state(int(child)) in [None,'Z'])
(root/'actual-death.json').write_text(json.dumps({'runnerWaitpidSignal':signal.SIGKILL,'childProcStates':{pid:state(int(pid)) for pid in children},'qualification':'Actual /proc disappearance/zombie observed for reparented children, no fabricated child waitpid receipt or report'},indent=2)+'\n');groups.append({'name':root.name,'actualRunnerSignal':signal.SIGKILL,'actualChildDeath':str(root/'actual-death.json')})
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native fsync holds, original deadline, real link/cancel rollback, native cleanup physical retention, actual EFBIG/EACCES and parentdeath; fault shim never fabricates a syscall return','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual native artifact syscall/ownership groups')
