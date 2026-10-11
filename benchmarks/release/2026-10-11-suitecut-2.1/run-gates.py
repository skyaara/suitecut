import os,pathlib,subprocess,json,time,fcntl
root=pathlib.Path('/workspace/suitecut');out=root/'benchmarks/release/2026-10-11-suitecut-2.1';os.chdir(root)
os.sched_setaffinity(0,{0,1,2,3});lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX)
results=[]
def run(name,args,env=None):
 print('START',name,flush=True);started=time.monotonic()
 with (out/'logs'/f'{name}.log').open('w') as log:
  p=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT)
 results.append({'name':name,'args':args,'exitCode':p.returncode,'wallSeconds':time.monotonic()-started})
 (out/'release-checks.json').write_text(json.dumps(results,indent=2)+'\n');print('FINISH',name,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
if not (out/'fuzz/results.json').exists():raise SystemExit('Native fuzz evidence missing')
fuzz=json.loads((out/'fuzz/results.json').read_text())
if len(fuzz['targets'])!=2 or any(t['exitCode'] for t in fuzz['targets']):raise SystemExit('Native fuzz failed')
tools=root/'.suitecut/memory-tools/root';ffmpeg=root/'.suitecut/memory-tools/ffmpeg-tsan'
env=dict(os.environ,LD_LIBRARY_PATH=str(tools/'usr/lib/x86_64-linux-gnu')+':'+str(ffmpeg/'lib')+':'+os.environ.get('LD_LIBRARY_PATH',''),TSAN_OPTIONS='halt_on_error=1')
build='/tmp/suitecut-release-tsan'
run('configure-tsan',['cmake','-S','native/browser','-B',build,'-DCEF_ROOT='+str(root/'.suitecut/cef-codecs-sdk'),'-DLIBYUV_ROOT='+str(root/'.suitecut/libyuv'),'-DSUITECUT_CEF_PIN_FILE=cef-build-linux-codecs.json','-DSUITECUT_FFMPEG_ROOT='+str(ffmpeg),'-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DSUITECUT_SANITIZERS=thread','-DCMAKE_C_COMPILER='+str(tools/'usr/bin/clang-19'),'-DCMAKE_CXX_COMPILER='+str(tools/'usr/bin/clang++-19')],env)
targets=['suitecut-media-test','suitecut-audio-startup-test','suitecut-codec-test','suitecut-audio-clock-test','suitecut-pixel-test','suitecut-memory-test','suitecut-sync-test','suitecut-scaler-failure-test']
run('build-tsan',['cmake','--build',build,'--parallel','2','--target',*targets],env)
run('ctest-tsan',['ctest','--test-dir',build,'--output-on-failure','-j','1'],env)
run('frozen-lock',['pnpm','install','--lockfile-only','--ignore-scripts','--offline','--frozen-lockfile'])
run('prepublish',['pnpm','prepublishOnly'])
run('pack-release',['pnpm','pack:release'])
lock.close()
env=dict(os.environ,SUITECUT_RUNTIME_EXECUTABLE=str(root/'.suitecut/native-build/suitecut-browser'),MEMORY_DIRECTORY=str(out/'installed-native'))
run('installed-native',['python3','benchmarks/native-memory/runtime.py','scripts/verify-beta-package.mjs','--release'],env)
env['MEMORY_DIRECTORY']=str(out/'engine-e2e')
run('engine-e2e',['python3','benchmarks/native-memory/runtime.py','playwright'],env)
