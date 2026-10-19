# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse,hashlib,json,os,signal,subprocess,tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--executable',type=Path,required=True)
parser.add_argument('--fixture',type=Path,required=True)
parser.add_argument('--directory',type=Path,required=True)
args=parser.parse_args()
binary=args.executable.resolve();fixture=args.fixture.resolve()
args.directory.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='supervisor-',dir=args.directory.resolve()))
inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [binary,fixture,Path(__file__).resolve()]}
rows=[]
def run(mode,expected,extra=None):
    report=out/(mode+'.json')
    command=[str(binary),'--report',str(report),'--timeout-ms','1000' if mode!='timeout' else '20','--cleanup-grace-ms','100','--physical-ms','2000','--',str(fixture),mode,*(extra or [])]
    env=os.environ.copy();env['PROOF_ENV']='unchanged-value'
    if mode=='interrupt':
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
        assert process.stdout.readline()==b'ready\n'
        process.send_signal(signal.SIGTERM)
        stdout,stderr=process.communicate(timeout=5)
        actual=process.returncode
    else:
        result=subprocess.run(command,input=b'realinput',capture_output=True,env=env,timeout=5)
        actual=result.returncode;stdout=result.stdout;stderr=result.stderr
    assert actual==expected,(mode,actual,stdout,stderr)
    value=json.loads(report.read_text());assert value['descendantsRetired'] and not value['error'],value
    assert value['childPID']>0 and value['childStart']>0
    for child in value['descendants']:
        assert child['parentAtObservation']==value['supervisorPID'] and child['start']>0
        stat=Path(f"/proc/{child['pid']}/stat")
        if stat.exists():assert int(stat.read_text().rsplit(')',1)[1].split()[19])!=child['start'],child
    assert not Path(f"/proc/{value['childPID']}").exists(),value
    if mode=='stdio':assert stdout==b'actual-stdout\n' and stderr==b'actual-stderr\n' and not value['descendants']
    else:assert not stderr,stderr
    if mode in ['fork','abnormal','held']:assert len(value['descendants'])==1
    if mode=='doublefork':assert len(value['descendants'])==2
    if mode in ['held','abnormal']:assert value['cleanupFailed'] is True
    elif mode not in ['timeout','interrupt','signal']:assert value['cleanupFailed'] is False
    if mode=='held':assert value['childExit']['code']==0 and value['descendants'][0]['killRequested'] and value['descendants'][0]['signaled'] and value['descendants'][0]['code']==signal.SIGKILL
    if mode=='abnormal':assert value['childExit']['code']==0 and value['descendants'][0]['code']==7
    if mode in ['signal','timeout','interrupt']:assert value['childExit']['signaled']
    if mode=='timeout':assert value['timeout'] and value['childExit']['code']==signal.SIGKILL
    if mode=='interrupt':assert value['interruption']==signal.SIGTERM
    rows.append({'mode':mode,'supervisorExit':actual,'report':str(report),'reportSHA256':hashlib.sha256(report.read_bytes()).hexdigest()})
for mode,code,extra in [('stdio',0,['literal argument','--child-flag']),('exit7',7,None),('signal',-signal.SIGUSR1,None),('fork',0,None),('doublefork',0,None),('held',125,None),('abnormal',125,None),('timeout',-signal.SIGKILL,None),('interrupt',-signal.SIGTERM,None)]:run(mode,code,extra)
changed=[p for p,h in inputs.items() if hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h]
(out/'index.json').write_text(json.dumps({'scope':'genuine standalone fork/exec/exit/signal/held-descendant native supervisor controls; no browser launch','passed':len(rows),'inputs':inputs,'changed':changed,'groups':rows},indent=2)+'\n')
assert not changed
print('PASS',len(rows),'native supervisor groups; actual PID/start reaping, inherited IO/env and waitpid outcomes')
