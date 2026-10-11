import json,pathlib
p=pathlib.Path(__file__).parent/'2026-10-10';d=json.loads((p/'progress.json').read_text());runs=json.loads((p/'runs.json').read_text()) if (p/'runs.json').exists() else []
run=d.get('run',{});case=p/'runs'/run.get('id','none')/'result.json';current=json.loads(case.read_text()) if case.exists() else {}
latest=runs[-1] if runs else {};a=latest.get('analysis',{});res=a.get('resources',{});motion=a.get('motion',{});audio=a.get('audio',{}).get('silence',{})
sample_path=case.parent/'resources.jsonl';sample={}
if sample_path.exists():
 with sample_path.open('rb') as f:
  f.seek(max(0,sample_path.stat().st_size-20000));lines=f.read().splitlines()
 for line in reversed(lines):
  try:sample=json.loads(line);break
  except ValueError:pass
product=[x for x in sample.get('processes',[]) if x['role'] not in ['receiver','display','audio-server']]
native={};status_path=case.parent/'status.jsonl'
if run.get('backend')=='cpp-cef' and status_path.exists():
 with status_path.open('rb') as f:f.seek(max(0,status_path.stat().st_size-5000));lines=f.read().splitlines()
 for line in reversed(lines):
  try:native=json.loads(line).get('output',{});break
  except ValueError:pass
print(json.dumps({'status':d['status'],'completed':d['completedRuns'],'total':d['plannedRuns'],'current':run.get('id'),'activeSeconds':round(current.get('observedActiveMs',0)/1000),'latest':latest.get('id'),'latestCPU':res.get('cpuMeanCores'),'latestFPS':motion.get('distinctFps'),'latestAudioSilenceMs':audio.get('interiorTotalMs'),'latestExit':latest.get('returncode'),'latestValidated':latest.get('mediaValidated'),'currentRSSMiB':round(sum(x['rssBytes'] for x in product)/1024**2),'currentCPUCores':round(sum(x['cpuCores'] or 0 for x in product),2),'nativeMissingAudioMs':round(native.get('missingAudioSamples',0)/48),'nativeMaximumEncodeMs':native.get('maximumEncodeMs')}))
