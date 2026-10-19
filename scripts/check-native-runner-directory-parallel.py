# SPDX-License-Identifier: MIT
"""Real native coroutine/file concurrency; no browser or authored scheduler."""
import argparse, hashlib, json, os, signal, subprocess, tempfile, time
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('executable','sdk-directory','supervisor','directory','node'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.directory.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='parallel-',dir=a.directory));groups=[]
pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in [Path(__file__).resolve(),a.executable.resolve(),a.supervisor.resolve(),a.node.resolve(),*sorted(a.sdk_directory.glob('*.js'))]}
api=(a.sdk_directory/'native-suite.js').as_uri()
def index():
 changed=[path for path,digest in pins.items() if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest]
 (root/'index.json').write_text(json.dumps({'inputs':pins,'changed':changed,'groups':groups,'scope':'Actual native concurrent ready cohorts; full discovery/selection and run-global ownership. Lock-bearing catalogs conservatively serialize.'},indent=2)+'\n');assert not changed,changed

def run(name,files,workers=2,expected=0,options=(),interrupt=False):
 case=root/name;tests=case/'tests';tests.mkdir(parents=True);beacon=case/'events.jsonl'
 prelude='import {defineNativeSuite} from '+json.dumps(api)+';import assert from "node:assert/strict";import {appendFileSync,existsSync,readdirSync,writeFileSync,openSync,closeSync,unlinkSync} from "node:fs";const root='+json.dumps(str(case))+';const mark=(phase)=>appendFileSync('+json.dumps(str(beacon))+',JSON.stringify({phase,pid:process.pid,native:process.ppid,at:process.hrtime.bigint().toString()})+"\\n");const delay=ms=>new Promise(r=>setTimeout(r,ms));const entered=()=>writeFileSync(root+"/entered-"+process.pid,"");const waitFor=async f=>{while(!f())await delay(5)};\n'
 for name_,source in files.items():(tests/name_).write_text(prelude+source+'\n')
 command=[str(a.supervisor),'--report',str(case/'supervisor.json'),'--timeout-ms','45000','--cleanup-grace-ms','1000','--physical-ms','10000','--',str(a.node),str(a.sdk_directory/'native-runner-cli.js'),'--executable',str(a.executable),'--test-dir',str(tests),'--report',str(case/'actual.json'),'--workers',str(workers),*options]
 started=time.monotonic_ns();signal_receipt=None
 with (case/'stdout').open('wb') as out,(case/'stderr').open('wb') as err:
  child=subprocess.Popen(command,stdout=out,stderr=err)
  if interrupt:
   stop=time.monotonic()+15
   while child.poll() is None and time.monotonic()<stop:
    events=[json.loads(x) for x in beacon.read_text().splitlines() if x.endswith('}')] if beacon.exists() else []
    passed=next((e for e in events if e['phase']=='completed-pass'),None);held=next((e for e in events if e['phase']=='held-start'),None)
    if passed and held and not Path('/proc',str(passed['pid'])).exists():
     native=held['native'];assert passed['native']==native and Path('/proc',str(native),'exe').resolve()==a.executable.resolve()
     stat=Path('/proc',str(native),'stat').read_text();identity=stat[stat.rfind(')')+2:].split()[19];os.kill(native,signal.SIGINT);signal_receipt={'actualNativePID':native,'nativeKernelStart':identity,'retiredSuccessfulHostPID':passed['pid'],'heldHostPID':held['pid'],'signal':signal.SIGINT,'sentMonotonicNs':time.monotonic_ns()};break
    time.sleep(.01)
  result=child.wait()
  if interrupt:assert signal_receipt,'No genuine completed-host/held-peer signal admission; actual supervisor drained before rejection'
 physical=json.loads((case/'supervisor.json').read_text());row={'name':name,'command':command,'exit':result,'elapsedNs':time.monotonic_ns()-started,'supervisor':physical,'signalReceipt':signal_receipt};groups.append(row);index()
 assert result==expected and physical['childExit']=={'signaled':False,'code':expected,'rawWaitpidStatus':expected<<8},row
 assert not physical['cleanupFailed'] and not physical['timeout'] and physical['descendantsRetired'] and (case/'stderr').read_bytes()==b'',row
 doc=json.loads((case/'actual.json').read_text());events=[json.loads(x) for x in beacon.read_text().splitlines()] if beacon.exists() else []
 pids={physical['childPID'],doc['directory']['coordinatorPID']};pids.update(w['pid'] for c in doc['directory']['collections'] for w in c['workers']);pids.update(w['pid'] for s in doc['sources'] for w in s['report']['workers']);pids.update(x['writerPid'] for s in doc['sources'] for x in s['report'].get('attachments',[]))
 remaining=[pid for pid in pids if Path('/proc',str(pid)).exists()];assert not remaining,remaining
 budget=doc['directory']['nativeBudgetReceipt'];assert all(budget[k]==0 for k in ('physicalWorkers','physicalIO','physicalSteps','physicalSoft','physicalSources','publicationPending'))
 intervals=[]
 for collection in doc['directory']['collections']:
  timing=collection['timing'];assert timing and timing['actorCompletedMonotonicMs']>=timing['actorAdmittedMonotonicMs']
  assert {(h['workerIndex'],h['pid']) for h in timing['hosts']}=={(w['workerIndex'],w['pid']) for w in collection['workers']}
 for segment in doc['directory']['segments']:
  if not segment['sourceId']:continue
  source=next(s['report'] for s in doc['sources'] if s['sourceId']==segment['sourceId']);timing=segment['timing'];assert timing
  assert {(h['workerIndex'],h['pid']) for h in timing['hosts']}=={(w['workerIndex'],w['pid']) for w in source['workers']}
  for h in timing['hosts']:
   assert h['admittedMonotonicMs']<=h['waitpidCapturedMonotonicMs']<=timing['actorCompletedMonotonicMs']
   if h['metadataDecodedMonotonicMs'] is not None:assert h['admittedMonotonicMs']<=h['metadataDecodedMonotonicMs']<=h['waitpidCapturedMonotonicMs']
   intervals.extend([(h['admittedMonotonicMs'],1),(h['waitpidCapturedMonotonicMs'],-1)])
 active=peak=0
 for _,delta in sorted(intervals,key=lambda x:(x[0],x[1])):active+=delta;peak=max(peak,active)
 assert active==0 and peak<=workers,(active,peak)
 row.update(actualPIDs=sorted(pids),remainingPIDs=remaining,peakNativeHostReservations=peak,events=events,report=str(case/'actual.json'));index();return doc,events,case

try:
 def ordinary(title,body,timeout=10000):return 'export default defineNativeSuite({tests:[{title:'+json.dumps(title)+',timeout:'+str(timeout)+',run:async(_,info)=>{'+body+'}}]});'
 d,e,c=run('actual-two-body-rendezvous',{'a.spec.mjs':ordinary('a','mark("start");entered();await waitFor(()=>readdirSync(root).filter(x=>x.startsWith("entered-")).length===2);mark("end");'),'b.spec.mjs':ordinary('b','mark("start");entered();await waitFor(()=>readdirSync(root).filter(x=>x.startsWith("entered-")).length===2);mark("end");')});assert d['summary']['expected']==2 and groups[-1]['peakNativeHostReservations']==2 and max(int(x['at']) for x in e if x['phase']=='start')<min(int(x['at']) for x in e if x['phase']=='end')
 d,e,c=run('workers-two-ceiling-four-files',{str(n)+'.spec.mjs':ordinary(str(n),'mark("start");await delay(80);mark("end");') for n in range(4)});assert d['summary']['expected']==4 and groups[-1]['peakNativeHostReservations']==2
 d,e,c=run('early-failure-stops-held-peer',{'a.spec.mjs':ordinary('failure','await waitFor(()=>existsSync(root+"/held"));mark("failed");throw Error("actual early directory failure");'),'b.spec.mjs':'export default defineNativeSuite({tests:[{title:"held",timeout:10000,run:async(_,info)=>{writeFileSync(root+"/held","");mark("held-start");await new Promise(r=>info.signal.addEventListener("abort",r,{once:true}));mark("held-aborted");}},{title:"forbidden later body",run:()=>mark("FORBIDDEN")} ]});','c.spec.mjs':ordinary('unadmitted','mark("FORBIDDEN");')},expected=1,options=['--max-failures','1']);assert any(x['phase']=='held-aborted' for x in e) and not any(x['phase']=='FORBIDDEN' for x in e);assert {'failed','interrupted','notRun'}<=set(x['status'] for x in d['directory']['segments'])
 d,e,c=run('completed-pass-survives-peer-sigint',{'a.spec.mjs':ordinary('completed','mark("completed-pass");'),'b.spec.mjs':ordinary('held','mark("held-start");await new Promise(r=>info.signal.addEventListener("abort",r,{once:true}));mark("held-aborted");')},expected=130,interrupt=True);assert [x['status'] for x in d['directory']['segments']]==['passed','interrupted'];assert d['sources'][0]['report']['globalErrors']==[] and d['sources'][0]['report']['attempts'][0]['status']=='passed'
 locked='export default defineNativeSuite({tests:[{title:"lock",lock:"actual shared directory resource",run:async()=>{const fd=openSync(root+"/exclusive","wx");try{mark("locked");await delay(30)}finally{closeSync(fd);unlinkSync(root+"/exclusive")}}}]});'
 d,e,c=run('native-locks-conservatively-serialize',{'a.spec.mjs':locked,'b.spec.mjs':locked},workers=4);assert d['summary']['expected']==2 and d['directory']['resourceLocksSerializeFiles'] and d['directory']['activeFileLimit']==1 and groups[-1]['peakNativeHostReservations']==1
 outputs=ordinary('output','writeFileSync(info.outputPath("actual.txt"),"actual output");await info.attach("independent",{body:Buffer.from("actual immutable artifact")});')
 d,e,c=run('retention-after-real-cohort-drain',{'a.spec.mjs':outputs,'b.spec.mjs':outputs},options=['--preserve-output','never']);assert d['summary']['expected']==2
 for source in d['sources']:
  report=source['report'];assert len(report['outputRetention'])==1 and report['outputRetention'][0]['status']=='removed' and report['outputRetention'][0]['availability']=='originalLocationAbsent'
  assert not Path(report['outputRetention'][0]['originalPath']).exists() and Path(report['attachments'][0]['path']).read_bytes()==b'actual immutable artifact'
 credit=ordinary('actual bounded references','info.attachments.push(...Array.from({length:70},(_,n)=>({name:"actual"+n,contentType:"native/absent"})));')
 d,e,c=run('shared-declaration-credit-under-overlap',{'a.spec.mjs':credit,'b.spec.mjs':credit},expected=1);assert 70<=sum(len(x['report'].get('attachmentDeclarations',[])) for x in d['sources'])<=128 and d['summary']['unexpected']==1
 index();print(json.dumps({'passed':True,'groups':len(groups),'index':str(root/'index.json')}))
except BaseException:index();print('Preserved actual first negative:',root/'index.json',flush=True);raise
