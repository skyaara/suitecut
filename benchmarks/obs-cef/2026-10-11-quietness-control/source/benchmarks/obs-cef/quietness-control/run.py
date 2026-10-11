"""Fresh serial OBS embedded-CEF controls; retain every attempted capture."""
import datetime,json,os,pathlib,signal,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[3];os.chdir(root)
out=pathlib.Path(sys.argv[1]).resolve();(out/'logs').mkdir(parents=True,exist_ok=True)
cases=[('1080p30-record',1920,1080,30,'record',120000),('1080p30-stream',1920,1080,30,'stream',120000),('720p60-record',1280,720,60,'record',120000),('720p60-stream',1280,720,60,'stream',120000),('4k30-record',3840,2160,30,'record',120000),('4k30-stream',3840,2160,30,'stream',120000),('soak-1080p30-stream',1920,1080,30,'stream',1800000)]
selected=os.environ.get('OBS_CONTROL_CASES')
if selected:cases=[c for c in cases if c[0] in selected.split(',')]
manifest={'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cases':[],'serial':True,'browserCapture':'OBS embedded CEF Browser Source; reroute_audio=true through OBS mixer','trace':False,'simultaneousRecordingStreaming':False}
def interrupted(signum,frame):raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,interrupted)
def host():
 return {name:(pathlib.Path('/sys/fs/cgroup')/name).read_text() for name in ['memory.current','memory.events','cpu.stat']}
for name,width,height,fps,mode,duration in cases:
 # Advise clean file cache between runs only, never modify or delete evidence.
 advised=0
 for base in [root/'.suitecut',root/'benchmarks']:
  for path in base.rglob('*'):
   try:
    if path.is_file() and not path.is_symlink() and path.stat().st_size>=8*1024**2:
     with path.open('rb') as f:os.posix_fadvise(f.fileno(),0,0,os.POSIX_FADV_DONTNEED)
     advised+=path.stat().st_size
   except OSError:pass
 before=host();env=dict(os.environ,BENCH_DIRECTORY=str(out/'runs'/name),BENCH_BACKEND='obs-cef',BENCH_WIDTH=str(width),BENCH_HEIGHT=str(height),BENCH_FPS=str(fps),BENCH_MODE=mode,BENCH_DURATION_MS=str(duration),BENCH_PROFILE='memory' if name.startswith('soak') else 'performance')
 began=datetime.datetime.now(datetime.timezone.utc).isoformat();print('START',name,flush=True)
 with (out/'logs'/(name+'.log')).open('w') as log:
  child=subprocess.Popen(['python3','benchmarks/obs-cef/quietness-control/runtime.py'],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:child.wait()
  finally:
   if child.poll() is None:
    child.terminate()
    try:child.wait(20)
    except subprocess.TimeoutExpired:child.kill();child.wait()
 manifest['cases'].append({'name':name,'width':width,'height':height,'fps':fps,'mode':mode,'durationMs':duration,'startedAt':began,'finishedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'exitCode':child.returncode,'hostBefore':before,'hostAfter':host(),'cleanCacheBytesAdvisedBeforeCapture':advised})
 (out/'matrix.json').write_text(json.dumps(manifest,indent=2)+'\n');print('FINISH',name,child.returncode,flush=True)
manifest['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'matrix.json').write_text(json.dumps(manifest,indent=2)+'\n')
sys.exit(0 if all(c['exitCode']==0 for c in manifest['cases']) else 1)
