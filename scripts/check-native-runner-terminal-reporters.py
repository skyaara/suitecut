# SPDX-License-Identifier: MIT
"""Actual final native JSON/quiet stdout and owned publication failures."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

p=argparse.ArgumentParser()
p.add_argument('--executable',required=True,type=Path)
p.add_argument('--sdk-directory',required=True,type=Path)
p.add_argument('--supervisor',required=True,type=Path)
p.add_argument('--directory',required=True,type=Path)
p.add_argument('--node',required=True)
a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='terminal-reporters-',dir=a.directory.resolve()))
sdk=a.sdk_directory.resolve();exe=a.executable.resolve();supervisor=a.supervisor.resolve();groups=[]
common='import{defineNativeSuite}from '+json.dumps((sdk/'native-suite.js').as_uri())+";import assert from'node:assert/strict';import fs from'node:fs';"
simple="export default defineNativeSuite({tests:[{title:'real callback',run:()=>{fs.writeFileSync(MARKER,JSON.stringify({host:process.pid,native:process.ppid}));process.stdout.write('actual stdout\\n');process.stderr.write('actual stderr\\n')}}]});"

def source(folder,text):
    return common+text.replace('MARKER',json.dumps(str(folder/'actual-callback.json')))

def proc(pid):
    try:
        fields=Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()
        return {'pid':pid,'parent':int(fields[1]),'start':int(fields[19]),'state':fields[0]}
    except (FileNotFoundError,ProcessLookupError):return None

def records(document):
    return [row['report']for row in document['sources']]if document.get('kind')=='suitecut-native-merged-report'else[document]

def retired(document):
    for report in records(document):
        for worker in report.get('workers',report.get('collectionWorkers',[])):
            assert worker['exitStatusKnown']and not proc(worker['pid']),worker
        for output in report.get('outputs',[]):
            assert not proc(output['writerPid']),output
        for attachment in report.get('attachments',[]):
            assert attachment['writerExitConfirmed']and not proc(attachment['writerPid']),attachment
        for retention in report.get('outputRetention',[]):
            if retention.get('helperPid'):assert not proc(retention['helperPid']),retention

def execute(name,text=simple,options=(),expected=0,frontend='native',mode='json',files=None,output_fault=None):
    folder=out/name;folder.mkdir();report=folder/'actual.json';suite=folder/'suite.mjs';authored=[]
    if files:
        directory=folder/'tests';directory.mkdir()
        for filename,text in files.items():
            target=directory/filename;target.write_text(source(folder,text));authored.append(target)
        selection=['--test-dir',str(directory)]
    else:suite.write_text(source(folder,text));authored.append(suite);selection=['--suite',str(suite)]
    for index,path in enumerate(authored):
        checked=subprocess.run([a.node,'--check',str(path)],capture_output=True,timeout=10)
        (folder/('syntax-'+str(index)+'.stderr')).write_bytes(checked.stderr)
        assert checked.returncode==0,(path,checked.stderr)
    if frontend=='native':child=[str(exe),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js')]
    elif frontend=='cli':child=[a.node,str(sdk/'native-runner-cli.js'),'--executable',str(exe)]
    else:
        launcher=folder/'launch.mjs';launcher.write_text('import{runNativeSuite}from '+json.dumps((sdk/'native-runner.js').as_uri())+';const actual=await runNativeSuite('+json.dumps({'executablePath':str(exe),'suitePath':str(suite),'reportPath':str(report),'reporter':mode})+');if(actual.signal!==null)throw Error(actual.signal);process.exitCode=actual.exitCode;')
        child=[a.node,str(launcher)]
    if frontend!='api':
        child+=selection+['--report',str(report),*options]
        if mode is not None:child+=['--reporter',mode]
    command=[str(supervisor),'--report',str(folder/'supervisor.json'),'--timeout-ms','30000','--cleanup-grace-ms','2000','--physical-ms','10000','--',*child]
    (folder/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    signal_receipt=None
    if output_fault is None:
        actual=subprocess.run(command,capture_output=True,timeout=45);stdout,stderr,code=actual.stdout,actual.stderr,actual.returncode
    elif output_fault=='full':
        with open('/dev/full','wb',buffering=0)as destination:
            actual=subprocess.run(command,stdout=destination,stderr=subprocess.PIPE,timeout=45)
        stdout,stderr,code=b'',actual.stderr,actual.returncode
    else:
        reader,writer=os.pipe2(os.O_CLOEXEC|os.O_NONBLOCK);filled=0
        try:
            while True:filled+=os.write(writer,b'P'*4096)
        except BlockingIOError:pass
        original_flags=fcntl.fcntl(writer,fcntl.F_GETFL)&~os.O_NONBLOCK;fcntl.fcntl(writer,fcntl.F_SETFL,original_flags)
        try:
            with open(folder/'stderr','wb')as errors:
                process=subprocess.Popen(command,stdout=writer,stderr=errors)
                try:
                    if output_fault=='signal':
                        until=time.monotonic()+15
                        while not report.exists()and process.poll()is None and time.monotonic()<until:time.sleep(.01)
                        assert report.exists(),'actual committed report never appeared before signal gate'
                        marker=json.loads((folder/'actual-callback.json').read_text());identity=proc(marker['native'])
                        assert identity and identity['parent']==process.pid and Path(os.readlink(Path('/proc',str(identity['pid']),'exe')))==exe
                        os.kill(identity['pid'],signal.SIGINT);signal_receipt={**identity,'signal':signal.SIGINT,'sentMonotonicNs':time.monotonic_ns()}
                finally:
                    # Even an authored observer failure must wait for the real
                    # native supervisor's original bounded cleanup receipt.
                    code=process.wait(timeout=45)
            stderr=(folder/'stderr').read_bytes();assert fcntl.fcntl(writer,fcntl.F_GETFL)==original_flags
            os.close(writer);writer=-1;chunks=[]
            while True:
                data=os.read(reader,16384)
                if not data:break
                chunks.append(data)
            stdout=b''.join(chunks);assert stdout==b'P'*filled
            (folder/'backpressure.json').write_text(json.dumps({'preloadedBytes':filled,'parentDescriptorFlagsUnchanged':True,'actualSignal':signal_receipt},indent=2)+'\n')
        finally:
            os.close(reader)
            if writer>=0:os.close(writer)
    (folder/'stdout').write_bytes(stdout);(folder/'stderr').write_bytes(stderr)
    physical=json.loads((folder/'supervisor.json').read_text())
    assert code==expected,(name,code,stderr)
    assert physical['childExit']['code']==expected and not physical['childExit']['signaled']and physical['descendantsRetired']and not physical['cleanupFailed']and not physical['timeout']and not proc(physical['childPID']),physical
    document=json.loads(report.read_bytes());retired(document)
    if output_fault:
        assert b'Native JSON reporter failed after actual execution exit 'in stderr,stderr
        assert document['summary']['unexpected']==0 and document['summary']['attempts']==1 and (folder/'actual-callback.json').exists(),document
    else:
        assert stderr==b'',stderr
        if mode=='json':assert stdout==report.read_bytes(),(name,stdout[:100])
        elif mode=='none':assert stdout==b'',stdout
        else:assert stdout.startswith(b'Native runner: ')and b'actual attempts'in stdout,stdout
    groups.append({'name':name,'actualExitCode':code,'report':str(report),'supervisor':str(folder/'supervisor.json'),'actualSignal':signal_receipt})
    return folder,document

paths=[Path(__file__).resolve(),exe,supervisor,Path(a.node).resolve(),*[file for file in sdk.rglob('*')if file.is_file()]]
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
before={str(path):digest(path)for path in sorted(set(paths))};(out/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
execute('default-console-is-preserved',mode=None)
execute('quiet-native',mode='none')
execute('quiet-thin-api',frontend='api',mode='none')
folder,data=execute('one-native-json-with-full-result-facts',text=r'''export default defineNativeSuite({tests:[{title:'actual retry \ud800',retries:1,run:async(_,i)=>{process.stdout.write('actual stdout\n');process.stderr.write('actual stderr\n');await i.step('real nested step',async()=>{await i.attach('real bytes',{body:Buffer.from([0,255,65])})});if(!i.retry)throw Error('actual first failure')}},{title:'actual declared skip',skip:true,run:()=>{throw Error('forbidden')}}]});''',options=('--junit',str(out/'one-native-json-with-full-result-facts/actual.xml'),'--html',str(out/'one-native-json-with-full-result-facts/actual.html')))
assert data['summary']['flaky']==1 and data['summary']['skipped']==1 and len(data['attempts'])==2 and data['attempts'][0]['pid']!=data['attempts'][1]['pid']
assert '\ud800'in data['attempts'][0]['title']and len(data['steps'])==2 and len(data['attachments'])==2
assert all(bytes.fromhex(worker['stdout']['data'])==b'actual stdout\n'and bytes.fromhex(worker['stderr']['data'])==b'actual stderr\n'for worker in data['workers'])
for artifact in data['attachments']:assert Path(artifact['path']).read_bytes()==bytes([0,255,65])
assert(folder/'actual.xml').is_file()and(folder/'actual.html').is_file()
execute('json-thin-cli',frontend='cli')
execute('json-thin-api',frontend='api')
folder,data=execute('json-listing-without-callbacks',options=('--list',))
assert data['kind']=='suitecut-native-test-list'and not(folder/'actual-callback.json').exists()and 'attempts'not in data
folder,data=execute('one-directory-document-for-real-concurrent-sources',files={'a.spec.mjs':simple,'b.spec.mjs':simple},options=('--workers','2'))
assert data['kind']=='suitecut-native-merged-report'and len(data['sources'])==2 and data['summary']['attempts']==2 and not data['directory']['runFailed']
execute('global-errors-stay-inside-json',text='export default defineNativeSuite({tests:[]});',expected=1)
execute('dev-full-after-successful-physical-retirement',expected=2,output_fault='full')
execute('blocked-stdout-original-suite-deadline',options=('--global-timeout','4000'),expected=2,output_fault='deadline')
execute('blocked-stdout-real-native-sigint',options=('--global-timeout','30000'),expected=2,output_fault='signal')
after={path:digest(Path(path))for path in before};(out/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n');assert before==after
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native JSON/quiet terminal reporter publication after genuine process/artifact retirement; exact output file bytes and true failure statuses. No custom lifecycle callbacks or Playwright schema parity.','inputCount':len(before),'changed':[],'groups':groups},indent=2)+'\n')
print('PASS',len(groups),'actual native terminal reporter groups')
