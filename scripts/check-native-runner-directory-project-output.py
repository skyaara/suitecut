# SPDX-License-Identifier: MIT
"""Actual combined directory cohorts/project output roots. No browser or overlay."""
import argparse, hashlib, json, os, subprocess, tempfile, time
from pathlib import Path

p=argparse.ArgumentParser()
for name in ('executable','sdk-directory','supervisor','directory','node'):
 p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='directory-project-',dir=a.directory));groups=[]
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
pins={str(x):sha(x) for x in [Path(__file__).resolve(),a.executable.resolve(),a.supervisor.resolve(),a.node.resolve(),*sorted(a.sdk_directory.glob('*.js'))]}
api=(a.sdk_directory/'native.js').as_uri()
def index():
 changed=[path for path,digest in pins.items() if sha(path)!=digest]
 (root/'index.json').write_text(json.dumps({'scope':'Four actual combined native directory/project-root cases; prior parallel7 and project27 remain separately qualified. No browser.','inputs':pins,'changed':changed,'groups':groups},indent=2)+'\n')
 assert not changed,changed

def sources(doc):return [s['report'] for s in doc['sources']]
def checked_receipts(doc):
 d=doc['directory'];budget=d['nativeBudgetReceipt']
 assert all(budget[k]==0 for k in ('physicalWorkers','physicalIO','physicalSteps','physicalSoft','physicalSources','publicationPending'))
 pids=set()
 def visit(value):
  if isinstance(value,dict):
   for key,item in value.items():
    if key in ('pid','writerPid','helperPid','coordinatorPID') and isinstance(item,int) and item>0:pids.add(item)
    visit(item)
  elif isinstance(value,list):
   for item in value:visit(item)
 visit(doc)
 remaining=[pid for pid in pids if Path('/proc',str(pid)).exists()];assert not remaining,remaining
 intervals=[]
 for segment in d['segments']:
  if not segment['sourceId']:continue
  source=next(v['report']for v in doc['sources']if v['sourceId']==segment['sourceId'])
  hosts=segment['timing']['hosts'];assert {(h['workerIndex'],h['pid'])for h in hosts}=={(w['workerIndex'],w['pid'])for w in source['workers']}
  for h in hosts:intervals.extend([(h['admittedMonotonicMs'],1),(h['waitpidCapturedMonotonicMs'],-1)])
 active=peak=0
 for _,delta in sorted(intervals,key=lambda x:(x[0],x[1])):active+=delta;peak=max(peak,active)
 assert active==0 and peak<=2,(active,peak)
 all_outputs=[o for s in sources(doc)for o in s.get('outputs',[])];assert len({o['id']for o in all_outputs})==len(all_outputs)
 for s in sources(doc):
  for o in s.get('outputs',[]):
   attempt=next(t for t in s['attempts'] if all(t[k]==o[k] for k in ('testId','generation','workerIndex','pid')))
   if o['status']=='ready':
    assert o['readyReceipt'] and not o['writerSignaled'] and o['writerExitCode']==0
    assert o['writerExitReceiptMonotonicMs']<=o['physicalReceiptMonotonicMs']<=o['assignmentReceiptMonotonicMs']<o['deadlineMonotonicMs']
   assert attempt['pid']==o['pid']
 return sorted(pids),peak

def run(name,make,code=0,options=(),observe=False,setup=None):
 case=root/name;tests=case/'tests';tests.mkdir(parents=True);beacon=case/'events.jsonl'
 common='import{defineNativeSuite}from '+json.dumps(api)+';import assert from "node:assert/strict";import{writeFileSync,readFileSync,appendFileSync,existsSync,readdirSync}from "node:fs";const control='+json.dumps(str(case))+';const mark=(phase,info)=>appendFileSync('+json.dumps(str(beacon))+',JSON.stringify({phase,pid:process.pid,outputDir:info.outputDir,project:info.project.name})+"\\n");const delay=ms=>new Promise(r=>setTimeout(r,ms));const waitFor=async f=>{while(!f())await delay(5)};'
 for file,source in make(case).items():
  target=tests/file;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(common+source+'\n')
 if setup:setup(case)
 command=[str(a.supervisor),'--report',str(case/'supervisor.json'),'--timeout-ms','45000','--cleanup-grace-ms','1000','--physical-ms','10000','--',str(a.executable),'--node',str(a.node),'--host-script',str(a.sdk_directory/'native-runner-host.js'),'--test-dir',str(tests),'--report',str(case/'actual.json'),'--workers','2',*options]
 observation=None
 with (case/'stdout').open('wb')as out,(case/'stderr').open('wb')as err:
  child=subprocess.Popen(command,stdout=out,stderr=err)
  try:
   if observe:
    end=time.monotonic()+15
    while child.poll()is None and time.monotonic()<end:
     try:events=[json.loads(x)for x in beacon.read_text().splitlines()]
     except(FileNotFoundError,json.JSONDecodeError):events=[]
     complete=next((e for e in events if e['phase']=='complete'),None);held=next((e for e in events if e['phase']=='held'),None)
     if complete and held and not Path('/proc',str(complete['pid'])).exists():
      observation={'retiredCompletedHost':complete['pid'],'liveHeldHost':held['pid'],'heldHostPresent':Path('/proc',str(held['pid'])).exists(),'completedOutputStillPresent':Path(complete['outputDir'],'actual.txt').is_file(),'completedOutputDir':complete['outputDir']};break
     time.sleep(.01)
    (case/'release').write_text('actual external fixture release')
  finally:
   result=child.wait() # Supervisor genuinely drains/reaps before ANY checker assertion.
 physical=json.loads((case/'supervisor.json').read_text());row={'name':name,'command':command,'exit':result,'supervisor':physical,'observation':observation,'report':str(case/'actual.json')};groups.append(row);index()
 assert result==code and physical['childExit']=={'signaled':False,'code':code,'rawWaitpidStatus':code<<8},row
 assert not physical['cleanupFailed'] and not physical['timeout'] and physical['descendantsRetired'] and (case/'stderr').read_bytes()==b'',row
 assert not Path('/proc',str(physical['childPID'])).exists() and not Path('/proc',str(physical['supervisorPID'])).exists()
 doc=json.loads((case/'actual.json').read_text());row['actualPIDs'],row['peakNativeHostReservations']=checked_receipts(doc);row['remainingPIDs']=[];index()
 return case,doc,row

def project(name,body,path=None,retries=0):
 return '{name:'+json.dumps(name)+((',outputDir:'+json.dumps(path))if path is not None else '')+',tests:[{title:"same actual title",retries:'+str(retries)+',run:async(_,info)=>{'+body+'}}]}'
def suite(projects):return 'export default defineNativeSuite({projects:['+','.join(projects)+']});'
def artifacts(doc):return [a for s in sources(doc)for a in s.get('attachments',[])]
def outputs(doc):return [o for s in sources(doc)for o in s.get('outputs',[])]

try:
 def identity_files(case):
  files={}
  for file,reverse in [('a.spec.mjs',False),('nested/b.spec.mjs',True)]:
   directory=case/'tests'/Path(file).parent;expected=directory/'left-out'
   snapshots='assert.equal(info.snapshotDir,'+json.dumps(str(case/'tests'/file)+'-snapshots')+');assert.equal(info.snapshotPath("actual.png").startsWith(info.snapshotDir+"/")&&info.snapshotDir!==info.outputDir,true);'
   body=snapshots+'assert.equal(info.project.outputDir,'+json.dumps(str(expected))+');writeFileSync(info.outputPath("actual.txt"),"actual "+info.project.name);writeFileSync(control+"/entered-"+process.pid,"");await waitFor(()=>readdirSync(control).filter(x=>x.startsWith("entered-")).length===2);mark("rendezvous",info);'
   fallback=snapshots+'assert.equal(Object.hasOwn(info.project,"outputDir"),false);assert.equal(info.outputDir.startsWith('+json.dumps(str(case/'fallback'))+'+"/"),true);writeFileSync(info.outputPath("actual.txt"),"actual fallback");'
   ps=[project('left',body,'left-out'),project('fallback',fallback)]
   files[file]=suite(list(reversed(ps))if reverse else ps)
  return files
 case,d,row=run('reversed-local-project-roots-and-fallback',identity_files,options=['--output-directory',str(root/'reversed-local-project-roots-and-fallback'/'fallback')]);assert d['summary']['expected']==4 and len(d['sources'])==4
 assert {Path(o['outputDir']).parent for o in outputs(d)}=={case/'tests/left-out',case/'tests/nested/left-out',case/'fallback'}
 assert d['directory']['activeFileLimit']==2 and row['peakNativeHostReservations']==2 and not d['directory']['runFailed']
 def held_files(case):
  shared=str(case/'shared')
  common='assert.equal(info.project.outputDir,'+json.dumps(shared)+');writeFileSync(info.outputPath("actual.txt"),"immutable "+process.pid);await info.attach("actual path copy",{path:info.outputPath("actual.txt")});'
  return {'a.spec.mjs':suite([project('same',common+'mark("complete",info);',shared)]),'b.spec.mjs':suite([project('same',common+'mark("held",info);await waitFor(()=>existsSync(control+"/release"));',shared)])}
 def foreign(case):(case/'shared').mkdir();(case/'shared/foreign').write_bytes(b'actual foreign bytes')
 case,d,row=run('shared-root-held-peer-retention-and-artifact',held_files,options=['--preserve-output','never'],observe=True,setup=foreign)
 assert row['observation'] and row['observation']['heldHostPresent'] and row['observation']['completedOutputStillPresent']
 assert d['summary']['expected']==2 and len({o['outputDir']for o in outputs(d)})==2 and (case/'shared/foreign').read_bytes()==b'actual foreign bytes'
 for s in sources(d):
  assert len(s['outputRetention'])==1 and s['outputRetention'][0]['status']=='removed' and s['outputRetention'][0]['availability']=='originalLocationAbsent'
  assert not Path(s['outputs'][0]['outputDir']).exists()
 for a_ in artifacts(d):
  path=Path(a_['path']);st=path.stat();assert str(st.st_ino)==a_['inode'] and str(st.st_dev)==a_['device'] and path.read_bytes().startswith(b'immutable ') and a_['writerExitConfirmed'] and a_['writerExitCode']==0
 case,d,row=run('actual-bad-root-and-healthy-peer',lambda case:{'a.spec.mjs':suite([project('same','writeFileSync(control+"/FORBIDDEN","");','blocked')]),'b.spec.mjs':suite([project('same','writeFileSync(info.outputPath("actual.txt"),"healthy peer");mark("healthy",info);','healthy')])},code=1,options=['--output-directory',str(root/'actual-bad-root-and-healthy-peer'/'fallback')],setup=lambda case:(case/'tests/blocked').write_bytes(b'actual regular file'))
 assert not(case/'FORBIDDEN').exists() and (case/'tests/blocked').read_bytes()==b'actual regular file' and not(case/'fallback').exists()
 assert sorted(s['report']['summary']['expected']for s in d['sources'])==[0,1] and {o['status']for o in outputs(d)}=={'failed','ready'}
 assert any(Path(o['outputDir'],'actual.txt').read_bytes()==b'healthy peer'for o in outputs(d)if o['status']=='ready')
 def drift_files(case):
  body='writeFileSync(info.outputPath("actual.txt"),"actual first failure");writeFileSync(control+"/changed","");throw Error("actual original root failure");'
  changing='export default defineNativeSuite({projects:[{name:"same",outputDir:existsSync(control+"/changed")?"second":"first",tests:[{title:"same actual title",retries:1,run:async(_,info)=>{'+body+'}}]}]});'
  return {'a.spec.mjs':changing,'b.spec.mjs':suite([project('same','writeFileSync(info.outputPath("actual.txt"),"completed peer");','peer')])}
 case,d,row=run('retry-root-drift-keeps-original-catalog',drift_files,code=1,options=['--preserve-output','failures-only'])
 assert not(case/'tests/second').exists() and any(error=='Native directory collected file/catalog identity changed'for s in sources(d)for error in s['globalErrors'])
 assert any('actual original root failure'in t.get('error','')for s in sources(d)for t in s['attempts'])
 assert any(r['status']=='kept' and Path(r['originalPath'],'actual.txt').read_bytes()==b'actual first failure'for s in sources(d)for r in s.get('outputRetention',[]))
 assert any(s['summary']['expected']==1 for s in sources(d));index();print(json.dumps({'passed':True,'groups':len(groups),'index':str(root/'index.json')}))
except BaseException:
 index();print('Preserved actual first negative:',root/'index.json',flush=True);raise
