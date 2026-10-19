# SPDX-License-Identifier: MIT
"""Authored native live-worker reuse controls with actual pipe/socket/PID/file receipts.

Pinned originals are read-only behavioral oracles. No upstream driver or
assertion is modified and no JavaScript scheduler supplies group order/retries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import socket
import selectors
import select
import threading
import subprocess
import tempfile
import time

parser=argparse.ArgumentParser()
parser.add_argument('--executable',type=Path,required=True)
parser.add_argument('--cli-script',type=Path,required=True)
parser.add_argument('--suite-api',type=Path,required=True)
parser.add_argument('--directory',type=Path,required=True)
parser.add_argument('--source-root',type=Path)
parser.add_argument('--idle-gate-library',type=Path)
parser.add_argument('--held-idle-only',action='store_true')
parser.add_argument('--soft-reuse-only',action='store_true')
parser.add_argument('--compiled-input-manifest',type=Path)
parser.add_argument('--node',default='node')
args=parser.parse_args()
args.directory.mkdir(parents=True,exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='run-',dir=args.directory.resolve()))
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[args.executable.resolve(),Path(__file__).resolve(),*[p for p in args.cli_script.resolve().parent.rglob('*') if p.is_file() and not p.is_symlink()]]
node=shutil.which(args.node)
assert node,'The actual Node executable could not be resolved'
inputs.append(Path(node).resolve())
# Pin the external packages in the emitted SDK closure as well as its own bytes.
resolution_command=[node,'--input-type=module','-e',
    "import {createRequire} from 'node:module';const require=createRequire(process.argv[1]);process.stdout.write(JSON.stringify(Object.fromEntries(['zod','yaml'].map(name=>[name,require.resolve(name)]))));",
    str(args.cli_script.resolve())]
resolution=subprocess.run(resolution_command,capture_output=True,timeout=10)
(root/'sdk-dependency-resolution.json').write_text(json.dumps(dict(command=resolution_command,returncode=resolution.returncode,stdout=resolution.stdout.decode(),stderr=resolution.stderr.decode()),indent=2)+'\n')
assert resolution.returncode==0 and resolution.stderr==b'',('SDK package resolution failed',resolution.returncode,resolution.stderr)
resolved_packages=json.loads(resolution.stdout)
for package in ['zod','yaml']:
    entry=Path(resolved_packages[package]).resolve()
    assert entry.is_file(),('Missing actual SDK dependency entry',package,entry)
    dependency=None
    for parent in entry.parents:
        metadata=parent/'package.json'
        if metadata.is_file() and json.loads(metadata.read_bytes()).get('name')==package:
            dependency=parent;break
    assert dependency is not None,('Missing resolved SDK package root',package,entry)
    inputs += [p for p in dependency.rglob('*') if p.is_file() and not p.is_symlink()]
if args.source_root:
    inputs += [p for p in (args.source_root/'native/runner').rglob('*') if p.is_file()]
    inputs += [args.source_root/'src'/p for p in ['native-suite.ts','native-runner-host.ts','native-runner-idle.ts','native-runner-steps.ts','native-runner-attachments.ts','native-runner-attachment-view.ts']]
    inputs += [args.source_root/p for p in ['package.json','pnpm-lock.yaml']]
if args.held_idle_only:assert args.idle_gate_library,'Held-ACK control requires its actual syscall observer'
if args.idle_gate_library:inputs.append(args.idle_gate_library.resolve())
if args.compiled_input_manifest:inputs.append(args.compiled_input_manifest.resolve())
before={str(p):digest(p) for p in sorted(set(inputs))}
(root/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
scopes=[]

class Barrier:
    def __init__(self,mode,folder):
        self.mode=mode;self.folder=folder;self.messages=[];self.errors=[];self.cli_pid=None;self.native_pid=None;self.pidfds={};self.childfds={};self.held={};self.first_pids=set();self.body_count=0;self.setup_count=0;self.teardown_count=0
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(16);self.port=self.listener.getsockname()[1]
        self.wake,self.stop=socket.socketpair();self.thread=threading.Thread(target=self.run);self.thread.start()
    def exited(self,pid):
        poller=select.poll();poller.register(self.pidfds[pid],select.POLLIN);return bool(poller.poll(0))
    def release(self,key):
        connection=self.held.pop(key);connection.sendall(b'go\n')
    def run(self):
        selector=selectors.DefaultSelector();selector.register(self.listener,selectors.EVENT_READ,'listen');selector.register(self.wake,selectors.EVENT_READ,'stop');pending={};end=time.monotonic()+20
        try:
            while True:
                remaining=end-time.monotonic();assert remaining>0,'Original actual barrier control deadline expired'
                for key,_ in selector.select(remaining):
                    if key.data=='stop':return
                    if key.data=='listen':
                        conn,_=self.listener.accept();pending[conn]=b'';selector.register(conn,selectors.EVENT_READ,'message');continue
                    conn=key.fileobj;data=conn.recv(4096)
                    if not data:
                        selector.unregister(conn);conn.close();pending.pop(conn,None);continue
                    pending[conn]+=data;assert len(pending[conn])<=4096,'Unbounded actual idle-proof message'
                    if b'\n' not in pending[conn]:continue
                    raw,tail=pending[conn].split(b'\n',1);assert not tail;pending[conn]=b'';message=json.loads(raw)
                    assert set(message)=={'kind','pid','nativePid','workerIndex','testId','fixture','calls','child','file'}
                    assert message['kind'] in ['setup','body','teardown'] and isinstance(message['pid'],int) and message['pid']>0
                    pid=message['pid'];native=message['nativePid'];stat=Path('/proc',str(pid),'stat').read_text();assert int(stat[stat.rfind(')')+2:].split()[1])==native
                    if self.native_pid is None:
                        self.native_pid=native;parent=Path('/proc',str(native),'stat').read_text();assert int(parent[parent.rfind(')')+2:].split()[1])==self.cli_pid
                    assert native==self.native_pid
                    if pid not in self.pidfds:self.pidfds[pid]=os.pidfd_open(pid)
                    if message['child']:
                        child=message['child'];childstat=Path('/proc',str(child),'stat').read_text();assert int(childstat[childstat.rfind(')')+2:].split()[1])==pid
                        if child not in self.childfds:self.childfds[child]=os.pidfd_open(child)
                    self.messages.append({**message,'observedMonotonic':time.monotonic()});assert len(self.messages)<=16
                    if message['kind']=='setup':
                        self.setup_count+=1
                        if self.setup_count>2 and self.mode in ['failure','child']:
                            assert all(self.exited(old) for old in self.first_pids),'Fresh cohort began before all previous actual primary exits'
                        conn.sendall(b'go\n')
                    elif message['kind']=='teardown':
                        self.teardown_count+=1
                        assert not (self.folder/'result.json').exists(),'Report published before actual worker fixture callback retirement'
                        conn.sendall(b'go\n')
                        if self.mode=='failure' and message['pid'] in self.first_pids and ('body','2') in self.held:
                            # Actual native fixture teardown invocation proves the
                            # failed raw body was accepted while sibling2 is held.
                            other=next(m for m in self.messages if m['kind']=='body' and m['testId']=='2');assert not self.exited(other['pid'])
                            self.release(('body','2'))
                    else:
                        self.body_count+=1;assert message['file']==str((self.folder/'suite.mjs').resolve())
                        self.held[('body',message['testId'])]=conn
                        if self.body_count<=2:self.first_pids.add(pid)
                        if self.body_count==2:
                            assert len(self.first_pids)==2 and all(not self.exited(old) for old in self.first_pids),'Actual first wave did not overlap'
                            if self.mode=='interrupt':os.kill(native,signal.SIGINT)
                            elif self.mode=='failure':self.release(('body','1'))
                            else:self.release(('body','1'));self.release(('body','2'))
                        elif self.body_count>2:
                            if self.mode in ['reuse','plain']:
                                assert pid in self.first_pids and not self.exited(pid),'Idle transfer did not retain the same actual live PID'
                                assert message['calls']==(2 if self.mode=='reuse' else 0),'Worker fixture value did not persist across borrows'
                                assert self.setup_count==(2 if self.mode=='reuse' else 0)
                            else:assert pid not in self.first_pids and all(self.exited(old) for old in self.first_pids),'Failed/incompatible cohort was reused'
                            if self.body_count==4:
                                last=[m for m in self.messages if m['kind']=='body'][-2:];assert len({m['pid'] for m in last})==2
                                self.release(('body','3'));self.release(('body','4'))
        except BaseException as error:self.errors.append(repr(error))
        finally:
            for conn in pending:conn.close()
            selector.close()
    def finish(self):
        self.stop.sendall(b'x');self.thread.join(timeout=5);assert not self.thread.is_alive(),'Actual barrier thread remains live'
        self.listener.close();self.stop.close();self.wake.close()
        for fd in [*self.pidfds.values(),*self.childfds.values()]:
            poller=select.poll();poller.register(fd,select.POLLIN);assert poller.poll(0),'Actual observed helper/worker PID remains live after report'
            os.close(fd)
        (self.folder/'barrier.json').write_text(json.dumps({'messages':self.messages,'errors':self.errors,'pidCount':len(self.pidfds),'childCount':len(self.childfds)},indent=2)+'\n')
        assert not self.errors,self.errors

class IdleGate:
    def __init__(self,barrier,folder):
        self.barrier=barrier;self.folder=folder;self.observations=[];self.errors=[];self.held=[];self.teardowns=set();self.written=set();self.signaled=False;self.done=threading.Event()
        self.directory=tempfile.TemporaryDirectory(prefix='sc-reuse-ack-');self.path=str(Path(self.directory.name)/'gate')
        self.listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);self.listener.bind(self.path);self.listener.listen(16)
        self.wake,self.stop=socket.socketpair();self.thread=threading.Thread(target=self.run);self.thread.start()
    def run(self):
        selector=selectors.DefaultSelector();selector.register(self.listener,selectors.EVENT_READ,'listen');selector.register(self.wake,selectors.EVENT_READ,'stop');pending={};end=time.monotonic()+20
        try:
            while True:
                remaining=end-time.monotonic();assert remaining>0,'Original held-idle syscall control deadline expired'
                for key,_ in selector.select(remaining):
                    if key.data=='stop':return
                    if key.data=='listen':
                        conn,_=self.listener.accept();pending[conn]=b'';selector.register(conn,selectors.EVENT_READ,'packet');continue
                    conn=key.fileobj;data=conn.recv(65536)
                    if not data:
                        selector.unregister(conn);conn.close();pending.pop(conn,None);continue
                    pending[conn]+=data;assert len(pending[conn])<=65736
                    if b'\n' not in pending[conn]:continue
                    header,payload=pending[conn].split(b'\n',1);words=header.decode('ascii').split(' ');assert len(words)==6
                    kind=words[0];pid,parent,fd,length,result=map(int,words[1:]);assert 0<length<=65536
                    if len(payload)<length:continue
                    assert len(payload)==length and payload.endswith(b'\n')
                    record={'kind':kind,'pid':pid,'parent':parent,'fd':fd,'length':length,'result':result,'bytesHex':payload.hex(),'observedMonotonic':time.monotonic()};self.observations.append(record)
                    selector.unregister(conn);pending.pop(conn)
                    if kind=='ACK_HELD':
                        assert fd==3 and result==-1 and parent==self.barrier.native_pid and pid in self.barrier.pidfds
                        fields=payload.decode('ascii').strip().split(' ');assert len(fields)==10 and fields[0]=='WORKER_IDLE_READY' and int(fields[9])==pid
                        assert pid not in {r['pid'] for r in self.observations[:-1] if r['kind']=='ACK_HELD'}
                        self.held.append(conn)
                        if len(self.held)==2:
                            assert self.barrier.body_count==2 and self.barrier.setup_count==2 and all(not self.barrier.exited(owner) for owner in self.barrier.first_pids)
                            assert not (self.folder/'result.json').exists();os.kill(parent,signal.SIGINT);self.signaled=True
                    elif kind=='TEARDOWN_SENT':
                        assert self.signaled and pid==self.barrier.native_pid and result==length and payload.startswith(b'TEARDOWN ')
                        self.teardowns.add(payload);conn.close()
                        if len(self.teardowns)==2:
                            assert len(self.held)==2 and not self.written and not (self.folder/'result.json').exists()
                            for held in self.held:held.sendall(b'G');held.close()
                            self.held=[]
                    elif kind=='ACK_WRITTEN':
                        assert self.signaled and len(self.teardowns)==2 and result==length and fd==3 and payload.startswith(b'WORKER_IDLE_READY ')
                        self.written.add(pid);conn.close()
                        if len(self.written)==2:self.done.set()
                    else:raise AssertionError(('Unknown actual syscall observer record',record))
        except BaseException as error:self.errors.append(repr(error));self.done.set()
        finally:
            for conn in [*pending,*self.held]:conn.close()
            selector.close()
    def finish(self):
        self.done.wait(timeout=5)
        self.stop.sendall(b'x');self.thread.join(timeout=5);assert not self.thread.is_alive()
        self.listener.close();self.stop.close();self.wake.close();self.directory.cleanup()
        (self.folder/'actual-idle-gate.json').write_text(json.dumps({'observations':self.observations,'errors':self.errors,'signaled':self.signaled},indent=2)+'\n')
        assert not self.errors and self.signaled and len(self.teardowns)==2 and len(self.written)==2,(self.errors,self.observations)
        held={r['pid']:r for r in self.observations if r['kind']=='ACK_HELD'}
        for record in self.observations:
            if record['kind']=='ACK_WRITTEN':assert record['bytesHex']==held[record['pid']]['bytesHex'],'Held ACK bytes were changed'

SOURCE=r'''
import assert from 'node:assert/strict';
import process from 'node:process';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {defineNativeSuite} from SUITE_API;
const mode=MODE,port=PORT;
const event=async(kind,info,value,calls=0)=>{
 const socket=net.createConnection({port,host:'127.0.0.1'});
 const abort=()=>socket.destroy(new Error('actual native callback aborted'));
 if(info.signal)info.signal.addEventListener('abort',abort,{once:true});
 try{
  await once(socket,'connect');const data=once(socket,'data');
  socket.write(JSON.stringify({kind,pid:process.pid,nativePid:process.ppid,workerIndex:info.workerIndex,testId:info.testId??null,fixture:value?.id??0,calls,child:value?.child?.pid??0,file:info.file??null})+'\n');
  const [bytes]=await data;assert.equal(bytes.toString(),'go\n');assert(!info.signal?.aborted);return true;
 }catch(error){if(info.signal?.aborted)return false;throw error;}
 finally{if(info.signal)info.signal.removeEventListener('abort',abort);socket.destroy();}
};
let fixtureNonce=0;
const fixtures=mode==='plain'?{}:{w:{scope:'worker',setup:async({},use,info)=>{
 const value={id:++fixtureNonce,calls:0};
 if(mode==='child'){value.child=spawn(process.execPath,['-e','process.stdin.resume()'],{stdio:['pipe','ignore','ignore']});await once(value.child,'spawn');}
 await event('setup',info,value);
 await use(value);
 await event('teardown',info,value);
 if(value.child){const actual=once(value.child,'close');value.child.stdin.end();const [code,signal]=await actual;assert.equal(code,0);assert.equal(signal,null);}
}}};
export default defineNativeSuite({fullyParallel:true,fixtures,tests:Array.from({length:4},(_,index)=>({title:'actual reuse '+index,fixtures:mode==='plain'?[]:['w'],timeout:10000,run:async(values,info)=>{
 const value=values.w;const calls=value?++value.calls:0;
 const released=await event('body',info,value,calls);
 if(!released){assert.equal(mode,'interrupt');return;}
 if(mode==='failure'&&index===0)throw new Error('genuine first group failure');
 await info.step('retained actor step '+index,()=>info.attach('actual file '+index,{body:Buffer.from('actual bytes '+index)}));
 assert(info.outputDir.startsWith('/'));assert(info.outputPath('child').startsWith(info.outputDir));
}}))});
'''

def run_scope(name,mode,code,expected_hosts):
    folder=root/name;folder.mkdir();barrier=Barrier(mode,folder)
    gate=IdleGate(barrier,folder) if mode=='held-idle' else None
    source=SOURCE
    if args.soft_reuse_only:
        # Real native/SDK protocol receipts with controlled source-tagged
        # matcher operations. This is not a browser verdict proof.
        source='import {runNativeSoftAssertion} from '+json.dumps((args.suite_api.resolve().parent/'native-soft-assertions.js').as_uri())+';\nimport {nativeMatcherFailure} from '+json.dumps((args.suite_api.resolve().parent/'native-matcher-failure.js').as_uri())+';\n'+source
        source=source.replace("if(mode==='failure'&&index===0)throw new Error('genuine first group failure');","await runNativeSoftAssertion(async()=>17); process.stdout.write('ACTUAL_SOFT_SETTLED_'+info.testId+'\\n'); if(mode==='failure'&&index===0)await runNativeSoftAssertion(async()=>{throw nativeMatcherFailure(new Error('actual source-tagged mismatch prevents reuse'))});")
    source=source.replace('SUITE_API',json.dumps(args.suite_api.resolve().as_uri())).replace('MODE',json.dumps(mode)).replace('PORT',str(barrier.port));suite=folder/'suite.mjs';suite.write_text(source);suite_hash=digest(suite);suite_stat=suite.stat()
    command=[node,str(args.cli_script.resolve()),'--executable',str(args.executable.resolve()),'--suite',str(suite),'--report',str(folder/'result.json'),'--junit',str(folder/'result.xml'),'--html',str(folder/'result.html'),'--workers','2','--global-timeout','15000']
    env=os.environ.copy()
    if gate:env.update(LD_PRELOAD=str(args.idle_gate_library.resolve()),SUITECUT_REUSE_IDLE_GATE=gate.path)
    start=time.monotonic();process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True,env=env);barrier.cli_pid=process.pid;timed_out=False
    try:stdout,stderr=process.communicate(timeout=22)
    except subprocess.TimeoutExpired:
        timed_out=True;os.killpg(process.pid,signal.SIGTERM)
        try:stdout,stderr=process.communicate(timeout=5)
        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate(timeout=5)
    (folder/'native.stdout').write_bytes(stdout);(folder/'native.stderr').write_bytes(stderr);receipt={'command':command,'exit':process.returncode,'timedOut':timed_out,'seconds':time.monotonic()-start};(folder/'actual-process.json').write_text(json.dumps(receipt,indent=2)+'\n')
    gate_failure=None
    if gate:
        try:gate.finish()
        except BaseException as error:gate_failure=error
    barrier.finish()
    if gate_failure:raise gate_failure
    assert not timed_out and process.returncode==code and stderr==b'',(name,receipt,stderr)
    assert digest(suite)==suite_hash;report=json.loads((folder/'result.json').read_bytes());workers=report['workers'];attempts=report['attempts'];reuse=report['workerReuse'];groups=report['fileWorkGroups'];file=report['files'][0]
    assert 'projects' not in report and 'workGroups' not in report and len(workers)==expected_hosts and len(reuse['hosts'])==expected_hosts
    assert (int(file['device']),int(file['inode']))==(suite_stat.st_dev,suite_stat.st_ino) and file['path']==str(suite.resolve()) and file['receipt']=='nativeFileClosed'
    assert len(attempts)==(2 if mode in ['interrupt','held-idle'] else 4) and len(reuse['borrows'])==len(attempts)
    assert len({a['generation'] for a in attempts})==len(attempts)
    assert len({a['workerIndex'] for a in attempts})==expected_hosts
    for worker in workers:assert worker['exitStatusKnown'] and worker['stderr']['byteLength']==0 and worker['signal'] is None and worker['exitCode']==0
    for host in reuse['hosts']:
        actual=next(w for w in workers if w['workerIndex']==host['workerIndex']);assert host['pid']==actual['pid'] and host['receipt']=='physicalRetired' and host['retiredMs']>=host['exitedMs']
    for borrow in reuse['borrows']:
        group=groups[int(borrow['fileWorkGroupId'])-1];assert borrow['groupGeneration']==group['generation'] and borrow['receipt']==group['receipt']
        host=next(h for h in reuse['hosts'] if h['lease']==borrow['hostLease']);assert group['workerIndices']==[host['workerIndex']]
        if borrow['receipt']=='idleTransferred':assert borrow['idleRequest'] is not None and borrow['acknowledgedMs']<=borrow['transferredMs']<=host['exitedMs']
    if mode in ['reuse','plain']:
        assert len([b for b in reuse['borrows'] if b['receipt']=='idleTransferred'])==2
        assert len({a['pid'] for a in attempts})==2 and all(g['status']=='passed' for g in groups)
    else:assert all(b['receipt']=='physicalRetired' for b in reuse['borrows'])
    if mode=='interrupt':assert [g['status'] for g in groups]==['interrupted','interrupted','notRun','notRun'] and all(g['receipt']=='notAdmitted' for g in groups[2:])
    if mode=='held-idle':
        assert [g['status'] for g in groups]==['passed','passed','notRun','notRun'] and all(g['receipt']=='notAdmitted' for g in groups[2:])
        assert all(b.get('canceledIdleRequest') and b.get('canceledAcknowledgedMs') is not None for b in reuse['borrows'])
    for category in ['attachments','steps','outputs']:assert len({r['id'] for r in report.get(category,[])})==len(report.get(category,[]))
    for attachment in report.get('attachments',[]):
        assert attachment['writerExitConfirmed'] and attachment['writerExitCode']==0
        assert Path(attachment['path']).read_bytes().startswith(b'actual bytes ')
        try:os.kill(attachment['writerPid'],0)
        except ProcessLookupError:pass
        else:raise AssertionError('Actual attachment writer survived final report')
    for output in report.get('outputs',[]):
        assert output['status']=='ready' and output['readyReceipt'] and output['writerExitCode']==0 and Path(output['outputDir']).is_dir()
        attempt=next(a for a in attempts if a['generation']==output['generation']);assert (output['testId'],output['workerIndex'],output['pid'])==(attempt['testId'],attempt['workerIndex'],attempt['pid'])
    if args.soft_reuse_only:
        rows=report.get('softAssertions',[]);assert len(rows)==(1 if mode=='failure' else 0) and len({r['id'] for r in rows})==len(rows)
        assert sum(r['acceptedMismatch'] for r in rows)==(1 if mode=='failure' else 0)
        for row in rows:
            attempt=next(a for a in attempts if a['generation']==row['generation'])
            assert row['workerIndex']==attempt['workerIndex'] and row['pid']==attempt['pid'] and row['testId']==attempt['testId'] and row['receipt']=='mismatch'
        actual_stdout=''.join(bytes.fromhex(w['stdout']['data']).decode() for w in workers)
        assert all(actual_stdout.count('ACTUAL_SOFT_SETTLED_'+str(i)+'\n')==1 for i in range(1,5))
        if mode=='failure':assert attempts[0]['status']=='failed' and attempts[0]['softErrorId'] in [r['id'] for r in rows if r['acceptedMismatch']]
    assert (folder/'result.xml').is_file() and (folder/'result.html').is_file();scopes.append({'name':name,'passed':True,'actual':receipt,'report':str(folder/'result.json'),'observedWorkers':len(barrier.pidfds),'observedChildren':len(barrier.childfds)})

try:
    assert not (args.held_idle_only and args.soft_reuse_only)
    if args.soft_reuse_only:
        run_scope('same-live-pid-controller-nonce-and-genuine-soft-settlement-across-groups','reuse',0,2)
        run_scope('accepted-soft-error-disqualifies-live-cohort-reuse','failure',1,4)
    elif not args.held_idle_only:
        run_scope('same-live-host-and-worker-fixture-across-artifact-groups','reuse',0,2)
        run_scope('same-live-host-without-worker-fixtures','plain',0,2)
        run_scope('failed-cohort-physical-retirement-and-unrelated-body-completion','failure',1,4)
        run_scope('actual-worker-child-disqualifies-idle-transfer','child',0,4)
        run_scope('actual-native-sigint-retires-live-leases-and-never-admits-pending','interrupt',130,2)
    if args.idle_gate_library:run_scope('genuine-held-idle-ack-after-native-sigint-and-teardown','held-idle',130,2)
except BaseException:
    (root/'first-failure.json').write_text(json.dumps({'completed':scopes,'scope':'First authored actual worker reuse failure preserved'},indent=2)+'\n');raise
finally:
    after={str(p):digest(p) for p in sorted(set(inputs))};(root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n');assert after==before,'Actual worker reuse proof inputs changed'
(root/'index.json').write_text(json.dumps({'passed':True,'scopes':scopes,'inputsUnchanged':True,'scope':'Actual bounded one-file flat anonymous SDK worker reuse: real overlap, retained fixture/live-PID transfer, shared IDs/artifact/output fences, failure/child retirement and actual SIGINT. Hooks/retry/locks/multifile/CEF-child reuse parity remains unproved.'},indent=2)+'\n')
print(json.dumps({'passed':True,'scopes':len(scopes),'directory':str(root)}))
