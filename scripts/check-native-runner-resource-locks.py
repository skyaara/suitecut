# SPDX-License-Identifier: MIT
"""Authored named-resource-lock CLI controls with actual native/OS callback receipts.

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


def execute(name,source,code=0,options=(),barrier=None):
    folder=root/name
    folder.mkdir()
    suite=folder/'suite.mjs'
    header="""import assert from 'node:assert/strict';
import process from 'node:process';
import {readFileSync,writeFileSync} from 'node:fs';
import {defineNativeSuite} from SUITE_API;
const emit=(event,info)=>process.stdout.write(JSON.stringify({event,pid:process.pid,retry:info?.retry,project:info?.project?.name,workerIndex:info?.workerIndex,testId:info?.testId})+'\\n');
const previous=MARKER;
const before=async({},info)=>{if(info.retry){const old=Number(readFileSync(previous,'utf8'));assert.notEqual(old,process.pid);assert.throws(()=>process.kill(old,0),e=>e.code==='ESRCH')}writeFileSync(previous,String(process.pid));emit('before',info)};
const after=async({},info)=>emit('after',info);
const fixtures={w:{scope:'worker',setup:async({},use,info)=>{emit('fixture-start',info);await use({pid:process.pid});emit('fixture-end',info)}}};
const test=(title,run,options={})=>({title,...options,run:async({},info)=>{emit(title,info);await run(info)}});
const exists=pid=>{try{process.kill(pid,0);return true}catch(error){if(error.code==='ESRCH')return false;throw error}};

""".replace('SUITE_API',json.dumps(args.suite_api.resolve().as_uri())).replace('MARKER',json.dumps(str(folder/'previous-pid')))
    suite.write_text(header+source)
    suite_hash=digest(suite)
    command=[args.node,str(args.cli_script.resolve()),'--executable',str(args.executable.resolve()),'--suite',str(suite),'--report',str(folder/'result.json'),'--junit',str(folder/'result.xml'),'--html',str(folder/'result.html'),*options]
    started=time.monotonic();process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
    if barrier:
        barrier.cli_pid=process.pid;barrier.ready.set()
    timed_out=False
    try:
        stdout,stderr=process.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        timed_out=True;os.killpg(process.pid,signal.SIGTERM)
        try:stdout,stderr=process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);stdout,stderr=process.communicate(timeout=5)
    (folder/'native.stdout').write_bytes(stdout);(folder/'native.stderr').write_bytes(stderr)
    receipt=dict(command=command,returncode=process.returncode,timedOut=timed_out,elapsedMs=(time.monotonic()-started)*1000,suiteSha256=suite_hash)
    (folder/'actual-process.json').write_text(json.dumps(receipt,indent=2)+'\n')
    if barrier:barrier.finish()
    assert digest(suite)==suite_hash
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
    for lock in report.get('resourceLocks',[]):
        project=next(p for p in report['projects'] if p['id']==lock['projectId'])
        assert lock['receipt']=='physicalRetired' and project['workerIndices']
        assert all(workers[index]['exitStatusKnown'] for index in project['workerIndices'])
        assert len(lock['names'])==len(set(lock['names']))
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


class Barrier:
    def __init__(self,count,mode='release'):
        self.count=count;self.mode=mode;self.records=[];self.errors=[];self.clients=[]
        self.ready=threading.Event();self.cli_pid=None
        self.server=socket.socket();self.server.bind(('127.0.0.1',0));self.server.listen(count);self.server.settimeout(8)
        self.port=self.server.getsockname()[1]
        self.record_path=root/('barrier-'+str(self.port)+'.json')
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        try:
            for _ in range(self.count):
                conn,_=self.server.accept();self.clients.append(conn);conn.settimeout(8);data=b''
                while not data.endswith(b'\n'):
                    part=conn.recv(4096)
                    assert part and len(data)+len(part)<16384
                    data+=part
                record=json.loads(data);assert record['retry']==0 and record['pid']>0
                self.records.append(record)
            assert len({r['pid'] for r in self.records})==self.count
            assert self.ready.wait(8)
            self.record_path.write_text(json.dumps(self.records,indent=2)+'\n')
            if self.mode=='signal':
                assert self.count==1
                native=self.records[0]['nativePid']
                stat=Path('/proc/'+str(native)+'/stat').read_text()
                assert int(stat[stat.rindex(')')+2:].split()[1])==self.cli_pid,'Network callback did not name the actual native child of this CLI'
                os.kill(native,signal.SIGINT)
            for conn in self.clients:conn.sendall(b'go\n')
        except BaseException as error:self.errors.append(repr(error))
        finally:
            for conn in self.clients:conn.close()
            self.server.close()
    def finish(self):
        self.thread.join(10)
        assert not self.thread.is_alive() and not self.errors,(self.records,self.errors)



def locks_for(report,name):
    project=next(p for p in report['projects'] if p['name']==name)
    return next(g for g in report.get('resourceLocks',[]) if g['projectId']==project['id'])


def pid_for(report,name):
    project=next(p for p in report['projects'] if p['name']==name)
    return [w['pid'] for w in report['workers'] if w['workerIndex'] in project['workerIndices']]


try:
    r,e=execute('same-name-holds-whole-group-through-hooks-fixtures-file-writers-and-host-exit',r'''
import {openSync,closeSync,unlinkSync,existsSync} from 'node:fs';
const gate=previous+'-exclusive',state=previous+'-state';let fd;
const acquire=()=>{fd=openSync(gate,'wx')};const release=()=>{closeSync(fd);unlinkSync(gate)};
const f={w:{scope:'worker',setup:async({},use,info)=>{emit('fixture-start',info);await use({pid:process.pid});if(info.project.name==='p'){const s=JSON.parse(readFileSync(state,'utf8'));s.cleaned=true;writeFileSync(state,JSON.stringify(s))}emit('fixture-end',info)}}};
export default defineNativeSuite({fixtures:f,projects:[
{name:'p',hooks:{beforeAll:[{fixtures:['w'],run:async({},info)=>{acquire();writeFileSync(state,JSON.stringify({pid:process.pid,bodyB:false,cleaned:false}));emit('before',info)}}],afterAll:[{run:async({},info)=>{await info.attach('afterAll-file',{body:Buffer.from('actual afterAll file')});release();emit('after',info)}}]},tests:[test('a',async()=>{assert(existsSync(gate))},{lock:'shared',fixtures:['w']}),test('b',async(info)=>{await info.step('p file',()=>info.attach('body-file',{body:Buffer.from('actual body file')}));const s=JSON.parse(readFileSync(state,'utf8'));s.bodyB=true;writeFileSync(state,JSON.stringify(s))},{fixtures:['w']})]},
{name:'q',hooks:{beforeAll:[{run:async({},info)=>{const s=JSON.parse(readFileSync(state,'utf8'));assert(s.bodyB&&s.cleaned&&!exists(s.pid));acquire();await info.step('q-before',()=>{})}}],afterAll:[{run:async()=>release()}]},tests:[test('c',async()=>{},{lock:'shared'})]}
]});
''',options=('--workers','2'))
    assert [(x['event'],x['project']) for x in e if x['event'] in ['a','b','c']]==[('a','p'),('b','p'),('c','q')]
    assert len(r['workers'])==2 and len(r['attachments'])==2
    q_start=next(s['startedMonotonicMs'] for s in r['steps'] if s['title']=='q-before')
    assert all(a['writerExitReceiptMonotonicMs']<=q_start for a in r['attachments'])
    assert set(locks_for(r,'p')['names'])=={'shared'}==set(locks_for(r,'q')['names'])

    barrier=Barrier(2)
    source=r'''
import {createConnection} from 'node:net';
const arrive=async(info)=>{const c=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{c.once('error',reject);c.once('connect',()=>c.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,project:info.project.name,retry:info.retry})+'\n'));c.once('data',data=>{assert.equal(data.toString(),'go\n');resolve()})})}finally{c.destroy()}};
const records=RECORDS;
export default defineNativeSuite({projects:[
{name:'p',tests:[test('a',arrive,{lock:'A'})]},
{name:'q',hooks:{beforeAll:[{run:async()=>{for(const r of JSON.parse(readFileSync(records,'utf8')))assert(!exists(r.pid))}}]},tests:[test('b',async()=>{},{lock:['A','B']})]},
{name:'r',tests:[test('c',arrive,{lock:'B'})]}
]});
'''.replace('PORT',str(barrier.port)).replace('RECORDS',json.dumps(str(barrier.record_path)))
    r,e=execute('atomic-multiple-names-skip-blocked-waiter-for-disjoint-real-overlap',source,options=('--workers','2'),barrier=barrier)
    assert {x['project'] for x in barrier.records}=={'p','r'} and len(r['workers'])==3
    assert locks_for(r,'r')['projectGeneration']=='2' and locks_for(r,'q')['projectGeneration']=='3'
    assert set(locks_for(r,'q')['names'])=={'A','B'}

    r,e=execute('native-file-suite-test-inheritance-and-exact-utf16-report',r'''
export default defineNativeSuite({lock:'file',projects:[
{name:'p',entries:[{title:'scope',lock:['suite','\ud800'],entries:[test('a',async()=>{writeFileSync(previous,String(process.pid))},{lock:['test','\0','','test']})]}]},
{name:'q',tests:[test('b',async()=>{assert(!exists(Number(readFileSync(previous,'utf8'))))})]}
]});
''',options=('--workers','2'))
    assert set(locks_for(r,'p')['names'])=={'file','suite','\ud800','test','\0',''} and set(locks_for(r,'q')['names'])=={'file'}

    barrier=Barrier(2)
    source=r'''
import {createConnection} from 'node:net';
const arrive=async(info)=>{const c=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{c.once('error',reject);c.once('connect',()=>c.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,project:info.project.name,retry:info.retry})+'\n'));c.once('data',()=>resolve())})}finally{c.destroy()}};
export default defineNativeSuite({projects:[
{name:'p',tests:[test('a',arrive,{lock:'\ud800'})]},
{name:'q',tests:[test('b',async()=>{},{lock:'\ud800'})]},
{name:'r',tests:[test('c',arrive,{lock:'\ud801'})]}
]});
'''.replace('PORT',str(barrier.port))
    r,e=execute('distinct-isolated-utf16-units-overlap-equal-units-wait',source,options=('--workers','2'),barrier=barrier)
    assert {x['project'] for x in barrier.records}=={'p','r'} and locks_for(r,'q')['projectGeneration']=='3'
    assert locks_for(r,'p')['names']==['\ud800'] and locks_for(r,'q')['names']==['\ud800'] and locks_for(r,'r')['names']==['\ud801']

    r,e=execute('waiting-outside-original-test-timeout-actual-work-not-sleep',r'''
import {pbkdf2Sync} from 'node:crypto';
export default defineNativeSuite({projects:[
{name:'p',tests:[test('a',async(info)=>{const start=process.hrtime.bigint();const digest=pbkdf2Sync('actual bounded lock proof','salt',3000000,32,'sha256');assert.equal(digest.length,32);emit('actual-work-nanoseconds:'+String(process.hrtime.bigint()-start),info);writeFileSync(previous,String(process.pid))},{lock:'budget',timeout:10000})]},
{name:'q',tests:[test('b',async(info)=>{assert.equal(info.timeout,300);assert(!exists(Number(readFileSync(previous,'utf8'))))},{lock:'budget',timeout:300})]}
]});
''',options=('--workers','2','--global-timeout','15000'))
    assert len(r['attempts'])==2 and all(a['status']=='passed' for a in r['attempts'])
    assert r['attempts'][0]['durationMs']>300 and r['attempts'][1]['durationMs']<300
    assert int(next(x['event'].split(':')[1] for x in e if x['event'].startswith('actual-work-nanoseconds:')))>300000000
    barrier=Barrier(2)
    source=r'''
import {createConnection} from 'node:net';
const arrive=async(info)=>{const c=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{c.once('error',reject);c.once('connect',()=>c.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,project:info.project.name,retry:info.retry})+'\n'));c.once('data',()=>resolve())})}finally{c.destroy()}};
export default defineNativeSuite({projects:[
{name:'p',entries:[test('a',arrive,{lock:'A'}),{title:'unselected',lock:'shared',entries:[test('excluded',async()=>assert.fail('excluded declaration admitted'))]}]},
{name:'q',tests:[test('b',arrive,{lock:'shared'})]}
]});
'''.replace('PORT',str(barrier.port))
    r,e=execute('native-selected-id-union-excludes-unselected-suite-locks',source,options=('--workers','2','--test-id','1','--test-id','3'),barrier=barrier)
    assert {a['testId'] for a in r['attempts']}=={'1','3'} and set(locks_for(r,'p')['names'])=={'A'} and set(locks_for(r,'q')['names'])=={'shared'}

    r,e=execute('same-native-owner-holds-lock-through-whole-serial-group-replay',r'''
import {existsSync} from 'node:fs';
const marker=previous+'-q';
const fresh=async({},info)=>{assert(!existsSync(marker));let pids=[];if(info.retry){pids=JSON.parse(readFileSync(previous,'utf8'));assert(pids.every(pid=>!exists(pid)))}pids.push(process.pid);writeFileSync(previous,JSON.stringify(pids));emit('before',info)};
export default defineNativeSuite({projects:[
{name:'p',entries:[{title:'serial',mode:'serial',hooks:{beforeAll:[{run:fresh}],afterAll:[{run:async({},info)=>{await info.attach('wave file',{body:Buffer.from('real writer')});emit('after',info)}}]},entries:[test('a',async(info)=>{if(!info.retry)assert.fail('real serial first-wave failure')},{lock:'shared',retries:1}),test('b',async()=>{})]}]},
{name:'q',hooks:{beforeAll:[{run:async({},info)=>{const pids=JSON.parse(readFileSync(previous,'utf8'));assert.equal(pids.length,2);assert(pids.every(pid=>!exists(pid)));writeFileSync(marker,'q');await info.step('q-after-replay',()=>{})}}]},tests:[test('c',async()=>{},{lock:'shared'})]}
]});
''',options=('--workers','2','--max-failures','1'))
    assert len(r['workers'])==3 and r['summary']['flaky']==1 and r['summary']['unexpected']==0
    assert len(r['resourceLocks'])==2 and len(pid_for(r,'p'))==2 and locks_for(r,'p')['projectGeneration']=='1' and locks_for(r,'q')['projectGeneration']=='2'
    q_start=next(s['startedMonotonicMs'] for s in r['steps'] if s['title']=='q-after-replay')
    assert all(a['writerExitReceiptMonotonicMs']<=q_start for a in r['attachments'])

    r,e=execute('failed-dependency-has-no-owner-teardown-after-real-lock-retirement',r'''
export default defineNativeSuite({projects:[
{name:'setup',teardown:'cleanup',tests:[test('a',async()=>{writeFileSync(previous,String(process.pid));assert.fail('actual setup failure')},{lock:'shared'})]},
{name:'blocked',dependencies:['setup'],tests:[test('forbidden',async()=>assert.fail('failed dependent admitted'),{lock:'shared'})]},
{name:'other',tests:[test('b',async()=>{assert(!exists(Number(readFileSync(previous,'utf8'))));writeFileSync(previous+'-other',String(process.pid))},{lock:'shared'})]},
{name:'cleanup',default:false,tests:[test('c',async()=>{assert(!exists(Number(readFileSync(previous,'utf8'))));assert(!exists(Number(readFileSync(previous+'-other','utf8'))))},{lock:'shared'})]}
]});
''',1,('--workers','2'))
    assert [p['status'] for p in r['projects']]==['failed','blocked','passed','passed'] and r['projects'][1]['workerIndices']==[] and len(r['resourceLocks'])==3

    r,e=execute('final-failure-cap-never-admits-locked-waiter',r'''
export default defineNativeSuite({projects:[
{name:'p',tests:[test('a',async()=>assert.fail('actual final failure'),{lock:'shared'})]},
{name:'q',tests:[test('forbidden',async()=>assert.fail('failure-limit waiter admitted'),{lock:'shared'})]}
]});
''',1,('--workers','2','--max-failures','1'))
    assert len(r['workers'])==1 and len(r['attempts'])==1 and len(r['resourceLocks'])==1 and r['projects'][1]['status']=='notRun' and r['projects'][1]['workerIndices']==[]

    for mode,code,options in [('deadline',1,('--workers','2','--global-timeout','2500')),('signal',130,('--workers','2'))]:
        barrier=Barrier(1,mode)
        source=r'''
import {createConnection} from 'node:net';
const held=async(info)=>{const abort=new Promise(resolve=>info.signal.addEventListener('abort',()=>{emit('actual-abort',info);resolve()},{once:true}));const c=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{c.once('error',reject);c.once('connect',()=>{c.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,project:info.project.name,retry:info.retry})+'\n');resolve()})});await abort}finally{c.destroy()}};
export default defineNativeSuite({projects:[
{name:'p',tests:[test('a',held,{lock:'shared',retries:2})]},
{name:'q',tests:[test('forbidden',async()=>assert.fail('stopped lock waiter admitted'),{lock:'shared'})]}
]});
'''.replace('PORT',str(barrier.port))
        r,e=execute('original-global-stop-cancels-waiter-without-owner-'+mode,source,code,options,barrier)
        assert len(r['workers'])==1 and len(r['attempts'])==1 and r['attempts'][0]['status']=='interrupted' and sum(x['event']=='actual-abort' for x in e)==1
        assert len(r['resourceLocks'])==1 and r['projects'][1]['status']=='notRun' and r['projects'][1]['workerIndices']==[]

    r,e=execute('non-string-lock-declaration-rejected-before-author-callback',r'''
export default defineNativeSuite({projects:[{name:'p',tests:[test('forbidden',async()=>assert.fail('invalid lock callback admitted'),{lock:42})]}]});
''',1,('--workers','2'))
    assert r['summary']['attempts']==0 and r['globalErrors'] and 'lock' in str(r['globalErrors']) and not r.get('resourceLocks')

    after={name:digest(Path(name)) for name in before}
    (root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    assert before==after
    (root/'index.json').write_text(json.dumps(dict(passed=True,groups=groups,inputsUnchanged=True,scope='Authored actual CLI native default/serial project file-group resource locks, physical process/writer receipts and IO barriers; not unchanged upstream/fully-parallel/external-file scheduling parity'),indent=2)+'\n')
    print('Actual native resource locks: '+str(len(groups))+' groups passed; '+str(root))
except BaseException as error:
    (root/'first-failure.json').write_text(json.dumps(dict(passed=False,error=repr(error),completedGroups=groups,inputsUnchanged={name:digest(Path(name)) for name in before}==before),indent=2)+'\n')
    raise
