"""Compare saved V8 heap object counts; retain a bounded summary, not raw values."""
import json,collections
from pathlib import Path
root=Path('benchmarks/obs-cef/2026-10-09-optimization/profiles/extended-reuse-after')
def counts(path):
 d=json.loads(path.read_text());fields=d['snapshot']['meta']['node_fields'];width=len(fields);types=d['snapshot']['meta']['node_types'][0];strings=d['strings'];nodes=d['nodes'];type_at=fields.index('type');name_at=fields.index('name');size_at=fields.index('self_size');result=collections.defaultdict(lambda:[0,0])
 for i in range(0,len(nodes),width):
  key=(types[nodes[i+type_at]],strings[nodes[i+name_at]]);result[key][0]+=1;result[key][1]+=nodes[i+size_at]
 return result
before=counts(root/'heap-10.heapsnapshot');after=counts(root/'heap-199.heapsnapshot');rows=[]
for key in set(before)|set(after):
 b=before[key];a=after[key];rows.append({'type':key[0],'name':key[1],'countBefore':b[0],'countAfter':a[0],'countDelta':a[0]-b[0],'selfBytesBefore':b[1],'selfBytesAfter':a[1],'selfBytesDelta':a[1]-b[1]})
# This is shallow size/count attribution, not a dominator or complete leak verdict.
summary={'method':'V8 shallow self sizes and object counts; dominator/retainer paths require deeper inspection','topGrowth':sorted(rows,key=lambda r:r['selfBytesDelta'],reverse=True)[:80],'selectedCounts':[r for r in rows if r['name'] in ['NativePage','AbortController','AbortSignal','Promise','WeakRef','ChildProcess','Buffer','Map','Set','Writable','Readable','NativeVideoContainer']]}
(root/'heap-comparison.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary['selectedCounts']))
