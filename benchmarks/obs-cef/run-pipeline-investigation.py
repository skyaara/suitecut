"""Own a display/audio runtime, preserve diagnostics and execute one bounded test."""
import fcntl,hashlib,json,os,shlex,subprocess,time
from datetime import datetime,timezone
from pathlib import Path
import psutil
root=Path(__file__).resolve().parents[2];os.chdir(root)
label=os.environ.get('INVESTIGATE_LABEL','clean-30-minute');duration=os.environ.get('INVESTIGATE_DURATION_MS','1800000')
out=root/'benchmarks/obs-cef/2026-10-09-dropout-investigation'/label;out.mkdir(parents=True,exist_ok=False);(out/'logs').mkdir()
lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
for p in psutil.process_iter(['name','status']):
 if p.info['name'] in ['Xvfb','obs','pulseaudio'] and p.info['status']!=psutil.STATUS_ZOMBIE:raise RuntimeError('Another display/audio/OBS runtime is active')
deps=root/'.suitecut/obs-deps/root';runtime=root/'.suitecut/runtime';runtime.mkdir(exist_ok=True)
wrapper=out/'native-wrapper.sh';wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(str(root/'.suitecut/native-build/suitecut-browser'))+' --suitecut-diagnostics "$@" 2>>'+shlex.quote(str(out/'logs/native.log'))+'\n');wrapper.chmod(0o755)
env={**os.environ,'DISPLAY':':99','PULSE_SERVER':'unix:'+str(runtime/'pulse.sock'),'LD_LIBRARY_PATH':':'.join(str(deps/x) for x in ['usr/lib/x86_64-linux-gnu','usr/lib/x86_64-linux-gnu/pulseaudio','usr/lib/pulse-17.0+dfsg1/modules']),'SUITECUT_NATIVE_EXECUTABLE':str(wrapper),'SOAK_LABEL':label,'SOAK_DURATION_MS':duration,'SOAK_FPS':'30','PROFILE_PRIVATE_EVERY':'0','INVESTIGATE_OUTPUT':str(out)}
state={'startedAt':datetime.now(timezone.utc).isoformat(),'label':label,'durationMs':int(duration),'status':'starting','sourceHashes':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'native/browser/browser.cc',*sorted((root/'src').glob('native*.ts')),root/'src/live-stream.ts',root/'src/live-audio.ts',root/'benchmarks/obs-cef/investigate-soak.mjs',root/'benchmarks/obs-cef/trace-stdio.mjs',root/'benchmarks/obs-cef/profile-pipeline.py',root/'benchmarks/obs-cef/fixture.html']},'binarySha256':hashlib.sha256((root/'.suitecut/native-build/suitecut-browser').read_bytes()).hexdigest()}
def save():(out/'driver.json').write_text(json.dumps(state,indent=2))
save();processes=[];logs=[]
try:
 for name,args in [('xvfb',[str(deps/'usr/bin/Xvfb'),':99','-screen','0','3840x2300x24','-nolisten','tcp','-ac']),('pulse',[str(deps/'usr/bin/pulseaudio'),'--daemonize=no','--exit-idle-time=-1','--use-pid-file=no','--dl-search-path='+str(deps/'usr/lib/pulse-17.0+dfsg1/modules'),'-n','--load=module-native-protocol-unix socket='+str(runtime/'pulse.sock')+' auth-anonymous=1','--load=module-null-sink sink_name=benchmark rate=48000 channels=2'])]:
  log=(out/'logs'/f'{name}.log').open('w');logs.append(log);p=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT);processes.append(p)
 for _ in range(40):
  check=subprocess.run([str(deps/'usr/bin/xdotool'),'getdisplaygeometry'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  if check.returncode==0 and (runtime/'pulse.sock').exists():break
  if any(p.poll() is not None for p in processes):raise RuntimeError('Runtime exited during startup')
  time.sleep(.25)
 else:raise RuntimeError('Runtime not ready')
 state['runtimePids']=[p.pid for p in processes];state['status']='running';save()
 with (out/'logs/workload.log').open('w') as log:
  workload=subprocess.Popen(['node','--expose-gc','--trace-gc-nvp','--import','./benchmarks/obs-cef/trace-stdio.mjs','benchmarks/obs-cef/investigate-soak.mjs'],env=env,stdout=log,stderr=subprocess.STDOUT)
  state['workloadPid']=workload.pid;save();state['workloadExitCode']=workload.wait()
 state['status']='completed; media analysis pending';save()
except BaseException as error:
 state.update(status='failed',error=repr(error));save();raise
finally:
 for p in processes:
  if p.poll() is None:p.terminate()
 for p in processes:
  try:p.wait(timeout=10)
  except subprocess.TimeoutExpired:p.kill();p.wait()
 for log in logs:log.close()
 state.update(runtimeStopped=True,completedAt=datetime.now(timezone.utc).isoformat());save()
