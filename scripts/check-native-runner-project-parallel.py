# SPDX-License-Identifier: MIT
"""Actual native project selector/test-ID/shard composition and physical receipts.

Authored assertions use the pinned public project behavior as an oracle. No
external runner/JS scheduler or modified upstream driver is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import tempfile
import socket
import selectors
import select
import threading

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--cli-script', type=Path, required=True)
parser.add_argument('--suite-api', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--source-root', type=Path)
parser.add_argument('--node', default=shutil.which('node'))
args = parser.parse_args()
assert args.node
requested = args.directory.resolve()
requested.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='run-', dir=requested))
inputs = [args.executable.resolve(), args.suite_api.resolve(), Path(__file__).resolve(), *sorted(path for path in args.cli_script.resolve().parent.rglob('*') if path.is_file() and not path.is_symlink())]
if args.source_root:
    inputs.extend(args.source_root/name for name in ['native/runner/project_pool.h','native/runner/native_continuation.h','native/runner/project_descendants.h','native/runner/host_process.h','native/runner/suite_deadline.h','native/runner/test_steps.h','native/runner/runner_steps.h','native/runner/runner_attachments.h','native/runner/project_selection.h','native/runner/project_pattern.h','native/runner/native_runner.cc','native/runner/project_plan.h','native/runner/project_metadata.h','native/runner/project_report.h','native/runner/runner_report.h','native/runner/retry_plan.h','native/runner/CMakeLists.txt','src/native-suite.ts','src/native-runner-host.ts','src/native-runner-cli.ts','src/native-runner.ts','src/native.ts'])
inputs = sorted(set(inputs))
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(path): digest(path) for path in inputs}
(root/'inputs-before.json').write_text(json.dumps(before, indent=2)+'\n')
groups = []


def execute(name, source, code=0, options=(), barrier=None):
    folder = root/name
    folder.mkdir()
    suite = folder/'suite.mjs'
    if barrier is not None:
        source = "const PORT="+str(barrier.port)+";\n"+BARRIER_SOURCE+source
    suite.write_text("import assert from 'node:assert/strict';\nimport process from 'node:process';\nimport {defineNativeSuite} from "+json.dumps(args.suite_api.resolve().as_uri())+";\nconst log=(event,info)=>process.stdout.write(JSON.stringify({event,pid:process.pid,project:info?.project?.name,retry:info?.retry})+'\\n');\n"+source)
    suite_hash = digest(suite)
    command = [args.node, str(args.cli_script.resolve()), '--executable', str(args.executable.resolve()), '--suite', str(suite), '--report', str(folder/'result.json'), '--workers','2',*options]
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    if barrier is not None:barrier.cli_pid=process.pid
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate(timeout=5)
    (folder/'native.stdout').write_bytes(stdout)
    (folder/'native.stderr').write_bytes(stderr)
    receipt = {'suiteSha256':suite_hash, 'command': command, 'returncode': process.returncode, 'timedOut': timed_out, 'elapsedMs': (time.monotonic()-start)*1000}
    (folder/'actual-process.json').write_text(json.dumps(receipt, indent=2)+'\n')
    assert digest(suite) == suite_hash, 'Authored module changed during actual execution'
    if barrier is not None:
        barrier.close()
        (folder/'actual-barrier.json').write_text(json.dumps({'messages':barrier.messages,'errors':barrier.errors},indent=2)+'\n')
        assert not barrier.errors, (name, barrier.errors)
        assert len(barrier.messages)==barrier.count, (name, barrier.messages)
    assert not timed_out and process.returncode == code, (name, receipt, stderr)
    assert stderr == b'', (name, stderr)
    value = json.loads((folder/'result.json').read_bytes())
    workers = {worker['workerIndex']: worker for worker in value['workers']}
    for worker in workers.values():
        assert worker['exitStatusKnown'] is True
        assert worker['stderr']['byteLength'] == 0
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('Actual project worker still alive at report publication')
    for project in value.get('projects', []):
        assert all(worker in workers for worker in project['workerIndices'])
    for attempt in value['attempts']:
        owner = [project for project in value.get('projects', []) if attempt['testId'] in project['testIds']]
        assert len(owner) == 1
        assert attempt['testId'] in owner[0]['selectedTestIds']
        assert attempt['workerIndex'] in owner[0]['workerIndices']
        assert workers[attempt['workerIndex']]['pid'] == attempt['pid']
    assert all(project['selectionRole'] in ['primary','dependency','teardown','notSelected'] for project in value.get('projects', []))
    assert len({attempt['generation'] for attempt in value['attempts']}) == len(value['attempts'])
    events = [json.loads(line) for worker in workers.values() for line in bytes.fromhex(worker['stdout']['data']).splitlines()]
    groups.append({'name': name, 'processAndOwnershipPassed': True, 'report': str(folder/'result.json'), 'actualProcess': receipt})
    return value, events


BARRIER_SOURCE = r"""
import net from 'node:net';
import fs from 'node:fs';
import {once} from 'node:events';
const barrier=async(info)=>{
 const socket=net.createConnection({port:PORT,host:'127.0.0.1'});
 const abort=()=>socket.destroy(new Error('actual native signal aborted barrier'));
 info.signal.addEventListener('abort',abort,{once:true});
 try{
  await once(socket,'connect');if(info.signal.aborted)abort();
  const reply=once(socket,'data');socket.write(JSON.stringify({project:info.project.name,pid:process.pid,nativePid:process.ppid,retry:info.retry})+'\n');
  const [bytes]=await reply;assert.equal(bytes.toString(),'go\n');return true;
 }catch(error){if(info.signal.aborted){log('actual-abort',info);return false;}throw error;}
 finally{info.signal.removeEventListener('abort',abort);socket.destroy();}
};
"""
class Barrier:
    def __init__(self, count=2, release=True, batch=None, interrupt=False):
        self.count=count;self.release=release;self.interrupt=interrupt;self.cli_pid=None;self.batch=batch or count;self.messages=[];self.errors=[];self.pidfds=[]
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(8);self.port=self.listener.getsockname()[1]
        self.wake,self.stop=socket.socketpair();self.thread=threading.Thread(target=self.run);self.thread.start()
    def run(self):
        selector=selectors.DefaultSelector();selector.register(self.listener,selectors.EVENT_READ,'listen');selector.register(self.wake,selectors.EVENT_READ,'stop');pending={};held=[]
        try:
            while True:
                for key,_ in selector.select():
                    if key.data=='stop':return
                    if key.data=='listen':
                        conn,_=self.listener.accept();pending[conn]=b'';selector.register(conn,selectors.EVENT_READ,'body')
                    else:
                        conn=key.fileobj;data=conn.recv(4096)
                        if not data:
                            selector.unregister(conn);conn.close();pending.pop(conn,None);continue
                        pending[conn]+=data
                        assert len(pending[conn])<=4096, 'Unbounded actual barrier message'
                        if b'\n' in pending[conn]:
                            raw,tail=pending[conn].split(b'\n',1);assert not tail
                            message=json.loads(raw);assert set(message)=={'project','pid','nativePid','retry'}
                            assert isinstance(message['pid'],int) and message['pid']>0
                            if len(self.messages) and len(self.messages)%self.batch==0:
                                for fd in self.pidfds[-self.batch:]:
                                    exited=select.poll();exited.register(fd,select.POLLIN);assert exited.poll(0), 'Next pool batch admitted before actual prior PID exit'
                            self.pidfds.append(os.pidfd_open(message['pid']))
                            self.messages.append({**message,'receivedMonotonic':time.monotonic()});held.append(conn)
                            assert len(self.messages)<=self.count, 'More project callbacks overlapped than the declared barrier'
                            if len(self.messages)%self.batch==0 and self.release:
                                for waiting in held[-self.batch:]:waiting.sendall(b'go\n')
                            if len(self.messages)==self.count and self.interrupt:
                                roots={item['nativePid'] for item in self.messages};assert len(roots)==1
                                native_pid=roots.pop();stat=Path('/proc',str(native_pid),'stat').read_text();assert int(stat[stat.rfind(')')+2:].split()[1])==self.cli_pid
                                os.kill(native_pid,signal.SIGINT)
                            pending[conn]=b''
        except BaseException as error:self.errors.append(repr(error))
        finally:
            for conn in pending:conn.close()
            selector.close()
    def close(self):
        self.stop.sendall(b'x');self.thread.join(timeout=5);assert not self.thread.is_alive(),'Actual barrier thread still live'
        self.listener.close();self.stop.close();self.wake.close()
        for fd in self.pidfds:os.close(fd)

try:
    gate=Barrier()
    value,events=execute('two-native-project-bodies-overlap', """
const project=name=>({name,metadata:{name},tests:[{title:name,timeout:5000,run:async({},info)=>{assert.equal(info.project.metadata.name,name);log('entered',info);assert.equal(await barrier(info),true);log('released',info)}}]});
export default defineNativeSuite({projects:[project('a'),project('b')]});
""",barrier=gate)
    assert {message['project'] for message in gate.messages}=={'a','b'}
    assert len({message['pid'] for message in gate.messages})==2
    assert {attempt['pid'] for attempt in value['attempts']}=={message['pid'] for message in gate.messages}
    assert [project['status'] for project in value['projects']]==['passed','passed']
    assert len(value['attempts'])==2
    gate=Barrier()
    value,events=execute('dependency-exit-parallel-steps-files-and-teardown-exit', """
const base=new URL('./',import.meta.url);
const path=name=>new URL(name+'.pid',base);
const dead=name=>{const pid=Number(fs.readFileSync(path(name),'utf8'));assert.throws(()=>process.kill(pid,0),{code:'ESRCH'})};
const body=name=>({name,dependencies:['setup'],metadata:{name},tests:[{title:name,timeout:5000,fixtures:['worker'],run:async({worker},info)=>{
 assert.equal(worker.pid,process.pid);assert.equal(worker.project,name);dead('setup');fs.writeFileSync(path(name),String(process.pid));
 await info.step('outer '+name,async()=>{assert.equal(await barrier(info),true);await info.step('inner '+name,async step=>{await step.attach('actual '+name,{body:Buffer.from(name),contentType:'text/plain'});});});log('body',info);
}}]});
export default defineNativeSuite({fixtures:{worker:{scope:'worker',setup:async({},use,info)=>{assert(Object.isFrozen(info.project));await use({pid:process.pid,project:info.project.name});log('fixture-retired',info)}}},projects:[
 {name:'setup',default:false,teardown:'cleanup',tests:[{title:'setup',run:()=>fs.writeFileSync(path('setup'),String(process.pid))}]},body('a'),body('b'),
 {name:'cleanup',default:false,tests:[{title:'cleanup',run:({},info)=>{dead('setup');dead('a');dead('b');log('cleanup',info)}}]}
]});
""",barrier=gate)
    assert [project['status'] for project in value['projects']]==['passed']*4
    assert len({attempt['pid'] for attempt in value['attempts']})==4
    assert len(value['workers'])==4, 'First actual catalog worker was replaced by an unnecessary preflight host'
    assert len(value['steps'])==4 and len({step['id'] for step in value['steps']})==4
    assert all(step['receipt']=='settled' and step['receiptOK'] is True for step in value['steps'])
    assert len(value['attachments'])==2 and len({item['id'] for item in value['attachments']})==2
    for item in value['attachments']:
        assert item['writerExitConfirmed'] is True and item['writerExitCode']==0
        project=next(project for project in value['projects'] if item['testId'] in project['testIds'])
        assert Path(item['path']).read_bytes()==project['name'].encode()
        assert item['stepId'] in {step['id'] for step in value['steps'] if step['pid']==item['pid']}

    gate=Barrier(count=4,batch=2)
    value,events=execute('pool-two-bounded-batches-after-actual-exit', """
const project=name=>({name,tests:[{title:name,timeout:5000,run:async({},info)=>{assert.equal(await barrier(info),true);log('body',info)}}]});
export default defineNativeSuite({projects:['a','b','c','d'].map(project)});
""",barrier=gate)
    assert [message['project'] for message in gate.messages[:2]] in [['a','b'],['b','a']]
    assert {message['project'] for message in gate.messages[2:]}=={'c','d'}
    assert len(value['workers'])==4 and all(project['status']=='passed' for project in value['projects'])


    gate=Barrier()
    value,events=execute('retry-waits-batch-exit-with-global-generation-and-failure-policy', """
const bpid=new URL('./b.pid',import.meta.url);
export default defineNativeSuite({projects:[
 {name:'a',tests:[{title:'retry',timeout:5000,retries:1,run:async({},info)=>{if(info.retry===0){assert.equal(await barrier(info),true);throw new Error('actual first attempt failure')}const pid=Number(fs.readFileSync(bpid,'utf8'));assert.throws(()=>process.kill(pid,0),{code:'ESRCH'});log('actual-retry',info)}}]},
 {name:'b',tests:[{title:'b',timeout:5000,run:async({},info)=>{fs.writeFileSync(bpid,String(process.pid));assert.equal(await barrier(info),true);log('b',info)}}]}
]});
""",options=['--max-failures','1'],barrier=gate)
    assert [project['status'] for project in value['projects']]==['passed','passed']
    assert value['summary']['flaky']==1 and len(value['attempts'])==3
    assert {attempt['generation'] for attempt in value['attempts']}=={'1','2','3'}
    assert len({attempt['pid'] for attempt in value['attempts']})==3 and len(value['workers'])==3
    assert any(event['event']=='actual-retry' and event['retry']==1 for event in events)
    gate=Barrier()
    value,events=execute('one-global-failure-cap-stops-active-and-pending-projects', """
const body=(name,fail=false)=>({title:name,timeout:5000,run:async({},info)=>{assert.equal(await barrier(info),true);if(fail)throw new Error('actual parallel failure');await new Promise(resolve=>{const aborted=()=>{log('actual-abort',info);resolve()};if(info.signal.aborted)aborted();else info.signal.addEventListener('abort',aborted,{once:true})})}});
export default defineNativeSuite({projects:[{name:'fail',teardown:'cleanup',tests:[body('fail',true)]},{name:'held',tests:[body('held')]},{name:'pending',tests:[{title:'not admitted',run:()=>assert.fail('global failure policy admitted queued project')}]},{name:'cleanup',tests:[{title:'not admitted cleanup',run:()=>assert.fail('global stop admitted teardown author work')}]}]});
""",code=1,options=['--max-failures','1'],barrier=gate)
    assert [project['status'] for project in value['projects']]==['failed','interrupted','notRun','notRun']
    assert len(value['attempts'])==2 and len(value['unrun'])==2
    assert all(project['workerIndices']==[] for project in value['projects'][2:])
    assert any(event['event']=='actual-abort' and event['project']=='held' for event in events)
    assert any('maximum failure count reached' in error for error in value['globalErrors'])

    gate=Barrier(release=False)
    value,events=execute('one-original-global-deadline-cancels-both-actual-bodies', """
const project=name=>({name,tests:[{title:name,timeout:5000,run:async({},info)=>{assert.equal(await barrier(info),false)}}]});
export default defineNativeSuite({projects:[project('a'),project('b'),{name:'pending',tests:[{title:'not admitted',run:()=>assert.fail('queued project admitted beyond original deadline')}]}]});
""",code=1,options=['--global-timeout','1600'],barrier=gate)
    assert [project['status'] for project in value['projects']]==['interrupted','interrupted','notRun']
    assert [attempt['status'] for attempt in value['attempts']]==['interrupted','interrupted']
    assert {event['project'] for event in events if event['event']=='actual-abort'}=={'a','b'}
    assert any('original global timeout of 1600ms exceeded' in error for error in value['globalErrors'])

    gate=Barrier(release=False,interrupt=True)
    value,events=execute('actual-native-sigint-cancels-both-without-queued-admission', """
const project=name=>({name,tests:[{title:name,timeout:5000,run:async({},info)=>{assert.equal(await barrier(info),false)}}]});
export default defineNativeSuite({projects:[project('a'),project('b'),{name:'pending',tests:[{title:'not admitted',run:()=>assert.fail('queued project admitted after actual SIGINT')}]}]});
""",code=130,barrier=gate)
    assert [project['status'] for project in value['projects']]==['interrupted','interrupted','notRun']
    assert {event['project'] for event in events if event['event']=='actual-abort'}=={'a','b'}
    assert any('actual SIGINT' in error for error in value['globalErrors'])

except BaseException:
    (root/'first-failure.json').write_text(json.dumps({'completedGroups':groups,'scope':'First actual native parallel failure preserved; no assertion retries'},indent=2)+'\n')
    raise
finally:
    after={str(path):digest(path) for path in inputs}
    (root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    assert before==after,'Actual native executable/SDK/checker inputs changed'
(root/'index.json').write_text(json.dumps({'passed':True,'groups':groups,'inputsUnchanged':True,'scope':'Authored actual native project pool overlap/stop proofs, real process/callback/file ownership; not full parallel parity'},indent=2)+'\n')
print('Actual native parallel projects:',len(groups),'groups passed;',root)
