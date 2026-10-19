# SPDX-License-Identifier: MIT
"""Real native coordinator exception/rollback controls; no production fault API."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time

parser=argparse.ArgumentParser()
parser.add_argument('--executable',type=Path,required=True)
parser.add_argument('--library',type=Path,required=True)
parser.add_argument('--sdk-directory',type=Path,required=True)
parser.add_argument('--source-root',type=Path,required=True)
parser.add_argument('--directory',type=Path,required=True)
parser.add_argument('--node',default='node')
parser.add_argument('--anonymous-only',action='store_true',help='Four genuine file-group seams; default project/work-group8 unchanged')
args=parser.parse_args();args.directory.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=args.directory.resolve()));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=[args.executable.resolve(),args.library.resolve(),Path(__file__).resolve(),*[p for p in (args.source_root/'native/runner').rglob('*') if p.is_file()],*[p for p in args.sdk_directory.rglob('*') if p.is_file() and not p.is_symlink()]]
# Resolve installed packages through the exact emitted CLI, independently of
# whether the SDK is normal dist or an isolated closure.
resolved=subprocess.run([args.node,'--input-type=module','-e',"import {createRequire} from 'node:module';const r=createRequire(process.argv[1]);process.stdout.write(JSON.stringify(['zod','yaml'].map(n=>[n,r.resolve(n)])));",str((args.sdk_directory/'native-runner-cli.js').resolve())],capture_output=True,timeout=10)
assert resolved.returncode==0 and resolved.stderr==b'',resolved.stderr
for name,entry in json.loads(resolved.stdout):
 package=next(p for p in Path(entry).resolve().parents if (p/'package.json').is_file() and json.loads((p/'package.json').read_text()).get('name')==name)
 inputs.extend(p for p in package.rglob('*') if p.is_file() and not p.is_symlink())
node=shutil.which(args.node);assert node,'Actual Node executable cannot be resolved';inputs.append(Path(node).resolve())
before={str(p):sha(p) for p in sorted(set(inputs))};(root/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n');scopes=[]
try:
 for mode in (['file'] if args.anonymous_only else ['project','work-group']):
  parallel=mode!='project'
  for phase,kind in [('actor-staging',0),('lock-admission',0),('descriptor-allocation',1),('poll',2)]:
   name=mode+'-'+phase;folder=root/name;folder.mkdir();ready=folder/'actual-io-ready';trace=folder/'coordinator-fault';gate=folder/'gate';os.mkfifo(gate,0o600);hosts=folder/'actual-sdk-pids';child=folder/'actual-detached-child';entered=folder/'body-entered';preserved=folder/'preserve.txt';preserved.write_bytes(b'foreign user data');preserved_inode=preserved.stat().st_ino
   source=r'''import {defineNativeSuite} from API;
import assert from 'node:assert/strict';import process from 'node:process';import {appendFileSync,writeFileSync} from 'node:fs';import {spawn} from 'node:child_process';import {once} from 'node:events';
appendFileSync(HOSTS,`${process.pid} ${process.ppid}\n`);
const descendant=async()=>{const c=spawn(process.execPath,['-e',"const server=require('node:net').createServer(()=>{});process.on('disconnect',()=>server.close());server.listen(0,'127.0.0.1',()=>process.send('ready'))"],{detached:true,stdio:['ignore','ignore','ignore','ipc']});await once(c,'message');writeFileSync(CHILD,String(c.pid));c.unref();c.channel.unref()};
const held=async({},info)=>{appendFileSync(ENTERED,String(process.pid)+'\n');if(KIND===1){await descendant();await info.attach('owned-file',{body:Buffer.from('actual committed bytes')});assert.fail('faulted committed attachment unexpectedly returned')}else await new Promise(resolve=>info.signal.addEventListener('abort',resolve,{once:true}))};
export default defineNativeSuite({fullyParallel:PARALLEL,projects:[{name:'p',tests:[{title:'p',lock:'p',timeout:10000,run:held}]},{name:'q',tests:[{title:'q',lock:'q',timeout:10000,run:async()=>{}}]}]});
'''
   if mode=='file':
    original=source
    source=source.replace("{fullyParallel:PARALLEL,projects:[{name:'p',tests:[{title:'p',lock:'p',timeout:10000,run:held}]},{name:'q',tests:[{title:'q',lock:'q',timeout:10000,run:async()=>{}}]}]}","{fullyParallel:true,tests:[{title:'p',lock:'p',timeout:10000,run:held},{title:'q',lock:'q',timeout:10000,run:async()=>{}}]}")
    assert source!=original and 'projects:[' not in source,'Authored anonymous fault fixture must not declare a project'
   for old,new in [('API',json.dumps((args.sdk_directory/'native-suite.js').resolve().as_uri())),('HOSTS',json.dumps(str(hosts))),('CHILD',json.dumps(str(child))),('ENTERED',json.dumps(str(entered))),('KIND',str(kind)),('PARALLEL','true' if parallel else 'false')]:source=source.replace(old,new)
   suite=folder/'suite.mjs';suite.write_text(source);env=os.environ.copy();env.update(SUITECUT_COORDINATOR_FAULT_PHASE=phase,SUITECUT_COORDINATOR_FAULT_TRACE=str(trace),SUITECUT_COORDINATOR_IO_AUDIT=str(folder/'real-syscalls'),LD_PRELOAD=str(args.library.resolve()))
   if kind:env.update(LD_PRELOAD=str(args.library.resolve()),SUITECUT_COORDINATOR_FAULT_READY=str(ready),SUITECUT_COORDINATOR_IO_READY=str(ready),SUITECUT_COORDINATOR_IO_GATE=str(gate),SUITECUT_COORDINATOR_IO_KIND=str(kind))
   cmd=[args.node,str((args.sdk_directory/'native-runner-cli.js').resolve()),'--executable',str(args.executable.resolve()),'--suite',str(suite),'--workers','2','--report',str(folder/'result.json')]
   started=time.monotonic_ns();process=subprocess.Popen(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True);timed_out=False
   try:stdout,stderr=process.communicate(timeout=15)
   except subprocess.TimeoutExpired:
    timed_out=True;os.killpg(process.pid,signal.SIGTERM)
    try:stdout,stderr=process.communicate(timeout=5)
    except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate(timeout=5)
   ended=time.monotonic_ns();(folder/'actual.stdout').write_bytes(stdout);(folder/'actual.stderr').write_bytes(stderr);receipt=dict(command=cmd,returncode=process.returncode,timedOut=timed_out,startedMonotonicNs=started,endedMonotonicNs=ended);(folder/'actual-process.json').write_text(json.dumps(receipt,indent=2)+'\n')
   assert not timed_out and process.returncode==2,(name,receipt,stderr)
   expected=b'Native runner: std::bad_alloc\n' if phase in ['actor-staging','descriptor-allocation'] else ('Native runner: Injected native coordinator '+phase+' failure\n').encode()
   assert stderr==expected,(name,stderr);assert not (folder/'result.json').exists(),'No successful/retired report may be fabricated after exception'
   fields=trace.read_text().split();assert len(fields)==6 and fields[0]==phase;native,pid,workers,files,when=map(int,fields[1:]);assert workers>=1 and ended-when<8_000_000_000,'Fixed6s emergency grace was extended/reset'
   actual_hosts=[tuple(map(int,line.split())) for line in hosts.read_text().splitlines()];assert actual_hosts and all(parent==native for _,parent in actual_hosts)
   audit=[]
   for line in (folder/'real-syscalls').read_text().splitlines():
    event,owner,a,b,c,at=line.split();audit.append(dict(event=event,owner=int(owner),a=int(a),b=int(b),c=int(c),at=int(at)))
   waits={x['a']:x for x in audit if x['event']=='wait' and x['owner']==native};assert len(waits)==len([x for x in audit if x['event']=='wait' and x['owner']==native]),'Duplicate actual positive waitpid receipt'
   helpers={x['owner']:x for x in audit if x['event']=='helper' and x['a']==native};assert helpers
   assert all(actual in waits for actual in helpers) and all(actual in waits for actual,_ in actual_hosts)
   assert pid in waits and waits[pid]['at']>=when,'Held original host was not genuinely reaped after exception'
   pids={native,pid,*[p for p,_ in actual_hosts],*helpers,*waits};evidence=dict(scope=name,injectedPhase=phase,actualNativePid=native,actualHostPids=[p for p,_ in actual_hosts],actualPhysicalFiles=files)
   if kind:
    observed=ready.read_text().strip().split(' ',3);assert len(observed)==4;writer,device,inode=map(int,observed[:3]);path=Path(observed[3]);assert writer>0 and inode>0 and files>0 and not path.exists(),('real inode rollback missing',path);pids.add(writer);evidence.update(writerPid=writer,device=device,inode=inode,removedPath=str(path))
    assert writer in waits and waits[writer]['at']>=when and os.WIFSIGNALED(waits[writer]['b']) and os.WTERMSIG(waits[writer]['b'])==signal.SIGKILL
    cleanup=[x for x in audit if x['event']=='unlink' and x['a']==device and x['b']==inode];assert len(cleanup)==1 and cleanup[0]['at']>=when
    rollback=cleanup[0]['owner'];assert rollback in helpers and helpers[rollback]['b']==(3 if kind==1 else 4) and helpers[rollback]['at']>=when
    assert rollback in waits and waits[rollback]['at']>=cleanup[0]['at'] and os.WIFEXITED(waits[rollback]['b']) and os.WEXITSTATUS(waits[rollback]['b'])==0
    evidence.update(actualRollbackPid=rollback,actualUnlinkReceipt=cleanup[0],actualWaitpidReceipts=list(waits.values()))
    if kind==1:assert child.is_file();pids.add(int(child.read_text()));assert entered.is_file();evidence['actualDetachedChildPid']=int(child.read_text());assert int(child.read_text()) in waits and waits[int(child.read_text())]['at']>=when
   else:assert not entered.exists(),'An admission/staging exception must not invoke author body'
   for actual in pids:
    if actual<=0:continue
    try:os.kill(actual,0)
    except ProcessLookupError:pass
    else:raise AssertionError(('Physical PID survived emergency',actual,name))
   assert preserved.read_bytes()==b'foreign user data' and preserved.stat().st_ino==preserved_inode
   evidence['actualWaitpidReceipts']=list(waits.values());evidence['actualHelperExecReceipts']=list(helpers.values());evidence['passed']=True;scopes.append(evidence);(root/'scopes.json').write_text(json.dumps(scopes,indent=2)+'\n')
except BaseException:
 (root/'first-failure.json').write_text(json.dumps(dict(completedScopes=scopes,scope='Actual injected native coordinator failure; first negative preserved'),indent=2)+'\n');raise
finally:
 after={str(p):sha(p) for p in sorted(set(inputs))};(root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n');assert before==after
(root/'index.json').write_text(json.dumps(dict(passed=True,scopes=scopes,inputsUnchanged=True,scope='Real native project/work-group or separately selected file-group staging,lock,descriptor,poll exception seams with actual SDK/descendant waitpid and committed-file/output rollback; injected failures are compiled test-only, no production fault API'),indent=2)+'\n');print(json.dumps(dict(passed=True,scopes=len(scopes),directory=str(root))))
