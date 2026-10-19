# SPDX-License-Identifier: MIT
"""Actual configured project output roots, filesystem ownership and retention."""
import argparse,json,pathlib,subprocess,tempfile,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);p.add_argument('--directory',required=True,type=pathlib.Path);p.add_argument('--supervisor',required=True,type=pathlib.Path);a=p.parse_args();receipt=a.directory.resolve();receipt.mkdir(parents=True,exist_ok=True);out=pathlib.Path(tempfile.mkdtemp(prefix='project-output-',dir=receipt));sdk=a.sdk_directory.resolve();rows=[]
common='import{defineNativeSuite}from '+json.dumps((sdk/'native.js').as_uri())+";import{strict as assert}from'node:assert';import{existsSync,statSync,writeFileSync,readFileSync,appendFileSync}from'node:fs';"
def absent(pid):return not pathlib.Path('/proc',str(pid)).exists()
def reports(data):return [v['report']for v in data['sources']]if data.get('kind')=='suitecut-native-merged-report'else[data]
def validate_actual(data):
 records=[]
 for report in reports(data):
  attempts=report['attempts'];outputs=report['outputs'];assert len(outputs)==len(attempts)
  for worker in report['workers']:assert absent(worker['pid'])
  for o in outputs:
   attempt=next(t for t in attempts if all(t[k]==o[k]for k in ['testId','generation','workerIndex','pid']))
   assert absent(o['writerPid'])and absent(attempt['pid'])
   if o['status']=='ready':
    assert o['readyReceipt']and o['writerExitCode']==0 and not o['writerSignaled'];assert o['writerExitReceiptMonotonicMs']<=o['physicalReceiptMonotonicMs']<=o['assignmentReceiptMonotonicMs']<o['deadlineMonotonicMs']
    retention=next((v for v in report.get('outputRetention',[])if v['outputId']==o['id']),None)
    if retention and retention['status']=='removed':assert not pathlib.Path(o['outputDir']).exists()and retention['helperExitCode']==0 and not retention['helperSignaled']and absent(retention['helperPid'])
    else:
     root=pathlib.Path(o['outputDir']);st=root.stat();assert root.is_dir()and st.st_dev==o['device']and str(st.st_ino)==o['inode']
   records.append((report,o,attempt))
 return records

def run(name,source=None,options=(),code=0,setup=None,files=None,listing=False):
 root=out/name;root.mkdir();report=root/'actual.json';suite=root/'suite.mjs';authored=[]
 if source is not None:suite.write_text(common+source(root));authored.append(suite)
 if setup:setup(root)
 if files is not None:
  testdir=root/'tests';testdir.mkdir()
  for path,text in files(root).items():
   target=testdir/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(common+text);authored.append(target)
  selection=['--test-dir',str(testdir)]
 else:selection=['--suite',str(suite)]
 for i,target in enumerate(authored):
  syntax=subprocess.run([a.node,'--check',str(target)],capture_output=True,timeout=15);(root/('fixture-check-'+str(i)+'.stdout')).write_bytes(syntax.stdout);(root/('fixture-check-'+str(i)+'.stderr')).write_bytes(syntax.stderr);assert syntax.returncode==0,(name,target,syntax.stderr)
 child=[str(a.executable.resolve()),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),*selection,'--report',str(report),*options]
 if not listing:child+=['--junit',str(root/'actual.xml'),'--html',str(root/'actual.html')]
 command=[str(a.supervisor.resolve()),'--report',str(root/'supervisor.json'),'--timeout-ms','60000','--cleanup-grace-ms','2000','--physical-ms','10000','--',*child]
 result=subprocess.run(command,capture_output=True,timeout=75);(root/'stdout.bin').write_bytes(result.stdout);(root/'stderr.bin').write_bytes(result.stderr);physical=json.loads((root/'supervisor.json').read_text());assert result.returncode==code and not result.stderr,(name,result.returncode,result.stdout,result.stderr)
 assert physical['childExit']['code']==code and not physical['childExit']['signaled']and physical['descendantsRetired']and not physical['cleanupFailed']and not physical['timeout'];assert absent(physical['childPID'])and absent(physical['supervisorPID'])
 data=json.loads(report.read_text());records=[]if listing else validate_actual(data)
 if not listing:
  assert ET.parse(root/'actual.xml').getroot().tag=='testsuites';assert '<html'in(root/'actual.html').read_text().lower()
 rows.append({'name':name,'actualExitCode':result.returncode,'report':str(report),'supervisor':str(root/'supervisor.json'),'actualOutputs':len(records)});return root,data,records

def roots_source(root):
 roots={'relative':'artifacts/../project-relative','absolute':str(root/'project-absolute')};fallback=root/'fallback';mark=root/'callbacks.jsonl'
 run="const run=(_,i)=>{const expected=i.project.name==='relative'?"+json.dumps(str(root/'project-relative'))+":i.project.name==='absolute'?"+json.dumps(str(root/'project-absolute'))+":"+json.dumps(str(fallback))+";assert.equal(i.outputDir.startsWith(expected+'/'),true);if(i.project.name==='fallback')assert.deepEqual(i.project,{name:'fallback',metadata:{}});else assert.equal(i.project.outputDir,expected);writeFileSync(i.outputPath('actual.txt'),i.project.name);appendFileSync("+json.dumps(str(mark))+",JSON.stringify({project:i.project.name,root:i.outputDir})+'\\n')};"
 return run+'export default defineNativeSuite({projects:['+','.join('{name:'+json.dumps(name)+(','+'outputDir:'+json.dumps(path)if path is not None else'')+",tests:[{title:'same title',run}]}"for name,path in [*roots.items(),('fallback',None)])+']});'
# Three native serial project owners, including untouched fallback project-info shape.
root,data,records=run('project-roots-and-native-fallback',roots_source,options=('--output-directory',str(out/'project-roots-and-native-fallback'/'fallback')));assert len(records)==3
assert {pathlib.Path(o['outputDir']).parent for _,o,_ in records}=={root/'project-relative',root/'project-absolute',root/'fallback'}
# Listing carries declarations, creates no output roots and invokes no callbacks.
root,data,_=run('listing-is-metadata-only',lambda r:"export default defineNativeSuite({projects:[{name:'relative',outputDir:'nested/../configured',tests:[{title:'not run',run:()=>writeFileSync("+json.dumps(str(r/'FORBIDDEN'))+",'forbidden')}]}]});",options=('--list',),listing=True);assert data['projects'][0]['outputDir']==str(root/'configured')and not(root/'configured').exists()and not(root/'FORBIDDEN').exists()and 'attempts'not in data and len(data['collectionWorkers'])==1
# Unique shared native output IDs across actual parallel workers and a genuine retry.
root,data,records=run('parallel-projects-retries-and-utf16-titles',lambda r:"const run=(_,i)=>{assert.equal(i.project.outputDir,"+json.dumps(str(r))+"+'/'+i.project.name);writeFileSync(i.outputPath('actual.txt'),i.project.name+' retry'+i.retry);if(i.project.name==='one'&&i.title==='retry'&&!i.retry)throw Error('actual first attempt')};export default defineNativeSuite({fullyParallel:true,projects:[{name:'one',outputDir:'one',tests:[{title:'retry',retries:1,run},{title:'same \\ud800',run}]},{name:'two',outputDir:'two',tests:[{title:'same \\ud800',run},{title:'../same',run}]}]});",options=('--workers','4'));assert data['summary']['flaky']==1 and len(records)==5 and len({o['outputDir']for _,o,_ in records})==5 and len({o['id']for _,o,_ in records})==5 and len({o['workerIndex']for _,o,_ in records})>=2
for _,o,t in records:assert pathlib.Path(o['outputDir'],'actual.txt').read_text()==('one'if pathlib.Path(o['outputDir']).parent.name=='one'else'two')+' retry'+str(t['retry'])
# Different actual files may have reversed local project indexes and roots.
def directory_files(r):
 def module(name,reverse=False):
  names=['right','left']if reverse else['left','right'];return "const run=(_,i)=>{assert.equal(i.project.outputDir,new URL('./'+i.project.name+'-out',import.meta.url).pathname);writeFileSync(i.outputPath('actual.txt'),"+json.dumps(name)+"+' '+i.project.name)};export default defineNativeSuite({projects:["+','.join("{name:"+json.dumps(n)+",outputDir:"+json.dumps(n+'-out')+",tests:[{title:'same title',run}]}"for n in names)+']});'
 return {'a.spec.mjs':module('a'),'nested/b.spec.mjs':module('b',True)}
root,data,records=run('directory-actual-local-project-identity',files=directory_files,options=('--project','left'));assert len(records)==2 and len(data['sources'])==2 and not data['directory']['runFailed']
assert {pathlib.Path(o['outputDir']).parent for _,o,_ in records}=={root/'tests/left-out',root/'tests/nested/left-out'};assert not(root/'tests/right-out').exists()and not(root/'tests/nested/right-out').exists()
# Shared configured roots preserve foreign content and use native identity names.
root,data,records=run('same-root-foreign-content-and-path-isolation',lambda r:"const run=(_,i)=>writeFileSync(i.outputPath('actual.txt'),i.project.name);export default defineNativeSuite({projects:[{name:'left',outputDir:'shared',tests:[{title:'../../same',run}]},{name:'right',outputDir:'shared',tests:[{title:'../../same',run}]}]});",options=('--workers','2'),setup=lambda r:((r/'shared').mkdir(),(r/'shared/foreign').write_text('immutable foreign bytes')));assert len({o['outputDir']for _,o,_ in records})==2 and (root/'shared/foreign').read_text()=='immutable foreign bytes'
# Scalar Unicode roots and an explicit empty relative declaration remain filesystem paths.
root,data,records=run('unicode-and-empty-relative-roots',lambda r:"const expected="+json.dumps({'unicode':str(r/'root-π-😀'),'empty':str(r)})+";const run=(_,i)=>{assert.equal(i.project.outputDir.replace(/\\/$/u,''),expected[i.project.name]);writeFileSync(i.outputPath('actual.txt'),i.project.name)};export default defineNativeSuite({projects:[{name:'unicode',outputDir:'root-π-😀',tests:[{title:'unicode root',run}]},{name:'empty',outputDir:'',tests:[{title:'module root',run}]}]});");assert {pathlib.Path(o['outputDir']).parent for _,o,_ in records}=={root,root/'root-π-😀'}
# Retention removes only the actual assigned child; original committed attachment remains.
root,data,records=run('project-roots-retention-never-real-attachments',lambda r:"const run=async(_,i)=>{writeFileSync(i.outputPath('actual.txt'),'actual source '+i.project.name);await i.attach('real copied source',{path:i.outputPath('actual.txt')})};export default defineNativeSuite({projects:[{name:'one',outputDir:'one',tests:[{title:'one',run}]},{name:'two',outputDir:'two',tests:[{title:'two',run}]}]});",options=('--workers','2','--preserve-output','never'));assert len(records)==2 and all(v['status']=='removed'for v in data['outputRetention'])
for artifact in data['attachments']:
 path=pathlib.Path(artifact['path']);st=path.stat();assert str(st.st_ino)==artifact['inode']and str(st.st_dev)==artifact['device']and path.read_text().startswith('actual source ')and artifact['writerExitConfirmed']and artifact['writerExitCode']==0 and absent(artifact['writerPid'])
root,data,records=run('project-roots-retention-failures-only',lambda r:"const run=(_,i)=>{writeFileSync(i.outputPath('actual.txt'),i.project.name);if(i.project.name==='bad')throw Error('actual project failure')};export default defineNativeSuite({projects:[{name:'good',outputDir:'good',tests:[{title:'good',run}]},{name:'bad',outputDir:'bad',tests:[{title:'bad',run}]}]});",options=('--preserve-output','failures-only'),code=1);assert data['summary']['unexpected']==1 and {v['status']for v in data['outputRetention']}=={'kept','removed'}
# Explicit bad root must fail actual mkdir before any author callback; no fallback.
root,data,records=run('actual-configured-root-mkdir-error',lambda r:"export default defineNativeSuite({projects:[{name:'broken',outputDir:'blocked',tests:[{title:'real root failure',expectedFailure:true,run:()=>writeFileSync("+json.dumps(str(r/'FORBIDDEN'))+",'forbidden')}]}]});",setup=lambda r:(r/'blocked').write_text('immutable regular file'),code=1);assert len(records)==1 and records[0][1]['status']=='failed'and not records[0][2]['expected']and not(root/'FORBIDDEN').exists()and(root/'blocked').read_text()=='immutable regular file'and not(root/'test-results').exists()
# A retry cannot redirect its original file/project catalog to a new root.
root,data,records=run('retry-root-catalog-mutation-rejected',lambda r:"const changed=Number(process.argv[3])>1;export default defineNativeSuite({projects:[{name:'stable',outputDir:changed?'second':'first',tests:[{title:'first actual failure',retries:1,run:(_,i)=>{writeFileSync(i.outputPath('actual.txt'),'first actual body');throw Error('actual first failure')}}]}]});",code=1);assert len(records)==1 and any('suite metadata changed'in e for e in data['globalErrors'])and not(root/'second').exists()
(receipt/'report.json').write_text(json.dumps({'passed':len(rows),'scope':'Actual native configured per-project output roots; original native owner IDs/budgets/retries, real atomic directory preparation/writer exits/host ACK, global fallback, no-callback listing, parallel and cross-file local project identity, truthful mkdir failure, retention and committed attachments. No Source/browser/snapshot behavior claim.','groups':rows},indent=2)+'\n');print('PASS',len(rows),'actual native per-project output root groups')
