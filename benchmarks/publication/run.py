"""Serial balanced trials, with media analysis only between capture runs."""
import datetime,json,os,pathlib,shutil,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2];out=pathlib.Path(os.environ.get('PUBLICATION_DIRECTORY',root/'benchmarks/publication/2026-10-10')).resolve();out.mkdir(parents=True,exist_ok=True);(out/'logs').mkdir(exist_ok=True)
backends=['ts-cef','ts-external','obs-cef','obs-external','cpp-cef'];trials=int(os.environ.get('PUBLICATION_TRIALS','5'));duration=int(os.environ.get('PUBLICATION_TRIAL_MS','30000'));memory_duration=int(os.environ.get('PUBLICATION_MEMORY_MS','600000'));native_memory_duration=int(os.environ.get('PUBLICATION_NATIVE_MEMORY_MS','1800000'))
plan=[]
for trial in range(trials):
 order=backends[trial%5:]+backends[:trial%5]
 if trial%2:order=list(reversed(order))
 for mode in (['record','stream'] if trial%2==0 else ['stream','record']):
  for backend in order:plan.append({'id':f'{mode}-{backend}-{trial+1:02d}','backend':backend,'mode':mode,'profile':'performance','durationMs':duration,'trial':trial+1})
for backend in ['obs-external','ts-cef','obs-cef','ts-external','cpp-cef']:
 plan.append({'id':f'memory-{backend}','backend':backend,'mode':'stream','profile':'memory','durationMs':native_memory_duration if backend=='cpp-cef' else memory_duration,'trial':None})
(out/'schedule.json').write_text(json.dumps(plan,indent=2)+'\n');completed=[]
if (out/'runs.json').exists():completed=json.loads((out/'runs.json').read_text())
done_ids={x['id'] for x in completed};base=time.monotonic()
for item in plan:
 if item['id'] in done_ids:continue
 if shutil.disk_usage(root).free<600*1024**2:raise RuntimeError('Insufficient free disk; original evidence is preserved')
 directory=out/'runs'/item['id']
 if directory.exists():raise RuntimeError(f'Uncompleted run directory exists: {directory}; retain it before resuming')
 env=dict(os.environ,BENCH_DIRECTORY=str(directory),BENCH_BACKEND=item['backend'],BENCH_MODE=item['mode'],BENCH_PROFILE=item['profile'],BENCH_DURATION_MS=str(item['durationMs']))
 progress={'status':'running','run':item,'completedRuns':len(completed),'plannedRuns':len(plan),'startedAt':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 (out/'progress.json').write_text(json.dumps(progress,indent=2)+'\n');print(json.dumps({'starting':item['id'],'completed':len(completed),'total':len(plan)}),flush=True)
 with (out/'logs'/f"{item['id']}.log").open('w') as log:
  try:p=subprocess.run(['python3','benchmarks/publication/runtime.py'],env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=item['durationMs']/1000+200);returncode=p.returncode
  except subprocess.TimeoutExpired:returncode=124
 row=dict(item,returncode=returncode,elapsedMs=(time.monotonic()-base)*1000,directory=str(directory))
 if (directory/'result.json').exists():
  row['result']=json.loads((directory/'result.json').read_text())
  with (out/'logs'/f"{item['id']}-analysis.log").open('w') as log:
   analysis=subprocess.run(['python3','benchmarks/publication/analyze.py',str(directory)],cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=300)
  row['analysisReturncode']=analysis.returncode
  if (directory/'analysis.json').exists():row['analysis']=json.loads((directory/'analysis.json').read_text())
 row['workloadCompleted']=row.get('result',{}).get('status')=='completed; media validation pending'
 row['mediaValidated']=row.get('analysis',{}).get('valid',False)
 row['processSucceeded']=returncode==0
 completed.append(row);(out/'runs.json').write_text(json.dumps(completed,indent=2)+'\n')
 print(json.dumps({'finished':item['id'],'processSucceeded':row['processSucceeded'],'mediaValidated':row['mediaValidated']}),flush=True)
 if row['workloadCompleted'] and not row.get('analysis',{}).get('mediaDecoded',False):
  (out/'progress.json').write_text(json.dumps({'status':'stopped for investigation','run':item,'completedRuns':len(completed),'plannedRuns':len(plan)},indent=2)+'\n')
  raise RuntimeError(f"Media validation failed: {item['id']}; all results retained")
 time.sleep(1)
(out/'progress.json').write_text(json.dumps({'status':'completed','completedRuns':len(completed),'plannedRuns':len(plan),'finishedAt':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2)+'\n')
