import json,pathlib,sys
p=pathlib.Path(sys.argv[1])/'runs/soak-1080p30-stream';r=json.loads((p/'result.json').read_text());elapsed=r.get('activeMs',r.get('observedActiveMs',0))/1000
print(json.dumps({'status':r['status'],'activeMinutes':round(elapsed/60,2),'error':r.get('error')}))
samples=p/'resources.jsonl'
if samples.exists():
 with samples.open('rb') as f:f.seek(max(0,samples.stat().st_size-24000));lines=f.read().splitlines()
 for raw in reversed(lines):
  try:s=json.loads(raw);break
  except ValueError:continue
 prods=[x for x in s['processes'] if x['role'] not in ['display','audio-server','receiver']]
 print(json.dumps({'latestCpuCores':round(sum(x['cpuCores'] or 0 for x in prods),2),'latestRssMiB':round(sum(x['rssBytes'] for x in prods)/2**20,1),'memoryGuardExceeded':s['memoryGuardExceeded'],'samplerMs':round(s['sampleDurationMs'],1)}))
 a=p/'analysis.json'
 if a.exists():
  a=json.loads(a.read_text());print(json.dumps({'harnessValid':a['harnessValid'],'audio':a.get('audio',{}).get('activeWindow'),'resources':a.get('resources'),'error':a.get('error')}))
