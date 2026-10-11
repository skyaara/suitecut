"""Serialize live captures; never overlap captures, builds, decoders or fuzzers."""
import datetime,json,os,pathlib,signal,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[2];os.chdir(root)
out=pathlib.Path(sys.argv[1]).resolve();(out/'logs').mkdir(exist_ok=True)
cases=[('720p60-record',1280,720,60,'record',120000),('720p60-stream',1280,720,60,'stream',120000),('1080p30-record',1920,1080,30,'record',120000),('1080p30-stream',1920,1080,30,'stream',120000),('4k30-record',3840,2160,30,'record',120000),('4k30-stream',3840,2160,30,'stream',120000),('controller-stall-1080p30',1920,1080,30,'record',60000)]
selected=os.environ.get('SYNC_MATRIX_CASES')
if selected:
 requested=set(selected.split(','));assert requested<=set(c[0] for c in cases),'Unknown matrix case';cases=[c for c in cases if c[0] in requested]
manifest={'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'cases':[],'serial':True,'trace':False,'simultaneousRecordingStreaming':False}
def interrupted(signum, frame):
 raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,interrupted)
for name,width,height,fps,mode,duration in cases:
 env=dict(os.environ,SYNC_DIRECTORY=str(out/'runs'/name),SYNC_WIDTH=str(width),SYNC_HEIGHT=str(height),SYNC_FPS=str(fps),SYNC_MODE=mode,SYNC_DURATION_MS=str(duration),SYNC_PROFILE='performance',SYNC_TRACE='0',SYNC_CONTROLLER_STALL='1' if name.startswith('controller-stall') else '0')
 began=datetime.datetime.now(datetime.timezone.utc).isoformat();print('START',name,flush=True)
 with (out/'logs'/(name+'.log')).open('w') as log:
  completed=subprocess.Popen(['python','benchmarks/native-sync/shared-clock-runtime.py'],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:completed.wait()
  finally:
   if completed.poll() is None:
    completed.terminate()
    try:completed.wait(20)
    except subprocess.TimeoutExpired:completed.kill();completed.wait()
 manifest['cases'].append({'name':name,'width':width,'height':height,'fps':fps,'mode':mode,'durationMs':duration,'startedAt':began,'finishedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'exitCode':completed.returncode})
 (out/'matrix.json').write_text(json.dumps(manifest,indent=2)+'\n');print('FINISH',name,completed.returncode,flush=True)
manifest['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'matrix.json').write_text(json.dumps(manifest,indent=2)+'\n')
sys.exit(0 if all(c['exitCode']==0 for c in manifest['cases']) else 1)
