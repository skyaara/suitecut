# SPDX-License-Identifier: MIT
"""Genuine native directory discovery, collection, actor and shared-budget proof.

No browser, external runner, authored scheduler or supplied report IDs. Each
case launches the real public CLI under the native process-tree supervisor.
"""
import argparse, hashlib, json, os, shutil, subprocess, tempfile, time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--executable',type=Path,required=True);p.add_argument('--sdk-directory',type=Path,required=True);p.add_argument('--supervisor',type=Path,required=True);p.add_argument('--directory',type=Path,required=True);p.add_argument('--node',default=shutil.which('node'));a=p.parse_args()
a.directory.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='directory-',dir=a.directory));groups=[]
inputs=[Path(__file__).resolve(),a.executable.resolve(),a.supervisor.resolve(),Path(a.node).resolve(),*sorted(a.sdk_directory.glob('*.js'))]
pins={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs};(root/'inputs-before.json').write_text(json.dumps(pins,indent=2)+'\n')
api=(a.sdk_directory/'native.js').as_uri()
def run(name,files,options=(),expected=0,allow_failed_collection=False):
 case=root/name;tests=case/'tests';tests.mkdir(parents=True);beacon=case/'actual-beacons'
 for relative,body in files.items():
  target=tests/relative;target.parent.mkdir(parents=True,exist_ok=True)
  target.write_text('import {defineNativeSuite} from '+json.dumps(api)+';\nimport assert from "node:assert/strict";\nimport {appendFileSync} from "node:fs";\nconst beacon='+json.dumps(str(beacon))+';\nconst mark=x=>appendFileSync(beacon,x+"\\n");\n'+body+'\n')
 report=case/'actual.json';supervised=case/'supervisor.json';args=[str(a.supervisor),'--report',str(supervised),'--timeout-ms','45000','--cleanup-grace-ms','1000','--physical-ms','10000','--',a.node,str(a.sdk_directory/'native-runner-cli.js'),'--executable',str(a.executable),'--test-dir',str(tests),'--report',str(report),*options]
 began=time.monotonic()
 with (case/'stdout').open('wb') as out,(case/'stderr').open('wb') as err:result=subprocess.run(args,stdout=out,stderr=err)
 physical=json.loads(supervised.read_text());row={'name':name,'argv':args,'exit':result.returncode,'elapsedSeconds':time.monotonic()-began,'supervisor':physical,'report':str(report),'stdout':str(case/'stdout'),'stderr':str(case/'stderr')};groups.append(row);index()
 assert result.returncode==expected,(name,result.returncode,(case/'stderr').read_text(),report.read_text()[:3000] if report.exists() else 'no report')
 assert physical['childExit']=={'signaled':False,'code':expected,'rawWaitpidStatus':expected<<8},row
 assert not physical['cleanupFailed'] and physical['descendantsRetired'] and not physical['timeout'],row
 assert (case/'stderr').read_bytes()==b'',row
 document=json.loads(report.read_text());
 actual_pids={physical['childPID'],document['directory']['coordinatorPID']}
 for c in document['directory']['collections']: actual_pids.update(w['pid'] for w in c['workers'])
 for source in document['sources']:
  actual_pids.update(w['pid'] for w in source['report']['workers'])
  actual_pids.update(w['writerPid'] for w in source['report'].get('attachments',[]))
 remaining=[pid for pid in actual_pids if Path('/proc',str(pid)).exists()]
 row['actualPIDs']=sorted(actual_pids);row['remainingPIDs']=remaining;assert not remaining,(name,remaining)
 budget=document['directory']['nativeBudgetReceipt'];assert all(budget[key]==0 for key in ('physicalWorkers','physicalIO','physicalSteps','physicalSoft','physicalSources','publicationPending'))
 marks=beacon.read_text().splitlines() if beacon.exists() else []
 for collection in document['directory']['collections']:
  for worker in collection['workers']:
   assert worker['pid']>0 and 0<=worker['exitCode']<=255 and (not worker['signaled'] or worker['exitCode']>0),(name,worker)
   if not allow_failed_collection: assert worker['exitCode']==0 and not worker['signaled'],(name,worker)
 row['marks']=marks;row['summary']=document['summary'];index();return document,marks

def index():
 changed=[name for name,digest in pins.items() if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=digest]
 (root/'index.json').write_text(json.dumps({'scope':'Actual serial native multi-file activation; concurrent file throughput pending','inputs':pins,'changed':changed,'groups':groups},indent=2)+'\n');assert not changed,changed

def test(title,mark,extra=''):
 return 'export default defineNativeSuite({tests:[{title:'+json.dumps(title)+',run:async(_,i)=>{assert.equal(i.file,import.meta.filename);mark('+json.dumps(mark)+');'+extra+'}}]});'

try:
 d,m=run('discovery-and-real-attachments',{'a.spec.mjs':test('a','a','await i.attach("real a",{body:Buffer.from("actual a")});'),'nested/b.test.mjs':test('b','b'),'node_modules/ignored.spec.mjs':test('ignored','bad'),'other.mjs':test('other','bad')},['--workers','8','--junit',str(root/'directory.xml'),'--html',str(root/'directory.html')]);assert sorted(m)==['a','b'] and d['summary']['expected']==2 and len(d['directory']['files'])==2
 assert all(f['inode']!='0' and f['collectionProtocolUTF8Hex'] for f in d['directory']['files'])
 import xml.etree.ElementTree as ET
 xml=ET.parse(root/'directory.xml').getroot();assert int(xml.attrib['tests'])==len(xml.findall('.//testcase')) and int(xml.attrib['errors'])==len(xml.findall('.//error'))
 assert 'Native directory policy and physical receipts' in (root/'directory.html').read_text()
 assert len({s['sourceId'] for s in d['sources']})==2 and all(s['report']['attempts'][0]['testId']=='1' for s in d['sources'])
 artifacts=[x for s in d['sources'] for x in s['report']['attachments']];assert len(artifacts)==1 and Path(artifacts[0]['path']).read_bytes()==b'actual a'
 def fixture_file(value):
  return 'export default defineNativeSuite({fixtures:{w:{scope:"worker",setup:async(_,use)=>{mark("setup-'+value+'");await use({value:"'+value+'",pid:process.pid});mark("teardown-'+value+'")}}},tests:[{title:"actual file fixture '+value+'",fixtures:["w"],run:async({w},i)=>{assert.equal(w.value,"'+value+'");assert.equal(w.pid,process.pid);assert.equal(i.file,import.meta.filename);mark("body-'+value+'");await i.step("native file step",()=>{});}}]});'
 d,m=run('actual-per-file-fixture-and-step-owners',{'a.spec.mjs':fixture_file('a'),'b.spec.mjs':fixture_file('b')});assert m==['setup-a','body-a','teardown-a','setup-b','body-b','teardown-b']
 steps=[x for source in d['sources'] for x in source['report']['steps']];assert len(steps)==2 and len({x['id'] for x in steps})==2 and d['directory']['nativeBudgetReceipt']['stepRecords']==2
 d,m=run('native-list-no-callbacks',{'a.spec.mjs':test('a','bad'),'b.spec.mjs':test('b','bad')},['--list']);assert not m and not d['sources'] and len(d['directory']['files'])==2 and all(t['selected'] for f in d['directory']['files'] for t in f['tests'])
 d,m=run('project-default-worker-fixture-clock',{'a.spec.mjs':'export default defineNativeSuite({fixtures:{w:{scope:"worker",setup:async(_,use,info)=>{assert.equal(info.timeout,8765);mark("clock-setup");await use("actual");mark("clock-teardown")}}},projects:[{name:"clock",timeout:8765,tests:[{title:"clock",fixtures:["w"],run:({w})=>{assert.equal(w,"actual");mark("clock-body")}}]}]});'},['--project','clock']);assert m==['clock-setup','clock-body','clock-teardown'] and d['summary']['expected']==1
 d,m=run('config-empty-local-partition',{'a.spec.mjs':'export default defineNativeSuite({grep:/chosen/,tests:[{title:"excluded",run:()=>mark("bad")}]});','b.spec.mjs':'export default defineNativeSuite({grep:/chosen/,tests:[{title:"chosen",run:()=>mark("chosen")}]});'});assert m==['chosen'] and len(d['directory']['files'])==2 and len(d['sources'])==1
 d,m=run('cross-file-focus',{'a.spec.mjs':test('a','bad'),'b.spec.mjs':'export default defineNativeSuite({tests:[{title:"only",only:true,run:()=>mark("only")}]});'});assert m==['only'] and d['summary']['tests']==1
 d,m=run('global-shard',{'a.spec.mjs':test('a','bad'),'b.spec.mjs':test('b','b')},['--shard','2/2']);assert m==['b'] and d['summary']['tests']==1
 d,m=run('native-patterns',{'a.spec.mjs':test('a','bad'),'nested/b.test.mjs':test('b','b'),'nested/c.spec.mjs':test('c','bad')},['--test-match','**/*.{spec,test}.mjs','--test-ignore','**/a.spec.mjs','--file-filter','b\\.test']);assert m==['b']
 d,m=run('global-empty-selection',{'a.spec.mjs':test('a','bad')},['--grep','absent'],1);assert not m and d['summary']['runFailed'] and any('No tests found' in x for x in d['directory']['errors'])
 d,m=run('empty-explicit-match',{'a.spec.mjs':test('a','bad')},['--file-filter','does-not-match','--pass-with-no-tests']);assert not m and not d['summary']['runFailed']
 d,m=run('global-empty-shard',{'a.spec.mjs':test('a','bad')},['--shard','4/4']);assert not m and d['directory']['emptyShard'] and not d['summary']['runFailed']
 graph=[{'name':'setup','tests':[]},{'name':'browser','dependencies':['setup'],'teardown':'cleanup','tests':[]},{'name':'cleanup','tests':[]}]
 def project_file(project,marker,fail=False):
  rows=[]
  for row in graph:
   data={k:v for k,v in row.items() if k!='tests'};data=json.dumps(data)[:-1]+',"tests":['
   if row['name']==project:data+='{title:'+json.dumps(marker)+',run:()=>{mark('+json.dumps(marker)+');'+('throw new Error("actual dependency failure");' if fail else '')+'}}'
   rows.append(data+']}')
  return 'export default defineNativeSuite({projects:['+','.join(rows)+']});'
 d,m=run('cross-file-dependency-teardown',{'a.spec.mjs':project_file('setup','setup'),'b.spec.mjs':project_file('browser','browser'),'c.spec.mjs':project_file('cleanup','cleanup')},['--project','browser']);assert m==['setup','browser','cleanup'] and d['summary']['expected']==3
 # Every project declares real tests before config filtering. Genuinely
 # declaration-empty file/project partitions remain a distinct native policy.
 filtered=[]
 for ordinal in range(3):
  rows=[]
  for row in graph:
   marker='body chosen' if row['name']=='browser' and ordinal==0 else row['name']+' excluded'
   data={k:v for k,v in row.items() if k!='tests'}
   rows.append(json.dumps(data)[:-1]+',"tests":[{title:'+json.dumps(marker)+',run:()=>mark('+json.dumps(marker)+')}]}')
  filtered.append((str(ordinal)+'.spec.mjs','export default defineNativeSuite({grep:/body chosen/,projects:['+','.join(rows)+']});'))
 d,m=run('config-filtered-empty-support',dict(filtered),['--project','browser','--grep','body chosen']);assert m==['body chosen'] and len(d['sources'])==1 and len(d['directory']['segments'])==1
 projects={p['name']:p for p in d['directory']['projects']};assert all(projects[n]['status']=='passed' for n in ('setup','browser','cleanup')) and projects['setup']['selectedGlobalIds']==[] and projects['cleanup']['selectedGlobalIds']==[]
 d,m=run('failed-dependency-unrun',{'a.spec.mjs':project_file('setup','setup-failed',True),'b.spec.mjs':project_file('browser','bad'),'c.spec.mjs':project_file('cleanup','cleanup')},['--project','browser'],1);assert m==['setup-failed','cleanup'] and d['summary']['runFailed'] and any(s['status']=='notRun' and s['selectedLocalIds'] for s in d['directory']['segments'])
 d,m=run('global-max-failures',{'a.spec.mjs':'export default defineNativeSuite({tests:[{title:"first",run:()=>{mark("first");throw new Error("actual first failure")}},{title:"unadmitted",run:()=>mark("bad")} ]});','b.spec.mjs':test('b','bad')},['--max-failures','1'],1);assert m==['first'] and any(s['status']=='notRun' and s['selectedLocalIds'] for s in d['directory']['segments'])
 d,m=run('shared-declaration-credit',{'a.spec.mjs':test('first','first','i.attachments.push(...Array.from({length:70},(_,n)=>({name:"first"+n,contentType:"x/native"})));'),'b.spec.mjs':test('second','second','i.attachments.push(...Array.from({length:70},(_,n)=>({name:"second"+n,contentType:"x/native"})));')},expected=1)
 declarations=[x for s in d['sources'] for x in s['report'].get('attachmentDeclarations',[])];assert 70<=len(declarations)<=128 and d['summary']['unexpected']==1 and any('capacity' in str(s['report']) for s in d['sources'])
 d,m=run('actual-collection-exit',{'a.spec.mjs':'process.exit(17);export default defineNativeSuite({tests:[]});'},expected=1,allow_failed_collection=True);assert d['summary']['runFailed'] and not d['sources'] and any(w['exitCode']==17 for c in d['directory']['collections'] for w in c['workers'])
 d,m=run('one-original-global-deadline',{'a.spec.mjs':'await new Promise(r=>setTimeout(r,120));'+test('first','first','await new Promise(r=>setTimeout(r,350));'),'b.spec.mjs':'await new Promise(r=>setTimeout(r,120));'+test('second','second','await new Promise(r=>setTimeout(r,350));')},['--global-timeout','1100'],1,allow_failed_collection=True);assert d['summary']['runFailed'] and 'Native suite original global timeout of 1100ms exceeded' in str(d)
 case=root/'input-output-alias';case.mkdir();tests=case/'tests';tests.mkdir();source=tests/'actual.spec.mjs';source.write_text('throw new Error("No authored import allowed before collision rejection");');original=source.read_bytes()
 supervised=case/'supervisor.json';command=[str(a.supervisor),'--report',str(supervised),'--timeout-ms','15000','--cleanup-grace-ms','1000','--physical-ms','10000','--',a.node,str(a.sdk_directory/'native-runner-cli.js'),'--executable',str(a.executable),'--test-dir',str(tests),'--report',str(source)]
 with (case/'stdout').open('wb') as out,(case/'stderr').open('wb') as err: result=subprocess.run(command,stdout=out,stderr=err)
 physical=json.loads(supervised.read_text());assert result.returncode==2 and physical['childExit']['code']==2 and not physical['cleanupFailed'] and physical['descendantsRetired'] and not physical['timeout']
 assert source.read_bytes()==original and b'aliases a collection input' in (case/'stderr').read_bytes()
 groups.append({'name':'input-output-alias','argv':command,'exit':result.returncode,'supervisor':physical,'originalSourceSHA256':hashlib.sha256(original).hexdigest(),'stdout':str(case/'stdout'),'stderr':str(case/'stderr'),'scope':'Expected native validation error, real nonempty stderr preserved; authored input unchanged'})
 index();print(json.dumps({'groups':len(groups),'index':str(root/'index.json'),'allPassed':True}))
except BaseException:
 index();print('Preserved first failure:',root/'index.json',flush=True);raise
