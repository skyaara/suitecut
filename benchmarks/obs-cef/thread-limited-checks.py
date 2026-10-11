"""Repeat comparisons with OBS x264 limited to the host CPU quota."""
import json,os,time,signal,subprocess,shutil,fcntl,atexit
from pathlib import Path
from datetime import datetime,timezone
import psutil
root=Path(__file__).resolve().parents[2];os.chdir(root)
out=root/'benchmarks/obs-cef/2026-10-09-optimization'
lock=(root/'.suitecut/thread-control.lock').open('w')
fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
state={'startedAt':datetime.now(timezone.utc).isoformat(),'stages':[], 'exclusiveCoordinatorLock':True}
def save(): (out/'thread-limited-checks-driver.json').write_text(json.dumps(state,indent=2))
save()
while True:
 try:previous=json.loads((out/'final-checks-driver.json').read_text())
 except (json.JSONDecodeError,FileNotFoundError):time.sleep(.1);continue
 if previous.get('status')=='passed':break
 if previous.get('status') in ['failed','blocked by failed long-run media validation']:
  state['status']='blocked by failed final checks';save();raise SystemExit(1)
 time.sleep(5)
# Stop only our original detached display/audio/OBS runtime.
old_pids={'driver':42847,'xvfb':42850,'pulse':42851,'obs':42864}
def running(pid):
 try:return psutil.Process(pid).status()!=psutil.STATUS_ZOMBIE
 except psutil.NoSuchProcess:return False
def stop(pid,sig):
 if running(pid):os.kill(pid,sig)
stop(old_pids['pulse'],signal.SIGTERM);time.sleep(1)
stop(old_pids['xvfb'],signal.SIGTERM)
for _ in range(50):
 if not any(running(pid) for pid in old_pids.values()):break
 time.sleep(.2)
for pid in old_pids.values():
 if running(pid):stop(pid,signal.SIGTERM)
time.sleep(1)
state['originalRuntimeAfterShutdown']={name:{'pid':pid,'running':running(pid)} for name,pid in old_pids.items()};save()
assert not any(row['running'] for row in state['originalRuntimeAfterShutdown'].values())
profile=root/'.suitecut/obs-profile/obs-studio/basic/profiles/Benchmark';before={};after={}
for name in ['recordEncoder.json','streamEncoder.json']:
 before[name]=json.loads((profile/name).read_text());after[name]={**before[name],'x264opts':'threads=4'};(profile/name).write_text(json.dumps(after[name]))
(out/'checks/obs-thread-tuning.json').write_text(json.dumps({'before':before,'after':after,'cpuQuotaEquivalents':4,'scope':'x264 opts only; codec/preset/rate control unchanged'},indent=2))
env={**os.environ,'DISPLAY':':99','PULSE_SERVER':'unix:'+str(root/'.suitecut/runtime/pulse.sock'),'SUITECUT_NATIVE_EXECUTABLE':str(root/'.suitecut/native-build/suitecut-browser'),'PLAYWRIGHT_BROWSERS_PATH':str(root/'.suitecut/playwright-browsers'),'RUNTIME_LOG_DIR':str(out/'logs/thread-limited-runtime')}
with (out/'logs/thread-limited-runtime-driver.log').open('w') as log:
 runtime=subprocess.Popen(['bash','benchmarks/obs-cef/start-runtime.sh'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
def cleanup_runtime():
 if runtime.poll() is None:
  try:children=psutil.Process(runtime.pid).children()
  except psutil.NoSuchProcess:children=[]
  for child in children:
   if child.name()=='obs':stop(child.pid,signal.SIGTERM)
  try:runtime.wait(timeout=10)
  except subprocess.TimeoutExpired:
   for child in children:stop(child.pid,signal.SIGTERM)
   runtime.terminate();runtime.wait(timeout=10)
 for name,value in before.items():(profile/name).write_text(json.dumps(value))
 state['runtimeStopped']=runtime.poll() is not None
 state['runtimeExitCode']=runtime.returncode
 state['profileEncoderSettingsRestored']=all(json.loads((profile/name).read_text())==value for name,value in before.items())
 save()
atexit.register(cleanup_runtime)
ready=False
for _ in range(40):
 time.sleep(.5)
 if runtime.poll() is not None:break
 probe=subprocess.run(['node','--input-type=module','-e',"import {ObsClient} from './benchmarks/obs-cef/obs-client.mjs';const c=new ObsClient();await c.connect();await c.request('GetStats');c.close()"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=10)
 if probe.returncode==0:ready=True;break
assert ready,'Thread-limited OBS runtime failed to start'
children=psutil.Process(runtime.pid).children();pids={p.name():p.pid for p in children};env.update(OBS_PID=str(pids['obs']),XVFB_PID=str(pids['Xvfb']))
state['runtimePids']={'driver':runtime.pid,**pids};save()
(out/'thread-limited-runtime-driver.json').write_text(json.dumps({'pid':runtime.pid,'children':pids,'detachedProcessGroup':True},indent=2))
def run(label,args,variables):
 row={'label':label,'command':args,'environmentOverrides':variables,'startedAt':datetime.now(timezone.utc).isoformat(),'status':'running'};state['stages'].append(row);save()
 with (out/'logs'/f'{label}.log').open('w') as log: result=subprocess.run(args,env={**env,**variables},stdout=log,stderr=subprocess.STDOUT)
 valid=result.returncode==0
 if valid and 'BENCHMARK_OUTPUT' in (variables or {}):
  rows=json.loads((Path(variables['BENCHMARK_OUTPUT'])/'trials.json').read_text());valid=len(rows)==24 and all(r['status']=='passed' for r in rows)
 if valid and 'FEATURE_OUTPUT' in (variables or {}):
  rows=json.loads((Path(variables['FEATURE_OUTPUT'])/'results.json').read_text());valid=bool(rows) and all(r.get('completedAt') and not r.get('error') and all(c['status']=='passed' for c in r['checks']) for r in rows)
 row.update(exitCode=result.returncode,status='passed' if valid else 'failed',completedAt=datetime.now(timezone.utc).isoformat());save()
 if not valid:state['status']='failed';save();raise SystemExit(1)
 if result.returncode:state['status']='failed';save();raise SystemExit(result.returncode)
run('comparison-obs-thread-limited',['node','benchmarks/obs-cef/run-optimized.mjs'],{'BENCHMARK_OUTPUT':str(out/'comparison-obs-thread-limited'),'BENCHMARK_MEDIA':str(root/'.suitecut/obs-cef-optimization/comparison-obs-thread-limited'),'BENCHMARK_CASE':'720p60-canvas,1080p60-canvas,4k30-canvas,4k60-scroll','BENCHMARK_TRIALS':'3'})
for trial in range(1,4):
 label=f'features-stream-thread-limited-{trial}'
 run(label,['node','benchmarks/obs-cef/features.mjs'],{'FEATURE_OUTPUT':str(out/label),'FEATURE_MEDIA':str(root/'.suitecut/obs-cef-optimization'/label),'FEATURE_MODE':'stream','FEATURE_BACKENDS':'obs,native'})
state.update(status='passed',completedAt=datetime.now(timezone.utc).isoformat());save()
cleanup_runtime()
atexit.unregister(cleanup_runtime)
