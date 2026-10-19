# SPDX-License-Identifier: MIT
"""Actual directory driver unwind with held writer, rollback and Node descendants."""
import argparse, hashlib, json, os, signal, subprocess, tempfile, time
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('executable','library','sdk-directory','supervisor','directory','node'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='directory-fault-',dir=a.directory));tests=root/'tests';tests.mkdir()
ready=root/'writer-ready';trace=root/'fault';audit=root/'syscalls';gate=root/'gate';os.mkfifo(gate,0o600);hosts=root/'hosts';descendant=root/'descendant';report=root/'refused.json';receipt=root/'supervisor.json'
source='''import {defineNativeSuite} from API;
import {appendFileSync,writeFileSync} from 'node:fs';import {spawn} from 'node:child_process';import {once} from 'node:events';
appendFileSync(HOSTS,`${process.pid} ${process.ppid}\\n`);
export default defineNativeSuite({tests:[{title:'actual directory held writer',timeout:10000,run:async(_,info)=>{
 const c=spawn(process.execPath,['-e',"const s=require('node:net').createServer(()=>{});process.on('disconnect',()=>s.close());s.listen(0,'127.0.0.1',()=>process.send('ready'))"],{detached:true,stdio:['ignore','ignore','ignore','ipc']});await once(c,'message');writeFileSync(CHILD,String(c.pid));c.unref();c.channel.unref();
 await info.attach('held actual bytes',{body:Buffer.from('actual directory inode')});throw Error('faulted writer unexpectedly published');}}]});
'''.replace('API',json.dumps((a.sdk_directory/'native-suite.js').as_uri())).replace('HOSTS',json.dumps(str(hosts))).replace('CHILD',json.dumps(str(descendant)))
fixture=tests/'actual.spec.mjs';fixture.write_text(source)
inputs=[Path(__file__).resolve(),a.executable.resolve(),a.library.resolve(),a.supervisor.resolve(),a.node.resolve(),fixture,*sorted(a.sdk_directory.glob('*.js'))];pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}
env=os.environ.copy();env.update(SUITECUT_COORDINATOR_FAULT_PHASE='descriptor-allocation',SUITECUT_COORDINATOR_FAULT_TRACE=str(trace),SUITECUT_COORDINATOR_FAULT_READY=str(ready),SUITECUT_COORDINATOR_IO_AUDIT=str(audit),SUITECUT_COORDINATOR_IO_READY=str(ready),SUITECUT_COORDINATOR_IO_GATE=str(gate),SUITECUT_COORDINATOR_IO_KIND='1',LD_PRELOAD=str(a.library))
command=[str(a.supervisor),'--report',str(receipt),'--timeout-ms','20000','--cleanup-grace-ms','1000','--physical-ms','10000','--',str(a.node),str(a.sdk_directory/'native-runner-cli.js'),'--executable',str(a.executable),'--test-dir',str(tests),'--report',str(report)]
began=time.monotonic_ns()
with (root/'stdout').open('wb') as out,(root/'stderr').open('wb') as err:result=subprocess.run(command,env=env,stdout=out,stderr=err)
ended=time.monotonic_ns();physical=json.loads(receipt.read_text());evidence={'inputs':pins,'command':command,'exit':result.returncode,'supervisor':physical,'stderr':str(root/'stderr'),'reportExists':report.exists()}
try:
 assert result.returncode==2 and physical['childExit']['code']==2 and not physical['childExit']['signaled'],physical
 assert not physical['timeout'] and not physical['cleanupFailed'] and physical['descendantsRetired'],physical
 assert not report.exists(),'Fault must not fabricate retired source/report'
 assert (root/'stderr').read_bytes()==b'Native runner: Native directory actor failed after real emergency drain; report publication refused: std::bad_alloc\n'
 phase,native,host,workers,files,when=trace.read_text().split();native,host,workers,files,when=map(int,(native,host,workers,files,when));assert phase=='descriptor-allocation' and workers==1 and files>0 and began<when<ended and ended-when<8_000_000_000
 records=[]
 for line in audit.read_text().splitlines():
  event,owner,x,y,z,at=line.split();records.append({'event':event,'owner':int(owner),'a':int(x),'b':int(y),'c':int(z),'at':int(at)})
 waits={x['a']:x for x in records if x['event']=='wait' and x['owner']==native};assert len(waits)==sum(x['event']=='wait' and x['owner']==native for x in records)
 helpers={x['owner']:x for x in records if x['event']=='helper' and x['a']==native};actual_hosts=[tuple(map(int,x.split())) for x in hosts.read_text().splitlines()]
 assert len(actual_hosts)==2 and all(parent==native and pid in waits for pid,parent in actual_hosts) and all(pid in waits for pid in helpers)
 writer,device,inode,path=ready.read_text().strip().split(' ',3);writer,device,inode=map(int,(writer,device,inode));assert not Path(path).exists() and writer in waits and waits[writer]['at']>=when and os.WIFSIGNALED(waits[writer]['b']) and os.WTERMSIG(waits[writer]['b'])==signal.SIGKILL
 unlinks=[x for x in records if x['event']=='unlink' and x['a']==device and x['b']==inode];assert len(unlinks)==1 and unlinks[0]['at']>=when
 rollback=unlinks[0]['owner'];assert rollback in helpers and helpers[rollback]['b']==3 and rollback in waits and waits[rollback]['at']>=unlinks[0]['at'] and waits[rollback]['b']==0
 child=int(descendant.read_text());assert child in waits and waits[child]['at']>=when and host in waits and waits[host]['at']>=when
 pids={physical['childPID'],native,host,writer,child,*helpers,*waits};remaining=[pid for pid in pids if Path('/proc',str(pid)).exists()];assert not remaining,remaining
 evidence.update(passed=True,actualNativePID=native,actualHostReceipts=actual_hosts,physicalFilesAtFault=files,actualWriterPID=writer,actualWriterInode=inode,actualRollbackPID=rollback,actualDescendantPID=child,actualWaitpidReceipts=list(waits.values()),actualUnlinkReceipt=unlinks[0],remainingPIDs=remaining)
finally:
 evidence['changed']=[str(x) for x,h in pins.items() if hashlib.sha256(Path(x).read_bytes()).hexdigest()!=h];(root/'index.json').write_text(json.dumps(evidence,indent=2)+'\n')
assert not evidence['changed'];print(json.dumps({'passed':True,'index':str(root/'index.json')}))
