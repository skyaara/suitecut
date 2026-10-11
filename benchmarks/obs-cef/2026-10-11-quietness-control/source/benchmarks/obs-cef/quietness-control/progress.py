import json,pathlib,sys
base=pathlib.Path(sys.argv[1]);rows=[]
for p in sorted((base/'runs').glob('*/result.json')):
 try:d=json.loads(p.read_text())
 except (OSError,ValueError):continue
 row={'case':p.parent.name,'status':d['status'],'activeSeconds':round(d.get('activeMs',d.get('observedActiveMs',0))/1000),'error':d.get('error')}
 samples=p.parent/'resources.jsonl'
 if samples.exists():
  with samples.open('rb') as f:
   f.seek(max(0,samples.stat().st_size-24000));lines=f.read().splitlines()
  for line in reversed(lines):
   try:s=json.loads(line);break
   except ValueError:continue
  else:s={}
  product=[x for x in s.get('processes',[]) if x['role'] not in ['display','audio-server','receiver']]
  row.update(latestCpuCores=round(sum(x['cpuCores'] or 0 for x in product),2),latestRssMiB=round(sum(x['rssBytes'] for x in product)/2**20,1))
 a=p.parent/'analysis.json'
 if a.exists():
  a=json.loads(a.read_text());row.update(harnessValid=a['harnessValid'],quiet=a.get('audio',{}).get('activeWindow'),analysisError=a.get('error'))
 rows.append(row)
print(json.dumps(rows,indent=2))
