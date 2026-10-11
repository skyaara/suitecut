"""Own the display/audio dependencies for one isolated shared-clock test."""
import fcntl,hashlib,json,os,pathlib,select,shlex,signal,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2];os.chdir(root)
out=pathlib.Path(os.environ['SYNC_DIRECTORY']).resolve();out.mkdir(parents=True,exist_ok=True)
state=out/'runtime';state.mkdir(exist_ok=True)
lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
cpus=sorted(os.sched_getaffinity(0))[:4];os.sched_setaffinity(0,cpus)
deps=root/'.suitecut/obs-deps/root';libs=[deps/'usr/lib/x86_64-linux-gnu',deps/'usr/lib/x86_64-linux-gnu/pulseaudio',deps/'usr/lib/pulse-17.0+dfsg1/modules']
env=dict(os.environ,LD_LIBRARY_PATH=':'.join(map(str,libs)),PULSE_SERVER='unix:'+str(state/'pulse.sock'))
for key in ['LD_PRELOAD','SUITECUT_AUDIO_TRACE_DIRECTORY']:env.pop(key,None)
if os.environ.get('SYNC_TRACE')=='1':env['SUITECUT_AUDIO_TRACE_DIRECTORY']=str(out)
width=int(os.environ.get('SYNC_WIDTH','1920'));height=int(os.environ.get('SYNC_HEIGHT','1080'))
children=[]
def interrupted(signum, frame):
 raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,interrupted)
try:
 display=subprocess.Popen([str(deps/'usr/bin/Xvfb'),'-displayfd','1','-screen','0',f'{width}x{height}x24','-nolisten','tcp','-ac'],env=env,stdout=subprocess.PIPE,stderr=(out/'xvfb.log').open('w'));children.append(display)
 if not select.select([display.stdout],[],[],10)[0]:raise RuntimeError('Display readiness timeout')
 number=display.stdout.readline().decode().strip();assert number.isdecimal();env['DISPLAY']=':'+number
 pulse=subprocess.Popen([str(deps/'usr/bin/pulseaudio'),'--daemonize=no','--exit-idle-time=-1','--use-pid-file=no','--dl-search-path='+str(deps/'usr/lib/pulse-17.0+dfsg1/modules'),'-n','--load=module-native-protocol-unix socket='+str(state/'pulse.sock')+' auth-anonymous=1','--load=module-null-sink sink_name=sharedclock rate=48000 channels=2'],env=env,stdout=(out/'pulse.log').open('w'),stderr=subprocess.STDOUT);children.append(pulse)
 time.sleep(1)
 if any(p.poll() is not None for p in children):raise RuntimeError('Display/audio runtime exited')
 env.update(XVFB_PID=str(display.pid),PULSE_PID=str(pulse.pid),BENCH_PROFILE=os.environ.get('SYNC_PROFILE','performance'))
 executable=os.environ.get('SYNC_EXECUTABLE',str(root/'.suitecut/native-build/suitecut-browser'))
 wrapper=state/'browser-wrapper';wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(executable)+' "$@" 2>>'+shlex.quote(str(out/'browser.log'))+'\n');wrapper.chmod(0o700);env['SUITECUT_NATIVE_EXECUTABLE']=str(wrapper)
 runtime={'cpus':cpus,'cpuQuota':pathlib.Path('/sys/fs/cgroup/cpu.max').read_text().strip(),'hostMemoryLimitBytes':int(pathlib.Path('/sys/fs/cgroup/memory.max').read_text()),'memoryGuardBytes':8*1024**3,'memoryGuardKind':'sampled summed-RSS guard, not kernel cap','displaySize':[width,height],'infrastructurePids':[display.pid,pulse.pid],'executable':executable,'audioTrace':env.get('SUITECUT_AUDIO_TRACE_DIRECTORY'),'sanitizers':False}
 (out/'runtime.json').write_text(json.dumps(runtime,indent=2)+'\n')
 runtime['executableSha256']=hashlib.sha256(pathlib.Path(executable).read_bytes()).hexdigest()
 (out/'runtime.json').write_text(json.dumps(runtime,indent=2)+'\n')
 controller=subprocess.Popen(['node','--expose-gc',str(root/'benchmarks/native-sync/shared-clock-capture.mjs')],env=env,cwd=root,start_new_session=True);children.append(controller)
 sys.exit(controller.wait(timeout=int(os.environ.get('SYNC_DURATION_MS','60000'))/1000+180))
finally:
 for child in reversed(children):
  if child is locals().get('controller'):
   # The browser/sampler belong to this isolated controller process group.
   # A timed-out or interrupted parent must not leave them in the next trial.
   try:os.killpg(child.pid,signal.SIGTERM)
   except ProcessLookupError:pass
   try:child.wait(5)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
   try:os.killpg(child.pid,signal.SIGKILL)
   except ProcessLookupError:pass
  elif child.poll() is None:
   child.terminate()
   try:child.wait(5)
   except subprocess.TimeoutExpired:child.kill();child.wait()
