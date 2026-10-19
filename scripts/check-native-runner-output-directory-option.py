# SPDX-License-Identifier: MIT
"""Genuine native and public SDK/CLI output root selection; no host mkdir."""
import argparse,json,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path);p.add_argument('--sdk-directory',required=True,type=pathlib.Path);p.add_argument('--node',required=True);p.add_argument('--directory',required=True,type=pathlib.Path);a=p.parse_args();receipt=a.directory.resolve();receipt.mkdir(parents=True,exist_ok=True);out=pathlib.Path(tempfile.mkdtemp(prefix='cases-',dir=receipt));sdk=a.sdk_directory.resolve();rows=[]
common='import{defineNativeSuite}from '+json.dumps((sdk/'native.js').as_uri())+";import{strict as assert}from'node:assert';import{writeFileSync,statSync}from'node:fs';"
def run(name,mode='native',retry=False,broken=False):
 root=out/name;root.mkdir();chosen=root/'selected-目录'/('real-root'if not broken else'not-directory');chosen.parent.mkdir()
 if broken:chosen.write_text('actual regular file must survive')
 suite=root/'suite.mjs';suite.write_text(common+"export default defineNativeSuite({tests:[{title:'actual chosen root',expectedFailure:"+str(broken).lower()+",retries:"+str(1 if retry else 0)+",run:(_,i)=>{assert(statSync(i.outputDir).isDirectory());writeFileSync(i.outputPath('actual.txt'),'actual retry '+i.retry);"+("if(!i.retry)throw Error('actual retry');"if retry else'')+"}}]});")
 report=root/'actual.json';base=[str(a.executable),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(report),'--output-directory',str(chosen)]
 if mode=='api':
  wrapper=root/'launch.mjs';wrapper.write_text('import{runNativeSuite}from '+json.dumps((sdk/'native.js').as_uri())+';const result=await runNativeSuite('+json.dumps({'executablePath':str(a.executable),'suitePath':str(suite),'reportPath':str(report),'outputDirectory':str(chosen)})+');if(result.signal!==null)throw Error("unexpected actual native signal "+result.signal);process.exitCode=result.exitCode??1;');base=[a.node,str(wrapper)]
 if mode=='cli':base=[a.node,str(sdk/'native-runner-cli.js'),'--executable',str(a.executable),'--suite',str(suite),'--report',str(report),'--output-directory',str(chosen)]
 v=subprocess.run(base,capture_output=True,timeout=15);(root/'stdout.bin').write_bytes(v.stdout);(root/'stderr.bin').write_bytes(v.stderr);assert v.returncode==(1 if broken else 0)and not v.stderr,(name,v.returncode,v.stdout,v.stderr);r=json.loads(report.read_text());assert len(r['outputs'])==(2 if retry else 1) and len(r['attempts'])==(2 if retry else 1),r;assert not(root/'test-results').exists()
 for record in r['outputs']:
  assert pathlib.Path(record['proposedPath']).parent==chosen
  if not broken:
   actual=pathlib.Path(record['outputDir']);assert actual.parent==chosen and str(actual.stat().st_ino)==record['inode']and (actual/'actual.txt').read_text()in['actual retry 0','actual retry 1']
  else:assert record['status']=='failed'and 'outputDir'not in record and record['writerExitCode']!=0
 if retry:assert len(r['outputs'])==2 and len({o['outputDir']for o in r['outputs']})==2 and r['summary']['flaky']==1
 if broken:assert chosen.read_text()=='actual regular file must survive'and not r['attempts'][0]['expected']and r['attempts'][0]['status']=='failed'
 rows.append({'scope':name,'actualExitCode':v.returncode,'report':str(report)})
for mode in ['native','api','cli']:run('actual-selected-root-'+mode,mode)
run('actual-retry-under-selected-root',retry=True)
run('actual-selected-root-filesystem-error',broken=True)
root=out/'native-rejects-invalid-root-before-loading';root.mkdir();suite=root/'suite.mjs';marker=root/'forbidden-module-loaded';suite.write_text("import{writeFileSync}from'node:fs';writeFileSync("+json.dumps(str(marker))+",'must not load');export default{tests:[]};")
for index,path in enumerate(['relative-root','/'+'a'*7900,b'/tmp/native-invalid-utf8-\xff']):
 v=subprocess.run([str(a.executable),'--node',a.node,'--host-script',str(sdk/'native-runner-host.js'),'--suite',str(suite),'--report',str(root/'actual.json'),'--output-directory',path],capture_output=True,timeout=5);(root/f'stderr-{index}.bin').write_bytes(v.stderr);assert v.returncode==2 and b'outputDirectory'in v.stderr and not marker.exists()and not(root/'actual.json').exists(),(path,v.returncode,v.stderr)
rows.append({'scope':'native rejects relative/oversized/invalidUTF8 before real host load','actualExitCodes':[2,2,2]})
root=out/'sdk-rejects-invalid-root-before-launch';root.mkdir();wrapper=root/'launch.mjs';wrapper.write_text('import{runNativeSuite}from '+json.dumps((sdk/'native.js').as_uri())+";import{strict as assert}from'node:assert';for(const outputDirectory of['relative','/tmp/a\\0b','/tmp/\\ud800','/'+'a'.repeat(7900)])assert.throws(()=>runNativeSuite({executablePath:'/does-not-exist-native-launch',suitePath:'/actual-suite.mjs',reportPath:'/actual-report.json',outputDirectory}));");v=subprocess.run([a.node,str(wrapper)],capture_output=True,timeout=5);(root/'stdout.bin').write_bytes(v.stdout);(root/'stderr.bin').write_bytes(v.stderr);assert v.returncode==0 and not v.stderr,(v.returncode,v.stderr);rows.append({'scope':'SDK validates exact filesystem UTF8/NUL/bounds before native spawn','actualExitCode':0})
(receipt/'report.json').write_text(json.dumps({'passed':len(rows),'scope':'Actual public global outputDirectory option through native CLI, SDK and suitecut-test; actual directory/writer provenance and original native retry roots. Syntax validation makes no existence or permission claim. Per-project root configuration remains separate.','groups':rows},indent=2)+'\n');print('PASS',len(rows),'actual native outputDirectory option groups')
