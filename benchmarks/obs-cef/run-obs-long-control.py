"""Own one isolated display/audio/OBS runtime and run the matched long workload."""
import fcntl,hashlib,json,os,signal,subprocess,time
from datetime import datetime,timezone
from pathlib import Path
import psutil
root=Path(__file__).resolve().parents[2];os.chdir(root)
out=root/'benchmarks/obs-cef/2026-10-09-obs-long-control';state={'startedAt':datetime.now(timezone.utc).isoformat(),'exclusiveRuntimeLock':True,'status':'starting'}
lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def save():(out/'driver.json').write_text(json.dumps(state,indent=2))
for proc in psutil.process_iter(['name','status']):
 if proc.info['name'] in ['Xvfb','obs','pulseaudio'] and proc.info['status']!=psutil.STATUS_ZOMBIE:raise RuntimeError('Another display/OBS/audio runtime is active')
state['sourceHashes']={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['benchmarks/obs-cef/obs-long-soak.mjs','benchmarks/obs-cef/fixture.html','benchmarks/obs-cef/soak.mjs','benchmarks/obs-cef/profile-obs-long.py','benchmarks/obs-cef/start-runtime.sh']};save()
env={**os.environ,'DISPLAY':':99','PULSE_SERVER':'unix:'+str(root/'.suitecut/runtime/pulse.sock'),'PLAYWRIGHT_BROWSERS_PATH':str(root/'.suitecut/playwright-browsers'),'OBS_CONFIG_ROOT':str(root/'.suitecut/obs-long-control/2026-10-09/profile'),'RUNTIME_LOG_DIR':str(out/'logs/runtime'),'OBS_LONG_OUTPUT':str(out),'OBS_LONG_MEDIA':str(root/'.suitecut/obs-long-control/2026-10-09/media'),'OBS_LONG_DURATION_MS':'1800000'}
with (out/'logs/runtime-driver.log').open('w') as log:runtime=subprocess.Popen(['bash','benchmarks/obs-cef/start-runtime.sh'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
state['runtimeDriverPid']=runtime.pid;save()
try:
 ready=False
 for _ in range(40):
  time.sleep(.5)
  if runtime.poll() is not None:break
  try:
   probe=subprocess.run(['node','--input-type=module','-e',"import {ObsClient} from './benchmarks/obs-cef/obs-client.mjs';const c=new ObsClient();await c.connect();await c.request('GetStats');c.close()"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=5)
  except subprocess.TimeoutExpired:continue
  if probe.returncode==0:ready=True;break
 assert ready,'OBS runtime did not become ready'
 pids={p.name():p.pid for p in psutil.Process(runtime.pid).children()};assert all(n in pids for n in ['obs','Xvfb','pulseaudio'])
 env.update(OBS_PID=str(pids['obs']),XVFB_PID=str(pids['Xvfb']),PULSE_PID=str(pids['pulseaudio']))
 state['runtimePids']=pids;state['status']='running';save()
 with (out/'logs/workload.log').open('w') as log:
  command=subprocess.Popen(['node','--expose-gc','benchmarks/obs-cef/obs-long-soak.mjs'],env=env,stdout=log,stderr=subprocess.STDOUT)
  state['workloadPid']=command.pid;save();code=command.wait()
 state['workloadExitCode']=code;state['workloadResult']=json.loads((out/'result.json').read_text())['status'];state['status']='workload completed; validation pending' if code==0 else 'failed';save()
except BaseException as error:
 state['status']='failed';state['error']=repr(error);save();raise
finally:
 if runtime.poll() is None:
  children=psutil.Process(runtime.pid).children()
  for proc in children:
   if proc.name()=='obs':
    try:proc.terminate()
    except psutil.NoSuchProcess:pass
  try:runtime.wait(timeout=15)
  except subprocess.TimeoutExpired:
   for proc in children:
    try:proc.terminate()
    except psutil.NoSuchProcess:pass
   runtime.terminate();runtime.wait(timeout=10)
 state['runtimeExitCode']=runtime.returncode;state['runtimeStopped']=True;state['completedAt']=datetime.now(timezone.utc).isoformat();save()
