# SPDX-License-Identifier: MIT
"""Authored serial-group CLI controls with actual native/OS callback receipts.

Pinned originals are read-only behavioral oracles. No upstream driver or
assertion is modified and no JavaScript scheduler supplies group order/retries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
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
parser.add_argument('--process-only',action='store_true')
args=parser.parse_args()
args.directory.mkdir(parents=True,exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='run-',dir=args.directory.resolve()))
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[args.executable.resolve(),Path(__file__).resolve(),*[p for p in args.cli_script.resolve().parent.rglob('*') if p.is_file() and not p.is_symlink()]]
if args.source_root:
    inputs += [p for p in (args.source_root/'native/runner').rglob('*') if p.is_file()]
    inputs += [args.source_root/'src'/p for p in ['native-suite.ts','native-runner-host.ts']]
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
const test=(title,run,options={})=>({title,fixtures:['w'],...options,run:async({w},info)=>{assert.equal(w.pid,process.pid);emit(title,info);await run(info)}});
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


try:
    if not args.process_only:
        r,e=execute('failure-omits-rest-without-fabricated-attempts',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async()=>{}),test('b',async()=>assert.fail('actual serial failure')),test('c',async()=>assert.fail('omitted member admitted'))]},test('outside',async()=>{})]});
    """,1)
        assert bodies(e)==[('a',0),('b',0),('outside',0)]
        assert r['summary']['expected']==2 and r['summary']['unexpected']==1 and r['summary']['didNotRun']==1
        assert only(r)['status']=='failed' and len(only(r)['omissions'])==1 and len(r['workers'])==2

        r,e=execute('whole-group-replay-fresh-worker-hooks-fixtures-and-files',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async()=>{}, {retries:1}),{title:'inner',entries:[test('b',async(info)=>{if(!info.retry)assert.fail('retry entire group')}),test('c',async(info)=>{await info.step('outer',()=>info.step('inner',()=>info.attach('serial-file',{body:Buffer.from('actual-file')})))})]}]}]});
    """)
        assert bodies(e)==[('a',0),('b',0),('a',1),('b',1),('c',1)]
        assert r['summary']['attempts']==5 and r['summary']['flaky']==1 and r['summary']['expected']==3 and r['summary']['didNotRun']==0
        assert only(r)['status']=='passed' and len(r['workers'])==2 and len(r['steps'])==2 and len(r['attachments'])==1
        assert sum(x['event']=='before' for x in e)==2 and sum(x['event']=='after' for x in e)==2 and sum(x['event']=='fixture-start' for x in e)==2 and sum(x['event']=='fixture-end' for x in e)==2

        r,e=execute('expected-static-and-runtime-failures-continue-same-group-worker',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async()=>assert.fail('expected static'),{expectedFailure:true}),test('b',async(info)=>{await info.fail();assert.fail('expected runtime')}),test('c',async()=>{})]}]});
    """)
        assert bodies(e)==[('a',0),('b',0),('c',0)] and len(r['workers'])==1 and only(r)['status']=='passed' and only(r)['omissions']==[]

        r,e=execute('unexpected-pass-replays-whole-group',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async(info)=>{if(info.retry)assert.fail('expected retry failure')},{expectedFailure:true,retries:1}),test('b',async()=>{})]}]});
    """)
        assert bodies(e)==[('a',0),('a',1),('b',1)] and r['summary']['flaky']==1 and only(r)['status']=='passed'

        r,e=execute('before-all-infrastructure-failure-cannot-be-expected',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:async(f,info)=>{await before(f,info);if(!info.retry)assert.fail('actual beforeAll failure')}}],afterAll:[{run:after}]},entries:[test('a',async()=>assert.fail('expected body failure'),{expectedFailure:true,retries:1}),test('b',async()=>{})]}]});
    """)
        assert bodies(e)==[('a',1),('b',1)] and r['summary']['attempts']==3 and r['summary']['flaky']==1 and only(r)['status']=='passed'
        assert not r['attempts'][0]['expected'] and r['attempts'][0]['retry']==0 and sum(x['event']=='after' for x in e)==2

        r,e=execute('after-all-failure-replays-passed-prefix',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}]},entries:[{title:'inner',hooks:{afterAll:[{run:async({},info)=>{emit('after',info);if(!info.retry)assert.fail('actual afterAll failure')}}]},entries:[test('a',async()=>{},{retries:1})]},test('b',async()=>{})]}]});
    """)
        assert bodies(e)==[('a',0),('a',1),('b',1)] and r['summary']['flaky']==1 and only(r)['status']=='passed'

        r,e=execute('maximum-member-retry-controls-entire-selected-group',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async(info)=>assert.equal(info.retry,2)),test('b',async()=>{},{retries:2})]}]});
    """)
        assert bodies(e)==[('a',0),('a',1),('a',2),('b',2)] and only(r)['retries']==2 and len(r['workers'])==3 and only(r)['status']=='passed'

        r,e=execute('mixed-history-final-failure-keeps-original-actual-attempts',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:before}],afterAll:[{run:after}]},entries:[test('a',async(info)=>assert.equal(info.retry,0),{retries:1}),test('b',async()=>assert.fail('always fails')),test('c',async()=>assert.fail('omitted c'))]}]});
    """,1)
        assert bodies(e)==[('a',0),('b',0),('a',1)] and r['summary']['flaky']==1 and r['summary']['unexpected']==1 and r['summary']['didNotRun']==1 and only(r)['status']=='failed'

        r,e=execute('one-final-failure-limit-stops-later-declarations-after-retry',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',entries:[test('a',async()=>assert.fail('exhausted group'),{retries:1}),test('b',async()=>assert.fail('omitted group b'))]},test('outside',async()=>assert.fail('failure-limit admission'))]});
    """,1,('--max-failures','1'))
        assert bodies(e)==[('a',0),('a',1)] and len(r['workers'])==2 and r['summary']['didNotRun']==2 and only(r)['status']=='failed'

        r,e=execute('original-global-deadline-interrupts-group-without-replay',"""
    export default defineNativeSuite({fixtures,entries:[{title:'group',mode:'serial',entries:[test('a',async(info)=>{await new Promise(resolve=>info.signal.addEventListener('abort',()=>{emit('actual-abort',info);resolve()},{once:true}))},{retries:2}),test('b',async()=>assert.fail('deadline admitted member'))]},test('outside',async()=>assert.fail('deadline admitted outside'))]});
    """,1,('--global-timeout','2500'))
        assert bodies(e)==[('a',0)] and sum(x['event']=='actual-abort' for x in e)==1 and len(r['workers'])==1 and r['attempts'][0]['status']=='interrupted' and only(r)['status']=='interrupted' and r['summary']['didNotRun']==2

    barrier=Barrier(2)
    source="""
import {createConnection} from 'node:net';
const arrive=async(info)=>{const conn=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{conn.once('error',reject);conn.once('connect',()=>conn.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,project:info.project.name,retry:info.retry})+'\\n'));conn.once('data',data=>{assert.equal(data.toString(),'go\\n');resolve()})})}finally{conn.destroy()}};
const oldBatch=RECORDS;
const fresh=async(f,info)=>{if(info.retry){for(const old of JSON.parse(readFileSync(oldBatch,'utf8')))assert.throws(()=>process.kill(old.pid,0),e=>e.code==='ESRCH')}await before(f,info)};
export default defineNativeSuite({fixtures,projects:[
{name:'p',entries:[{title:'serial p',mode:'serial',hooks:{beforeAll:[{fixtures:['w'],run:fresh}],afterAll:[{run:after}]},entries:[test('a',async(info)=>{if(!info.retry){await arrive(info);assert.fail('p first wave')}} ,{retries:1}),test('c',async(info)=>{await info.step('real file',()=>info.attach('parallel-group-file',{body:Buffer.from('actual')}))})]}]},
{name:'q',entries:[{title:'serial q',mode:'serial',entries:[test('b',async(info)=>arrive(info))]}]}
]});
""".replace('PORT',str(barrier.port)).replace('RECORDS',json.dumps(str(barrier.record_path)))
    r,e=execute('parallel-project-groups-share-physical-batch-and-global-identities',source,0,('--workers','2','--max-failures','1'),barrier)
    assert len(r['serialGroups'])==2 and all(g['status']=='passed' for g in r['serialGroups'])
    assert len(r['workers'])==3 and r['summary']['attempts']==4 and r['summary']['flaky']==1 and r['summary']['unexpected']==0 and len(r['attachments'])==1
    assert {record['project'] for record in barrier.records}=={'p','q'}
    assert all(project['status']=='passed' for project in r['projects'])

    barrier=Barrier(1,'signal')
    source="""
import {createConnection} from 'node:net';
const held=async(info)=>{const abort=new Promise(resolve=>info.signal.addEventListener('abort',()=>{emit('actual-abort',info);resolve()},{once:true}));const conn=createConnection({port:PORT,host:'127.0.0.1'});try{await new Promise((resolve,reject)=>{conn.once('error',reject);conn.once('connect',()=>{conn.write(JSON.stringify({pid:process.pid,nativePid:process.ppid,retry:info.retry})+'\\n');resolve()})});await abort}finally{conn.destroy()}};
export default defineNativeSuite({fixtures,entries:[{title:'serial',mode:'serial',entries:[test('a',held,{retries:2}),test('b',async()=>assert.fail('SIGINT admitted member'))]},test('outside',async()=>assert.fail('SIGINT admitted outside'))]});
""".replace('PORT',str(barrier.port))
    r,e=execute('native-only-sigint-retains-real-abort-and-exit-before-no-replay',source,130,(),barrier)
    assert bodies(e)==[('a',0)] and sum(x['event']=='actual-abort' for x in e)==1 and len(r['workers'])==1 and r['attempts'][0]['status']=='interrupted' and only(r)['status']=='interrupted' and r['summary']['didNotRun']==2

    after={name:digest(Path(name)) for name in before}
    (root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    assert before==after
    (root/'index.json').write_text(json.dumps(dict(passed=True,groups=groups,inputsUnchanged=True,scope='Authored native actual CLI serial-group controls, real callback/OS worker retirement and JSON/JUnit/HTML; not unchanged upstream/full serial scheduling parity'),indent=2)+'\n')
    print('Actual native serial groups: '+str(len(groups))+' groups passed; '+str(root))
except BaseException as error:
    (root/'first-failure.json').write_text(json.dumps(dict(passed=False,error=repr(error),completedGroups=groups,inputsUnchanged={name:digest(Path(name)) for name in before}==before),indent=2)+'\n')
    raise
