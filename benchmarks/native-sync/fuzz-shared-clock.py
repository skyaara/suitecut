"""Run bounded sanitizer-backed fuzz campaigns after all live captures."""
import datetime,hashlib,json,os,pathlib,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2];os.chdir(root)
out=pathlib.Path(sys.argv[1]).resolve();seconds=int(sys.argv[2]) if len(sys.argv)>2 else 600
manifest={'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),'secondsPerTarget':seconds,'targets':[],'scope':'Native bounds/clock helpers and real FFmpeg output queue/lifecycle paths. Shared CEF/FFmpeg dependencies are not ASan-instrumented.'}
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for target in ['bounds','media']:
 exe=root/'.suitecut/shared-clock-asan'/('suitecut-fuzz-'+target);corpus=out/'fuzz'/(target+'-corpus');artifacts=out/'fuzz'/(target+'-artifacts')
 args=[str(exe),str(corpus),f'-max_total_time={seconds}','-max_len=512','-timeout=5','-rss_limit_mb=1024','-malloc_limit_mb=128','-detect_leaks=1','-print_final_stats=1','-artifact_prefix='+str(artifacts)+'/']
 print('START',target,flush=True);started=time.monotonic()
 with (out/'logs'/('fuzz-'+target+'.log')).open('w') as log:completed=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT)
 manifest['targets'].append({'name':target,'args':args,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'exitCode':completed.returncode,'wallSeconds':time.monotonic()-started,'corpusFiles':len(list(corpus.iterdir())),'artifacts':[p.name for p in artifacts.iterdir()]})
 (out/'fuzz'/'results.json').write_text(json.dumps(manifest,indent=2)+'\n');print('FINISH',target,completed.returncode,flush=True)
 if completed.returncode:break
manifest['finishedAt']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'fuzz'/'results.json').write_text(json.dumps(manifest,indent=2)+'\n')
sys.exit(0 if len(manifest['targets'])==2 and all(c['exitCode']==0 for c in manifest['targets']) else 1)
