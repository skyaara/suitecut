import os,pathlib,subprocess,json,time,fcntl,datetime,hashlib
root=pathlib.Path('/workspace/suitecut');out=root/'benchmarks/release/2026-10-11-suitecut-2.1'
os.chdir(root);lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
os.sched_setaffinity(0,{0,1,2,3})
env=dict(os.environ,LD_LIBRARY_PATH=str(root/'.suitecut/memory-tools/root/usr/lib/x86_64-linux-gnu')+':'+os.environ.get('LD_LIBRARY_PATH',''),ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
results=[]
def run(name,args):
 print('START',name,flush=True);started=time.monotonic()
 with (out/'logs'/f'{name}.log').open('w') as log:
  p=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT)
 results.append({'name':name,'args':args,'exitCode':p.returncode,'wallSeconds':time.monotonic()-started})
 (out/'native-checks.json').write_text(json.dumps(results,indent=2)+'\n');print('FINISH',name,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
targets=['suitecut-media-test','suitecut-audio-startup-test','suitecut-codec-test','suitecut-audio-clock-test','suitecut-pixel-test','suitecut-memory-test','suitecut-sync-test','suitecut-scaler-failure-test']
run('build-asan',['cmake','--build','.suitecut/shared-clock-asan','--parallel','2','--target',*targets,'suitecut-fuzz-bounds','suitecut-fuzz-media'])
run('ctest-asan',['ctest','--test-dir','.suitecut/shared-clock-asan','--output-on-failure','-j','1'])
run('build-release',['cmake','--build','.suitecut/native-build','--parallel','2','--target',*targets])
run('ctest-release',['ctest','--test-dir','.suitecut/native-build','--output-on-failure','-j','1'])
run('fuzz',['python3','benchmarks/native-sync/fuzz-shared-clock.py',str(out),'600'])
