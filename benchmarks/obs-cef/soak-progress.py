"""Compact live checkpoint; does not infer leak freedom from incomplete runs."""
import json,statistics,sys
from pathlib import Path
p=Path('benchmarks/obs-cef/2026-10-09-optimization/soak');m=p/'node-memory.jsonl'
if not m.exists():print('Soak has not produced memory samples.');raise SystemExit()
rows=[]
for line in m.read_text().splitlines():
 try:rows.append(json.loads(line))
 except json.JSONDecodeError:pass
if not rows:raise SystemExit()
last=rows[-1];recent=[r for r in rows if r['atMs']>=last['atMs']-60000]
summary={'elapsedMinutes':round(last['atMs']/60000,2),'frames':last['frames'],'audioPackets':last['audioPackets'],'lastMinuteNodeMedianMiB':{key:round(statistics.median(r[key] for r in recent)/1024**2,2) for key in ['rss','heapUsed','external','arrayBuffers']},'lastMinuteNodeRangeMiB':{key:[round(min(r[key] for r in recent)/1024**2,2),round(max(r[key] for r in recent)/1024**2,2)] for key in ['heapUsed','arrayBuffers']}}
lines=(p/'processes.jsonl').read_text().splitlines();current=json.loads(lines[-1])
summary['processes']=[{key:r.get(key) for key in ['pid','role','rssBytes','pssBytes','ussBytes','fdCount','threads','cpuPercent']} for r in current['processes'] if r['role']!='sampler']
if '--compact' in sys.argv:
    summary.pop('processes',None)
    summary['nodeFdCount']=next((p.get('fdCount') for p in current['processes'] if p['role']=='node'),None)
print(json.dumps(summary))
