"""Serial final matrix, sanitizer fuzzing and sustained memory capture."""
import datetime,json,os,pathlib,signal,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2];os.chdir(root)
out=pathlib.Path(sys.argv[1]).resolve()
phases=[('resolution-matrix',['python','benchmarks/native-sync/shared-clock-matrix.py',str(out)],{}),('sanitizer-fuzz',['python','benchmarks/native-sync/fuzz-shared-clock.py',str(out),'600'],{}),('memory-soak',['python','benchmarks/native-sync/shared-clock-runtime.py'],{'SYNC_DIRECTORY':str(out/'runs/soak-1080p30-stream'),'SYNC_DURATION_MS':'1800000','SYNC_MODE':'stream','SYNC_PROFILE':'memory','SYNC_WIDTH':'1920','SYNC_HEIGHT':'1080','SYNC_FPS':'30','SYNC_TRACE':'0'})]
state={'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'phases':[],'serial':True}
for name,args,extra in phases:
 print('START',name,flush=True);item={'name':name,'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'running'};state['phases'].append(item);(out/'final-verification.json').write_text(json.dumps(state,indent=2)+'\n')
 with (out/'logs'/('final-'+name+'.log')).open('w') as log:
  completed=subprocess.run(args,env=dict(os.environ,**extra),stdout=log,stderr=subprocess.STDOUT)
 item.update(status='completed' if completed.returncode==0 else 'failed',exitCode=completed.returncode,finishedAt=datetime.datetime.now(datetime.timezone.utc).isoformat());(out/'final-verification.json').write_text(json.dumps(state,indent=2)+'\n');print('FINISH',name,completed.returncode,flush=True)
 if completed.returncode:sys.exit(completed.returncode)
state['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'final-verification.json').write_text(json.dumps(state,indent=2)+'\n')
