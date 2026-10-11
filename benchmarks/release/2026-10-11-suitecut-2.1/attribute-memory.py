import pathlib,json,collections,statistics
root=pathlib.Path(__file__).resolve().parents[3];out=pathlib.Path(__file__).resolve().parent
run=root/'benchmarks/native-sync/2026-10-11-shared-clock/runs/soak-1080p30-stream'
result=json.loads((run/'result.json').read_text());start=result['activeStartMonotonic'];end=start+1800
points=collections.defaultdict(lambda:collections.defaultdict(list));roles={}
for line in (run/'resources.jsonl').open():
 sample=json.loads(line);time=(sample['monotonic']-start)/60
 if sample['monotonic']<start+120 or sample['monotonic']>end:continue
 for process in sample['processes']:
  if process['role'] in ['receiver','display','audio-server']:continue
  pid=process['pid'];roles[pid]=process['role']
  for metric in ['rssBytes','pssBytes','ussBytes']:
   if metric in process and process[metric] is not None:points[pid][metric].append((time,process[metric]/1024**2))
def slope(rows):
 x=statistics.mean(r[0] for r in rows);y=statistics.mean(r[1] for r in rows)
 denom=sum((r[0]-x)**2 for r in rows)
 return sum((r[0]-x)*(r[1]-y) for r in rows)/denom if denom else None
rows=[]
for pid,metrics in points.items():
 row={'pid':pid,'role':roles[pid],'metrics':{}}
 for key,data in metrics.items():
  row['metrics'][key]={'samples':len(data),'slopeMiBPerMinute':slope(data),'firstMiB':data[0][1],'lastMiB':data[-1][1],'peakMiB':max(d[1] for d in data)}
 rows.append(row)
rows.sort(key=lambda r:r['metrics']['rssBytes']['slopeMiBPerMinute'],reverse=True)
(out/'earlier-memory-attribution.json').write_text(json.dumps({'source':str(run.relative_to(root)),'method':'Per-PID OLS over the earlier sealed active run after 120 seconds; infrastructure excluded. Browser role includes CEF main and other CEF subprocesses. This is descriptive, not leak allocation attribution.','rows':rows},indent=2)+'\n')
for row in rows:
 print(row['pid'],row['role'],{k:round(v['slopeMiBPerMinute'],4) for k,v in row['metrics'].items()})
