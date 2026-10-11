"""Summarize saved V8 CPU profiles without inferring native time from JS stacks."""
import json,collections
from pathlib import Path
root=Path('benchmarks/obs-cef/2026-10-09-optimization/profiles');rows=[]
for p in sorted(root.glob('*.cpuprofile')):
 d=json.loads(p.read_text());nodes={n['id']:n for n in d['nodes']};counts=collections.Counter(d.get('samples',[]));rank=[]
 for identity,count in counts.most_common(30):
  f=nodes[identity]['callFrame'];rank.append({'function':f['functionName'],'url':f['url'],'line':f['lineNumber']+1,'samples':count,'samplePercent':count/max(1,len(d.get('samples',[])))*100})
 rows.append({'profile':p.name,'samples':len(d.get('samples',[])),'durationMs':(d['endTime']-d['startTime'])/1000,'topFunctions':rank})
(root/'cpu-profile-functions.json').write_text(json.dumps(rows,indent=2));print(json.dumps({'profiles':len(rows)}))
