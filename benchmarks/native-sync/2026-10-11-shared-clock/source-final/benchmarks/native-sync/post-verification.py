"""Decode and report only after all final capture/fuzz phases have finished."""
import datetime,hashlib,json,pathlib,shutil,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2];out=pathlib.Path(sys.argv[1]).resolve();deadline=time.monotonic()+3*3600
while True:
 state=json.loads((out/'final-verification.json').read_text())
 if any(p['status']=='failed' for p in state['phases']):raise SystemExit('Final verification phase failed; inspect it before analyzing.')
 if 'finishedAt' in state:break
 if time.monotonic()>deadline:raise SystemExit('Timed out waiting for final verification.')
 time.sleep(5)
manifest={'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'serial':True,'cases':[],'analyzerSha256':hashlib.sha256((root/'benchmarks/native-sync/analyze-shared-clock.py').read_bytes()).hexdigest()}
for result in sorted((out/'runs').glob('*/result.json')):
 directory=result.parent;analysis=directory/'analysis.json'
 if analysis.exists() and 'harnessValid' not in json.loads(analysis.read_text()):
  shutil.copyfile(analysis,directory/'trace-diagnostic-before-analysis.json')
 print('DECODE',directory.name,flush=True);began=time.monotonic()
 with (out/'logs'/('decode-'+directory.name+'.log')).open('w') as log:
  done=subprocess.run(['python',str(root/'benchmarks/native-sync/analyze-shared-clock.py'),str(directory)],cwd=root,stdout=log,stderr=subprocess.STDOUT)
 manifest['cases'].append({'case':directory.name,'exitCode':done.returncode,'seconds':time.monotonic()-began});(out/'decode-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
manifest['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'decode-verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
subprocess.run(['python',str(root/'benchmarks/native-sync/report-shared-clock.py'),str(out)],cwd=root,check=True)
print('REPORT COMPLETE',flush=True)
