"""Correlate existing immutable evidence and summarize separately saved diagnostic runs."""
import json,statistics,sys
from datetime import datetime
from pathlib import Path
OUT=Path('benchmarks/obs-cef/2026-10-09-dropout-investigation')
def load(path):return json.loads(path.read_text())
def rows(path):return [json.loads(s) for s in path.read_text().splitlines() if s.strip()]
old=Path('benchmarks/obs-cef/2026-10-09-optimization/soak');result=load(old/'result.json');manifest=load(old/'manifest.json')
old_media=load(Path('benchmarks/obs-cef/2026-10-09-obs-long-control/media-validation.json'))['suitecut']
first=manifest['tests'][0]['attempts'][0]['videoTiming'][0]['firstFrameEpochMs'];ready=datetime.fromisoformat(result['readyAt'].replace('Z','+00:00')).timestamp()*1000;media_offset=(ready-first)/1000
node=rows(old/'node-memory.jsonl');proc=rows(old/'processes.jsonl')
gaps=[]
for a,b in zip(node,node[1:]):
 seconds=(b['atMs']-a['atMs'])/1000
 if seconds>2:
  cpu=((b['cpu']['user']+b['cpu']['system'])-(a['cpu']['user']+a['cpu']['system']))/1e6
  gaps.append({'runStartSeconds':a['atMs']/1000,'runEndSeconds':b['atMs']/1000,'recordingStartSeconds':a['atMs']/1000+media_offset,'recordingEndSeconds':b['atMs']/1000+media_offset,'durationSeconds':seconds,'framesReceived':b['frames']-a['frames'],'audioPacketsReceived':b['audioPackets']-a['audioPackets'],'nodeCpuSeconds':cpu,'nodeCpuPercentOfOneCore':100*cpu/seconds})
sampler_gaps=[{'fromSamplerStartSeconds':a['monotonic']-proc[0]['monotonic'],'toSamplerStartSeconds':b['monotonic']-proc[0]['monotonic'],'gapSeconds':b['monotonic']-a['monotonic'],'scanDurationMs':a['sampleDurationMs']} for a,b in zip(proc,proc[1:]) if b['monotonic']-a['monotonic']>2]
plateaus=[];start=0
for i in range(1,len(node)+1):
 if i==len(node) or (node[i]['frames'],node[i]['audioPackets'])!=(node[start]['frames'],node[start]['audioPackets']):
  if i-1>start:
   a,b=node[start],node[i-1];seconds=(b['atMs']-a['atMs'])/1000;cpu=((b['cpu']['user']+b['cpu']['system'])-(a['cpu']['user']+a['cpu']['system']))/1e6
   plateaus.append({'recordingStartSeconds':a['atMs']/1000+media_offset,'recordingEndSeconds':b['atMs']/1000+media_offset,'durationSeconds':seconds,'nodeCpuSeconds':cpu,'nodeCpuPercentOfOneCore':100*cpu/seconds,'sampleCount':i-start})
  start=i
def jumps(directory,holds):
 frames=load(directory/'frame-timestamps.json')['frames'];pts=[float(f['best_effort_timestamp_time']) for f in frames];ids=load(directory/'marker-ids.json');out=[]
 for hold in holds:
  i=next(i for i,t in enumerate(pts) if abs(t-hold['startSeconds'])<.001);j=next(i for i,t in enumerate(pts) if abs(t-hold['endSeconds'])<.001)
  out.append({**hold,'resumedMarker':ids[j],'markerAdvance':ids[j]-ids[i]})
 return out
summary={'original':{'recordingClockOffsetFromRunSeconds':media_offset,'nodeGapsOver2Seconds':gaps,'independentSamplerGapsOver2Seconds':sampler_gaps,'independentSamplerClockCaveat':'Relative to sampler start, not exactly the recording origin; no original realtime/monotonic anchor was saved. Do not use its per-process CPU percentages during long scans: timestamps bracket different moments. Node process.cpuUsage deltas are directly usable.','videoHoldsWithMarkerAdvance':jumps(Path('benchmarks/obs-cef/2026-10-09-obs-long-control/analysis/suitecut/recording'),old_media['recording']['video']['contentHoldsOver5Seconds']),'media':old_media,'eventLoopDelay':result['eventLoopDelay']},'runs':{}}
summary['original']['longestNoReceiptInterval']=max(plateaus,key=lambda x:x['durationSeconds'])
for directory in [p for p in OUT.iterdir() if p.is_dir() and (p/'result.json').exists()]:
 r=load(directory/'result.json');n=[x for x in rows(directory/'node-memory.jsonl') if x['atMs']<=r['activeMs']];p=rows(directory/'processes.jsonl');stdio=rows(directory/'stdio.jsonl')
 native=[json.loads(s.split('SUITECUT_DIAGNOSTIC ',1)[1]) for s in (directory/'logs/native.log').read_text().splitlines() if s.startswith('SUITECUT_DIAGNOSTIC ')]
 samples=[x for x in stdio if x['type']=='sample'];spawns={x['pid']:x for x in stdio if x['type']=='spawn' and x.get('pid') is not None}
 timings=[]
 for pid,spawn in spawns.items():
  seen=[c for x in stdio for c in (x.get('children',[]) if x['type']=='sample' else [x] if x['type']=='close' else []) if c.get('pid')==pid]
  last=seen[-1] if seen else spawn
  timings.append({'pid':pid,'command':spawn['command'],'args':spawn['args'],'writes':last['writes'],'drains':last['drains'],'maxDrainMs':last['maxDrainMs'],'closed':last['closed']})
 memory={}
 for key in ['rss','heapUsed','external','arrayBuffers']:
  warm=[x[key]/1024**2 for x in n if 600_000<=x['atMs']<900_000];late=[x[key]/1024**2 for x in n if x['atMs']>=r['requestedMs']-300_000]
  memory[key]={'minMiB':min(x[key] for x in n)/1024**2,'maxMiB':max(x[key] for x in n)/1024**2,'lateMedianMiB':statistics.median(late),'warmMedianMiB':statistics.median(warm) if warm else None}
 gc=[]
 for line in (directory/'logs/workload.log').read_text().splitlines():
  if 'pause=' in line:
   try:gc.append(float(line.split('pause=',1)[1].split()[0]))
   except ValueError:pass
 row={'resultStatus':r['status'],'requestedMs':r['requestedMs'],'activeMs':r['activeMs'],'injection':r.get('injection'),'pipelineTiming':r['pipelineTiming'],'eventLoopDelay':r['eventLoopDelay'],'intervalLoopMaxMs':max(x['loopMaxMs'] for x in samples),'nativeFinal':native[-1],'nativeMonitorMaximumIntervalMs':max(b['hostMs']-a['hostMs'] for a,b in zip(native,native[1:])),'independentSamplerMaximumIntervalSeconds':max(b['monotonic']-a['monotonic'] for a,b in zip(p,p[1:])),'independentSamplerMaximumScanMs':max(x['sampleDurationMs'] for x in p),'childWriteTiming':timings,'nodeMemory':memory,'afterCleanupGc':r['afterCleanupGc'],'gcObservedMaxPauseMs':max(gc) if gc else None,'gcObservedCount':len(gc),'gcTraceEnabled':bool(gc),'scrollCommands':r['scrollCommands'],'scrollEvents':r['snapshot']['scrollEvents']}
 row['nativeAtWorkloadEnd']=min(native,key=lambda x:abs(x['videoWritten']-r['frames']))
 row['nativeEndSampleScope']='Nearest one-second native snapshot by delivered video count; full nativeFinal additionally includes startup/finalization and short post-workload sync decoding.'
 if (directory/'media-validation.json').exists():
  row['media']=load(directory/'media-validation.json')
  hold=row['media']['outputs']['recording']['video']['longestContentHold'];row['longestHoldMarkerAdvance']=jumps(directory/'analysis/recording',[hold])[0]
 active=[s for s in p if r['runStartMonotonicSeconds']<=s['monotonic']<=r['runStartMonotonicSeconds']+r['activeMs']/1000]
 row['processCpuBudget']=[]
 for pid in sorted({q['pid'] for s in active for q in s['processes']}):
  seen=[q for s in active for q in s['processes'] if q['pid']==pid]
  if len(seen)<2:continue
  a,b=seen[0],seen[-1];seconds=b['cpuObservedMonotonic']-a['cpuObservedMonotonic'];used=b['cpuSeconds']-a['cpuSeconds']
  row['processCpuBudget'].append({'pid':pid,'role':a['role'],'observedSeconds':seconds,'cpuSeconds':used,'averageCpuPercentOfOneCore':100*used/seconds,'firstRssMiB':a['rssBytes']/2**20,'lastRssMiB':b['rssBytes']/2**20,'maximumRssMiB':max(q['rssBytes'] for q in seen)/2**20,'firstFdCount':next((q['fdCount'] for q in seen if 'fdCount' in q),None),'lastFdCount':next((q['fdCount'] for q in reversed(seen) if 'fdCount' in q),None)})
 def counter(s,name):return {x.split()[0]:int(x.split()[1]) for x in s['host'].get(name,'').splitlines()}
 if len(active)>1:
  before,after=counter(active[0],'cpu.stat'),counter(active[-1],'cpu.stat');elapsed=active[-1]['monotonic']-active[0]['monotonic']
  row['cgroupCpuDelta']={key:after[key]-before[key] for key in before if key in after};row['cgroupAverageUsedCores']=row['cgroupCpuDelta']['usage_usec']/1e6/elapsed
 summary['runs'][directory.name]=row
(OUT/'findings.json').write_text(json.dumps(summary,indent=2));print(json.dumps({name:{'activeMinutes':x['activeMs']/60000,'status':x.get('media',{}).get('status'),'audioDropped':x['nativeFinal']['audioDropped']} for name,x in summary['runs'].items()}))
