# SPDX-License-Identifier: MIT
"""Authored native one-file anonymous-group CLI controls with actual native/OS callback receipts.

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
    inputs += [args.source_root/'src'/p for p in ['native-suite.ts','native-runner-host.ts']]
    inputs += [args.source_root/p for p in ['package.json','pnpm-lock.yaml']]
before={str(p):digest(p) for p in sorted(set(inputs))}
(root/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
groups=[]


def execute(name,source,code=0,options=(),barrier=None,symlink=False,changes_suite=False):
    folder=root/name
    folder.mkdir()
    suite=folder/'suite.mjs'
    header="""import assert from 'node:assert/strict';
import process from 'node:process';
import {readFileSync,writeFileSync} from 'node:fs';
import {defineNativeSuite} from SUITE_API;
const emit=(event,info)=>process.stdout.write(JSON.stringify({event,pid:process.pid,retry:info?.retry,project:info?.project?.name,file:info?.file,workerIndex:info?.workerIndex,testId:info?.testId})+'\\n');
const previous=MARKER;
const before=async({},info)=>{if(info.retry){const old=Number(readFileSync(previous,'utf8'));assert.notEqual(old,process.pid);assert.throws(()=>process.kill(old,0),e=>e.code==='ESRCH')}writeFileSync(previous,String(process.pid));emit('before',info)};
const after=async({},info)=>emit('after',info);
const fixtures={w:{scope:'worker',setup:async({},use,info)=>{emit('fixture-start',info);await use({pid:process.pid});emit('fixture-end',info)}}};
const test=(title,run,options={})=>({title,...options,run:async({},info)=>{emit(title,info);await run(info)}});
const exists=pid=>{try{process.kill(pid,0);return true}catch(error){if(error.code==='ESRCH')return false;throw error}};

""".replace('SUITE_API',json.dumps(args.suite_api.resolve().as_uri())).replace('MARKER',json.dumps(str(folder/'previous-pid')))
    if barrier is not None:
        source="const PORT="+str(barrier.port)+";\n"+BARRIER_SOURCE+source
    suite.write_text(header+source)
    suite_hash=digest(suite);suite_stat=suite.stat()
    suite_arg=suite
    if symlink:
        suite_arg=folder/'suite-alias.mjs';suite_arg.symlink_to(suite)
    command=[args.node,str(args.cli_script.resolve()),'--executable',str(args.executable.resolve()),'--suite',str(suite_arg),'--report',str(folder/'result.json'),'--junit',str(folder/'result.xml'),'--html',str(folder/'result.html'),*options]
    started=time.monotonic();process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
    if barrier:
        barrier.cli_pid=process.pid
    timed_out=False
    try:
        stdout,stderr=process.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        timed_out=True;os.killpg(process.pid,signal.SIGTERM)
        try:stdout,stderr=process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate(timeout=5)
    (folder/'native.stdout').write_bytes(stdout);(folder/'native.stderr').write_bytes(stderr)
    receipt=dict(command=command,returncode=process.returncode,timedOut=timed_out,elapsedMs=(time.monotonic()-started)*1000,suiteSha256=suite_hash,suiteAfterSha256=digest(suite),suiteDevice=suite_stat.st_dev,suiteInode=suite_stat.st_ino,suiteArg=str(suite_arg))
    (folder/'actual-process.json').write_text(json.dumps(receipt,indent=2)+'\n')
    if barrier:barrier.finish()
    assert (digest(suite)!=suite_hash) if changes_suite else (digest(suite)==suite_hash)
    assert not timed_out and process.returncode==code,(name,receipt,stderr)
    assert stderr==b'',(name,stderr)
    report=json.loads((folder/'result.json').read_bytes());workers={w['workerIndex']:w for w in report['workers']}
    for worker in workers.values():
        assert worker['exitStatusKnown'] and worker['stderr']['byteLength']==0
        try:os.kill(worker['pid'],0)
        except ProcessLookupError:pass
        else:raise AssertionError('Reported worker is still physically alive')
    generations=set()
    for attempt in report['attempts']:
        assert attempt['generation'] not in generations;generations.add(attempt['generation'])
        assert workers[attempt['workerIndex']]['pid']==attempt['pid']
    events=[json.loads(line) for worker in workers.values() for line in bytes.fromhex(worker['stdout']['data']).splitlines()]
    for group in report.get('serialGroups',[]):
        ids=set(group['testIds'])
        for omission in group['omissions']:
            assert omission['testId'] in ids and omission['causeTestId'] in ids
            assert any(a['testId']==omission['causeTestId'] and a['generation']==omission['causeGeneration'] and a['retry']==omission['retry'] and not a['expected'] for a in report['attempts'])
            assert not any(a['testId']==omission['testId'] and a['retry']==omission['retry'] for a in report['attempts'])
    if 'files' in report:
        assert len(report['files'])==1 and report['files'][0]['path']==str(suite.resolve())
        assert (int(report['files'][0]['device']),int(report['files'][0]['inode']))==(suite_stat.st_dev,suite_stat.st_ino)
        assert report['files'][0]['receipt']=='nativeFileClosed' and report['files'][0]['retiredMs']>=report['files'][0]['openedMs']
    for artifact in report.get('attachments',[]):
        assert artifact['writerExitConfirmed'] and artifact['writerExitCode']==0
        try:os.kill(artifact['writerPid'],0)
        except ProcessLookupError:pass
        else:raise AssertionError('Actual attachment writer survived report publication')
    assert (folder/'result.xml').is_file() and (folder/'result.html').is_file()
    groups.append(dict(name=name,passed=True,report=str(folder/'result.json'),actualProcess=receipt))
    return report,events


def only(report):
    assert len(report['serialGroups'])==1
    return report['serialGroups'][0]


def bodies(events):return [(e['event'],e['retry']) for e in events if e['event'] in ['a','b','c','outside']]


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
  const reply=once(socket,'data');socket.write(JSON.stringify({file:info.file,pid:process.pid,nativePid:process.ppid,retry:info.retry,testId:info.testId,workerIndex:info.workerIndex})+'\n');
  const [bytes]=await reply;assert.equal(bytes.toString(),'go\n');return true;
 }catch(error){if(info.signal.aborted){emit('actual-abort',info);return false;}throw error;}
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
                            message=json.loads(raw);assert set(message)=={'file','pid','nativePid','retry','testId','workerIndex'}
                            assert isinstance(message['pid'],int) and message['pid']>0
                            if len(self.messages) and len(self.messages)%self.batch==0:
                                for fd in self.pidfds[-self.batch:]:
                                    exited=select.poll();exited.register(fd,select.POLLIN);assert exited.poll(0), 'Next pool batch admitted before actual prior PID exit'
                            self.pidfds.append(os.pidfd_open(message['pid']))
                            self.messages.append({**message,'receivedMonotonic':time.monotonic()});held.append(conn)
                            assert len(self.messages)<=self.count, 'More anonymous file callbacks overlapped than the declared barrier'
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

    def finish(self):
        self.close()
        (root/('barrier-'+str(self.port)+'.json')).write_text(json.dumps(dict(messages=self.messages,errors=self.errors),indent=2)+'\n')
        assert not self.errors and len(self.messages)==self.count,(self.messages,self.errors)


def file_groups(report):
    assert 'projects' not in report and 'workGroups' not in report and 'resourceLocks' not in report
    assert len(report['files'])==1
    file=report['files'][0];groups=report['fileWorkGroups'];workers={w['workerIndex']:w for w in report['workers']}
    assert len({g['id'] for g in groups})==len(groups)
    admitted=[g for g in groups if g['generation'] is not None]
    assert len({g['generation'] for g in admitted})==len(admitted)
    owned=set();tests=set()
    for group in groups:
        assert group['fileId']==file['id'] and set(group['testIds'])<=set(file['selectedTestIds']) and not tests.intersection(group['testIds']);tests.update(group['testIds'])
        assert 'projectId' not in group
        if group['receipt']=='physicalRetired':
            assert group['generation'] is not None and group['workerIndices']
            for index in group['workerIndices']:
                assert index not in owned and workers[index]['exitStatusKnown'];owned.add(index)
        else:
            assert group['receipt'] in ['neverStarted','notAdmitted'] and not group['workerIndices'] and group['status']=='notRun'
            assert (group['generation'] is None)==(group['receipt']=='notAdmitted')
    assert tests==set(file['selectedTestIds']) and owned==set(workers)
    for attempt in report['attempts']:
        group=next(g for g in groups if attempt['testId'] in g['testIds']);assert attempt['workerIndex'] in group['workerIndices']
    for lease in report.get('fileResourceLocks',[]):
        group=next(g for g in groups if g['id']==lease['fileWorkGroupId'])
        assert lease['fileId']==file['id'] and lease['generation']==group['generation'] and lease['receipt']==group['receipt']
        assert 'projectId' not in lease and len(lease['names'])==len(set(lease['names']))
    return groups

try:
    gate=Barrier(count=3)
    r,e=execute('anonymous-three-actual-overlap-and-global-artifact-output-identities',r'''
import {fileURLToPath} from 'node:url';
export default defineNativeSuite({fullyParallel:true,tests:['a','b','c'].map(title=>test(title,async(info)=>{assert.equal(info.project,undefined);assert.equal(info.file,fileURLToPath(import.meta.url));assert.equal(await barrier(info),true);await info.step(title,()=>info.attach(title,{body:Buffer.from(title)}));emit(info.outputDir,info)}))});
''',options=('--workers','3'),barrier=gate)
    g=file_groups(r);assert len(g)==3 and all(x['mode']=='parallel' for x in g)
    assert len({x['pid'] for x in gate.messages})==3 and len({x['workerIndex'] for x in gate.messages})==3
    assert len(r['attempts'])==3 and len(r['attachments'])==3 and len(r['steps'])==3
    assert len({x['id'] for x in r['attachments']})==3 and len({x['id'] for x in r['steps']})==3
    assert len(r['outputs'])==3 and len({x['id'] for x in r['outputs']})==3 and len({x['outputDir'] for x in r['outputs']})==3
    for output in r['outputs']:
        assert output['status']=='ready' and output['readyReceipt'] and output['writerExitCode']==0
        attempt=next(a for a in r['attempts'] if a['testId']==output['testId'])
        assert (output['generation'],output['workerIndex'],output['pid'])==(attempt['generation'],attempt['workerIndex'],attempt['pid'])
        try:os.kill(output['writerPid'],0)
        except ProcessLookupError:pass
        else:raise AssertionError('Actual output actor survived anonymous group report')

    r,e=execute('anonymous-default-without-parallel-mode-retains-one-ordered-group',r'''
let ran=false;
export default defineNativeSuite({tests:[test('a',async()=>{ran=true}),test('b',async()=>assert(ran))]});
''',options=('--workers','3'))
    g=file_groups(r);assert len(g)==1 and g[0]['mode']=='default' and g[0]['testIds']==['1','2'] and len(r['workers'])==1

    r,e=execute('anonymous-nested-default-override-and-explicit-parallel-region',r'''
let ran=false;
export default defineNativeSuite({fullyParallel:true,entries:[{title:'ordered',mode:'default',entries:[test('a',async()=>{ran=true}),test('b',async()=>assert(ran))]},{title:'parallel',mode:'parallel',entries:[test('c',async()=>{}),test('d',async()=>{})]}]});
''',options=('--workers','2'))
    g=file_groups(r);assert [x['testIds'] for x in g]==[['1','2'],['3'],['4']] and len(r['workers'])==3

    r,e=execute('anonymous-outermost-serial-whole-group-retry-new-physical-worker',r'''
export default defineNativeSuite({fullyParallel:true,entries:[{title:'serial',mode:'serial',hooks:{beforeAll:[{run:before}],afterAll:[{run:after}]},entries:[test('a',async()=>{}),{title:'inner',mode:'serial',entries:[test('b',async(info)=>{if(!info.retry)throw new Error('actual serial failure')},{retries:1})]}]},test('c',async()=>{})]});
''',options=('--workers','2'))
    g=file_groups(r);assert [x['mode'] for x in g]==['serial','parallel'] and len(g[0]['workerIndices'])==2
    assert [(x['event'],x['retry']) for x in e if x['event'] in ['a','b']]==[('a',0),('b',0),('a',1),('b',1)]
    assert r['summary']['flaky']==1 and len(r['attempts'])==5

    gate=Barrier()
    r,e=execute('anonymous-inherited-lock-skips-blocked-sibling-disjoint-overlap',r'''
const owner=previous+'-owner';
export default defineNativeSuite({fullyParallel:true,entries:[{title:'locked',lock:'shared',entries:[test('a',async(info)=>{writeFileSync(owner,String(process.pid));assert.equal(await barrier(info),true);await info.attach('a-file',{body:Buffer.from('actual file')})}),test('b',async()=>assert(!exists(Number(readFileSync(owner,'utf8')))))]},test('c',async(info)=>assert.equal(await barrier(info),true),{lock:'other'})]});
''',options=('--workers','2'),barrier=gate)
    file_groups(r);assert {x['testId'] for x in gate.messages}=={'1','3'} and [x['names'] for x in r['fileResourceLocks']]==[['shared'],['shared'],['other']]

    r,e=execute('anonymous-focus-original-test-id-shard-compose-before-grouping',r'''
export default defineNativeSuite({fullyParallel:true,tests:[test('a',async()=>assert.fail('unfocused')),...['b','c','d'].map(title=>test(title,async()=>{},{only:true}))]});
''',options=('--workers','2','--test-id','2','--test-id','3','--test-id','4','--shard','2/2'))
    g=file_groups(r);assert len(g)==1 and g[0]['testIds']==['4'] and [a['testId'] for a in r['attempts']]==['4']

    r,e=execute('anonymous-single-worker-original-report-and-info-path-unchanged',r'''
let beforeCount=0;
export default defineNativeSuite({fullyParallel:true,hooks:{beforeAll:[{run:async()=>{++beforeCount}}]},tests:[test('a',async(info)=>{assert.equal(beforeCount,1);assert.equal(info.file,undefined);assert.equal(info.project,undefined)}),test('b',async()=>assert.equal(beforeCount,1))]});
''',options=('--workers','1'))
    assert all(key not in r for key in ['projects','workGroups','files','fileWorkGroups','fileResourceLocks']) and len(r['workers'])==1 and len(r['attempts'])==2

    r,e=execute('anonymous-expected-failure-keeps-global-final-failure-policy',r'''
export default defineNativeSuite({fullyParallel:true,tests:[test('a',async()=>{throw new Error('expected static')},{expectedFailure:true}),test('b',async(info)=>{await info.fail('expected runtime');throw new Error('expected runtime')}),test('c',async()=>{})]});
''',options=('--workers','2','--max-failures','1'))
    g=file_groups(r);assert all(x['status']=='passed' for x in g) and len(r['attempts'])==3 and all(a['expected'] for a in r['attempts'])

    gate=Barrier()
    r,e=execute('anonymous-shared-final-failure-limit-stops-live-and-pending-groups',r'''
const held=async(info)=>{await new Promise(resolve=>{const abort=()=>{emit('actual-abort',info);resolve()};if(info.signal.aborted)abort();else info.signal.addEventListener('abort',abort,{once:true})})};
export default defineNativeSuite({fullyParallel:true,tests:[test('a',async(info)=>{assert.equal(await barrier(info),true);throw new Error('actual final failure')}),test('b',async(info)=>{assert.equal(await barrier(info),true);await held(info)}),test('never',async()=>assert.fail('failure cap admitted pending group'))]});
''',code=1,options=('--workers','2','--max-failures','1'),barrier=gate)
    g=file_groups(r);assert [x['status'] for x in g]==['failed','interrupted','notRun'] and len(r['attempts'])==2
    assert any(x['event']=='actual-abort' for x in e) and g[2]['receipt']=='notAdmitted'

    gate=Barrier(release=False)
    r,e=execute('anonymous-original-global-deadline-cancels-both-retains-pending-identity',r'''
export default defineNativeSuite({fullyParallel:true,tests:[...['a','b'].map(title=>test(title,async(info)=>assert.equal(await barrier(info),false),{timeout:5000})),test('never',async()=>assert.fail('deadline admitted pending group'))]});
''',code=1,options=('--workers','2','--global-timeout','1600'),barrier=gate)
    g=file_groups(r);assert [x['status'] for x in g]==['interrupted','interrupted','notRun'] and len(r['attempts'])==2
    assert len([x for x in e if x['event']=='actual-abort'])==2 and any('original global timeout of 1600ms exceeded' in x for x in r['globalErrors'])

    gate=Barrier(release=False,interrupt=True)
    r,e=execute('anonymous-actual-native-sigint-no-new-attempts',r'''
export default defineNativeSuite({fullyParallel:true,tests:[...['a','b'].map(title=>test(title,async(info)=>assert.equal(await barrier(info),false),{timeout:5000})),test('never',async()=>assert.fail('SIGINT admitted pending group'))]});
''',code=130,options=('--workers','2'),barrier=gate)
    g=file_groups(r);assert [x['status'] for x in g]==['interrupted','interrupted','notRun'] and len(r['attempts'])==2 and len([x for x in e if x['event']=='actual-abort'])==2

    r,e=execute('anonymous-symlink-selects-genuine-native-canonical-file',r'''
import {fileURLToPath} from 'node:url';
export default defineNativeSuite({fullyParallel:true,tests:[test('a',async(info)=>assert.equal(info.file,fileURLToPath(import.meta.url))),test('b',async(info)=>assert.equal(info.project,undefined))]});
''',options=('--workers','2'),symlink=True)
    file_groups(r)

    r,e=execute('anonymous-replacement-during-real-module-load-rejected-before-callback',r'''
import {renameSync} from 'node:fs';import {fileURLToPath} from 'node:url';
const own=fileURLToPath(import.meta.url);writeFileSync(own+'.replacement','actual replaced native suite bytes');renameSync(own+'.replacement',own);
export default defineNativeSuite({fullyParallel:true,tests:[test('never',async()=>assert.fail('changed actual suite admitted callback'))]});
''',code=1,options=('--workers','2'),changes_suite=True)
    assert not r['attempts'] and any('suite file identity/version changed' in x for x in r['globalErrors'])

    r,e=execute('anonymous-no-deps-requires-genuine-declared-projects',r'''
export default defineNativeSuite({fullyParallel:true,tests:[test('never',async()=>assert.fail('project selector admitted anonymous callback'))]});
''',code=1,options=('--workers','2','--no-deps'))
    assert not r['attempts'] and any('project selector requires declared projects' in x for x in r['globalErrors'])

    r,e=execute('anonymous-selected-empty-shard-keeps-real-file-without-fake-attempts',r'''
export default defineNativeSuite({fullyParallel:true,tests:[test('never',async()=>assert.fail('empty shard admitted callback'))]});
''',options=('--workers','2','--shard','2/2'))
    g=file_groups(r);assert len(g)==1 and g[0]['testIds']==[] and g[0]['mode']=='default' and not r['attempts'] and len(r['workers'])==1

    r,e=execute('anonymous-unknown-native-test-id-rejected-before-callback',r'''
export default defineNativeSuite({fullyParallel:true,tests:[test('never',async()=>assert.fail('unknown ID admitted callback'))]});
''',code=1,options=('--workers','2','--test-id','99'))
    assert not r['attempts'] and any('Unknown or duplicate native selected test identity' in x for x in r['globalErrors'])

except BaseException:
    (root/'first-failure.json').write_text(json.dumps(dict(completedGroups=groups,scope='First anonymous-file actual adapter failure preserved'),indent=2)+'\n')
    raise
finally:
    after={str(p):digest(p) for p in sorted(set(inputs))}
    (root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    assert after==before,'Actual proof inputs changed during execution'
(root/'index.json').write_text(json.dumps(dict(passed=True,groups=groups,inputsUnchanged=True,scope='Authored actual native one-file anonymous grouping with genuine file identity, IO overlap, shared original stop policy and physical file/output/worker receipts. No discovery, mixed file/project input, import closure pinning or worker reuse parity claim.'),indent=2)+'\n')
print(json.dumps(dict(passed=True,groups=len(groups),directory=str(root))))
