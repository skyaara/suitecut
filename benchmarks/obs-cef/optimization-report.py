"""Generate the optimization report from saved observations, never requested targets."""
import json,csv,statistics,hashlib
from pathlib import Path
from collections import defaultdict
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'benchmarks/obs-cef/2026-10-09-optimization'
MIB=1024**2

def read(path):return json.loads(path.read_text())
def median(values):return statistics.median(values) if values else None
def fmt(value,digits=2):return '—' if value is None else f'{value:.{digits}f}'
def plot_save(name):
 plt.tight_layout();plt.savefig(OUT/(name+'.png'),dpi=160);plt.savefig(OUT/(name+'.svg'));plt.close()
comparison='comparison-final' if (OUT/'comparison-final/trials.json').exists() else 'comparison'
trials=read(OUT/comparison/'trials.json')
groups=defaultdict(list)
for row in trials:groups[(row['case']['name'],row['backend'])].append(row)
aggregates=[]
for (case,backend),rows in groups.items():
 passed=[r for r in rows if r['status']=='passed']
 aggregates.append({'case':case,'backend':backend,'attempted':len(rows),'passed':len(passed),'distinctFpsMedian':median([r['decoded']['distinctActiveFps'] for r in passed]),'longestHoldMsMedian':median([r['decoded']['longestActiveHoldMs'] for r in passed]),'finalizationMsMedian':median([r['finalizationMs'] for r in passed]),'cpuPercentMedian':median([r['resources']['meanCpuPercent'] for r in passed]),'peakRssMiBMedian':median([r['resources']['peakRssBytes']/MIB for r in passed]),'fpsValues':[r['decoded']['distinctActiveFps'] for r in passed]})
(OUT/comparison/'aggregates.json').write_text(json.dumps(aggregates,indent=2))
with (OUT/comparison/'trials.csv').open('w') as f:
 fields=['id','backend','status','distinctFps','longestHoldMs','captureFps','finalizationMs','meanCpuPercent','peakRssMiB','error'];writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
 for r in trials:writer.writerow({'id':r['id'],'backend':r['backend'],'status':r['status'],'distinctFps':r.get('decoded',{}).get('distinctActiveFps'),'longestHoldMs':r.get('decoded',{}).get('longestActiveHoldMs'),'captureFps':r.get('nativeCapture',{}).get('fps'),'finalizationMs':r.get('finalizationMs'),'meanCpuPercent':r.get('resources',{}).get('meanCpuPercent'),'peakRssMiB':r.get('resources',{}).get('peakRssBytes',0)/MIB,'error':r.get('error')})
cases=list(dict.fromkeys(a['case'] for a in aggregates));x=np.arange(len(cases));fig,ax=plt.subplots(figsize=(10,4.5))
for offset,backend,color in [(-.19,'obs','#5d76d1'),(.19,'native','#18a184')]:
 vals=[next((a['distinctFpsMedian'] for a in aggregates if a['case']==c and a['backend']==backend),0) or 0 for c in cases];ax.bar(x+offset,vals,.36,label='OBS' if backend=='obs' else 'SuiteCut',color=color)
 for i,c in enumerate(cases):
  a=next(a for a in aggregates if a['case']==c and a['backend']==backend);ax.scatter(np.full(len(a['fpsValues']),i+offset),a['fpsValues'],color='black',s=12,zorder=3)
ax.set_xticks(x,cases);ax.set_ylabel('Distinct decoded marker transitions / second');ax.set_title('Available production pipelines: three serial trials per case');ax.legend();plot_save('comparison-fps')
before=read(OUT/'profiles/before-lifecycle-fix/memory.json');after=read(OUT/'profiles/after-lifecycle-fix/memory.json')
fig,ax=plt.subplots(figsize=(8,4))
for rows,label in [(before,'Before removable lifecycle observers'),(after,'After lifetime cleanup')]:ax.plot([r['cycle']+1 for r in rows],[r['arrayBuffers']/MIB for r in rows],marker='o',label=label)
ax.set_xlabel('Completed recording on the same live CEF source');ax.set_ylabel('Post-GC array buffers (MiB)');ax.legend();plot_save('reuse-memory')
reuse={'before':{'cycles':len(before),'firstArrayBuffers':before[0]['arrayBuffers'],'lastArrayBuffers':before[-1]['arrayBuffers'],'bytesPerCycle':(before[-1]['arrayBuffers']-before[0]['arrayBuffers'])/(len(before)-1)},'after':{'cycles':len(after),'firstArrayBuffers':after[0]['arrayBuffers'],'lastArrayBuffers':after[-1]['arrayBuffers'],'lastTenArrayBufferRange':[min(r['arrayBuffers'] for r in after[-10:]),max(r['arrayBuffers'] for r in after[-10:])]}}
(OUT/'reuse-summary.json').write_text(json.dumps(reuse,indent=2))
profiles=[read(p) for p in sorted((OUT/'profiles').glob('*/result.json'))]
profile_rows=[]
for p in profiles:profile_rows.append({'label':p['label'],'mode':p['mode'],'dimensions':f"{p['width']}x{p['height']}",'decodedDistinctFps':p.get('decoded',{}).get('distinctFps'),'nodeCpuPercent':p.get('nodeCpuPercent'),'eventLoopUtilization':p.get('eventLoopUtilization',{}).get('utilization'),'eventLoopP99Ms':p.get('eventLoopDelay',{}).get('p99Ms'),'finalizationMs':p.get('finalizationMs'),'error':p.get('error')})
(OUT/'profiles-summary.json').write_text(json.dumps(profile_rows,indent=2))
soak=read(OUT/'soak/result.json') if (OUT/'soak/result.json').exists() else None
completed_media=read(OUT/'soak/complete-media-validation.json') if (OUT/'soak/complete-media-validation.json').exists() else None
if soak and completed_media:
 for name,row in completed_media['media'].items():soak[name]=row
memory=[]
if (OUT/'soak/node-memory.jsonl').exists():
 memory=[json.loads(line) for line in (OUT/'soak/node-memory.jsonl').read_text().splitlines() if line.strip()]
 fig,axes=plt.subplots(3,1,figsize=(10,8),sharex=True)
 for key in ['rss','heapUsed','external','arrayBuffers']:axes[0].plot([r['atMs']/60000 for r in memory],[r[key]/MIB for r in memory],label=key)
 axes[0].set_ylabel('Node memory (MiB)');axes[0].legend(ncol=4)
 process_rows=[json.loads(line) for line in (OUT/'soak/processes.jsonl').read_text().splitlines() if line.strip()];origin=process_rows[0]['monotonic'];roles=defaultdict(list);uss_roles=defaultdict(list)
 for r in process_rows:
  if not r['processes'] or 'pssBytes' not in r['processes'][0]:continue
  sums=defaultdict(int);private=defaultdict(int)
  for p in r['processes']:
   sums[p['role']]+=p.get('pssBytes',0);private[p['role']]+=p.get('ussBytes',0)
  for role,value in sums.items():roles[role].append(((r['monotonic']-origin)/60,value/MIB))
  for role,value in private.items():uss_roles[role].append(((r['monotonic']-origin)/60,value/MIB))
 rss_roles=defaultdict(list)
 for r in process_rows:
  sums=defaultdict(int)
  for p in r['processes']:sums[p['role']]+=p.get('rssBytes',0)
  for role,value in sums.items():rss_roles[role].append(((r['monotonic']-origin)/60,value/MIB))
 for role,vals in roles.items():
  if role=='sampler':continue
  axes[2].plot(*zip(*vals),label=role)
 for role,vals in uss_roles.items():
  if role!='sampler':axes[1].plot(*zip(*vals),label=role)
 if uss_roles:
  axes[1].set_ylabel('Process-role USS (MiB)');axes[1].legend(ncol=4)
  axes[2].set_ylabel('Process-role PSS (MiB)');axes[2].legend(ncol=4)
 else:
  for role,values in rss_roles.items():
   if role!='sampler':axes[1].plot(*zip(*values),label=role)
  axes[1].set_ylabel('Summed role RSS (MiB)');axes[1].legend(ncol=4)
  axes[2].plot([(r['monotonic']-origin)/60 for r in process_rows],[r.get('sampleDurationMs',0) for r in process_rows],label='Sampler execution')
  axes[2].set_ylabel('Sampler execution (ms)');axes[2].legend()
 axes[2].set_xlabel('Elapsed minutes')
 for axis in axes:
  axis.axvspan(0,1,color='gray',alpha=.1)
  if soak:axis.axvline(soak.get('activeMs',1800000)/60000,color='red',ls='--',lw=1)
 plot_save('soak-memory')
 buckets=[]
 for start in range(0,int(max(r['atMs'] for r in memory)/60000)+1,5):
  rows=[r for r in memory if start<=r['atMs']/60000<start+5]
  if rows:buckets.append({'startMinute':start,'samples':len(rows),**{key:{'minMiB':min(r[key] for r in rows)/MIB,'medianMiB':median([r[key]/MIB for r in rows]),'maxMiB':max(r[key] for r in rows)/MIB} for key in ['rss','heapUsed','external','arrayBuffers']}})
 summary={'nodeBuckets':buckets,'nodeSamples':len(memory),'processSamples':len(process_rows),'rolesRssBuckets':{role:[{'startMinute':start,'medianMiB':median([v for t,v in values if start<=t<start+5])} for start in range(0,31,5)] for role,values in rss_roles.items()},'rolesUssBuckets':{role:[{'startMinute':start,'medianMiB':median([v for t,v in values if start<=t<start+5])} for start in range(0,31,5)] for role,values in uss_roles.items()},'rolesPssBuckets':{role:[{'startMinute':start,'medianMiB':median([v for t,v in values if start<=t<start+5])} for start in range(0,31,5)] for role,values in roles.items()}}
 (OUT/'soak/memory-summary.json').write_text(json.dumps(summary,indent=2))
stream_runs=[r for trial in range(1,4) for r in (read(OUT/f'features-stream-final-{trial}/results.json') if (OUT/f'features-stream-final-{trial}/results.json').exists() else [])]
stream_summary=[]
for backend in ['obs','native']:
 rows=[r for r in stream_runs if r['backend']==backend]
 def detail(row,name):return next(c.get('details',{}) for c in row['checks'] if c['name']==name)
 if rows:stream_summary.append({'backend':backend,'runs':len(rows),'passedChecks':sum(c['status']=='passed' for r in rows for c in r['checks']),'attemptedChecks':sum(len(r['checks']) for r in rows),'distinctFpsMedian':median([detail(r,'decoded receiver motion').get('distinctFps') for r in rows]),'p95AbsoluteOffsetMsMedian':median([detail(r,'decoded audio and video sync').get('p95AbsoluteOffsetMs') for r in rows]),'averageMuxedKbpsMedian':median([detail(r,'receiver media and timestamps').get('averageMuxedKbps') for r in rows])})
(OUT/'stream-summary.json').write_text(json.dumps(stream_summary,indent=2))
tuned_stream_runs=[r for trial in range(1,4) for r in (read(OUT/f'features-stream-thread-limited-{trial}/results.json') if (OUT/f'features-stream-thread-limited-{trial}/results.json').exists() else [])]
tuned_stream_summary=[]
for backend in ['obs','native']:
 rows=[r for r in tuned_stream_runs if r['backend']==backend]
 if rows:tuned_stream_summary.append({'backend':backend,'runs':len(rows),'passedChecks':sum(c['status']=='passed' for r in rows for c in r['checks']),'attemptedChecks':sum(len(r['checks']) for r in rows),'distinctFpsMedian':median([detail(r,'decoded receiver motion').get('distinctFps') for r in rows]),'p95AbsoluteOffsetMsMedian':median([detail(r,'decoded audio and video sync').get('p95AbsoluteOffsetMs') for r in rows]),'averageMuxedKbpsMedian':median([detail(r,'receiver media and timestamps').get('averageMuxedKbps') for r in rows])})
(OUT/'stream-thread-limited-summary.json').write_text(json.dumps(tuned_stream_summary,indent=2))
lines=['# SuiteCut optimization and OBS comparison — 9 October 2026','',
'The retained-frame leak is fixed in the measured reuse tests, and short browser-flow checks pass. Long-running media reliability remains unresolved: the final 30-minute dual-output run contains a 13.18-second silent interval in both outputs, video gaps over 11 seconds, and a failed received A/V window. Memory observations are finite; no indefinite leak guarantee or universal performance advantage over OBS is established.','',
'Profiles found a stale-frame catch-up loop, encoder thread contention and a real recording-session retention bug. The C++ capture path already handles browser rendering/conversion, and FFmpeg handles native encoding. The measured fixes target scheduling and ownership; no whole-application language rewrite was made.','',
'## Production changes and reasons','',
'- The recorder forwards bounded latest/pending frames with real capture timestamps in raw Matroska instead of repeatedly writing stale pixels to catch up with a constant-rate clock. Recording remains VP9, with variable presentation intervals preserved. A held final frame preserves the requested ending interval.',
'- The new raw-video container supports I420 and correctly oriented BGRA. Real FFmpeg checks verify irregular presentation times, frame count and orientation.',
'- Streaming retains the timestamp of the selected pixels through asynchronous encoding. H.264 reference dependencies are retained; repeated timestamps are made increasing without discarding dependent encoded frames.',
'- Streaming explicitly uses four x264 threads and one filter thread for the four-core quota. The production VP9 recorder already used four threads. The rejected frame-thread and H.264-recording experiments remain diagnostic.',
'- Removable browser lifecycle listeners, shared weakly cached page lifetime signals, and cleared session/recorder/parser references prevent completed recordings from retaining one full frame each.',
'- The C++ capture path borrows the existing BGRA view for I420 conversion when no popup composition is needed. A negotiated frame-subscription capability avoids conversion/transfer when there are no frame consumers and clears inactive caches. Older binaries retain the compatible continuous-frame path.',
'- Observed capture receipt tightens the native clock offset. Live audio accepts a bounded 500 ms future presentation window consistent with the native allowance, while history remains bounded. This future allowance is distinct from the playback delay.',
'- Media probing handles an unknown average frame-rate field by using a valid nominal rate, allowing timestamped VFR recordings to be read correctly.',
'', 'Source snapshots, the final patch, compiled JavaScript/native executable, test logs and run-specific source hashes are saved. Heavy rendering/conversion/encoding already execute in native code; the profiles do not justify attributing the remaining long media failure solely to JavaScript or replacing the complete API/control layer in C++.','',
'Node’s control event loop and the web page’s JavaScript/DOM execution are separate. Rewriting the host API in C++ would not make page scripts or DOM updates automatically parallel. CEF has multiple processes/threads and FFmpeg already uses native encoder threads. Reducing copies, pipe traffic and scheduling overhead can help, but language alone is not a measured explanation for the remaining failure. The independent process traces retain the evidence and uncertainty.', '',
'## Updated serial recording comparison','',
'Three ten-second trials per pipeline/case, alternating order. Motion uses decoded marker transitions and presentation timestamps, including the final held interval. Nominal container FPS is not used as elapsed time. Requested rates are targets, not success claims.','',
'| Case | Pipeline | Passed/attempted | Distinct FPS median | Longest hold ms | Finalize s | CPU % | Peak summed RSS MiB |',
'|---|---|---:|---:|---:|---:|---:|---:|']
for a in aggregates:lines.append(f"| {a['case']} | {a['backend']} | {a['passed']}/{a['attempted']} | {fmt(a['distinctFpsMedian'])} | {fmt(a['longestHoldMsMedian'])} | {fmt((a['finalizationMsMedian'] or 0)/1000)} | {fmt(a['cpuPercentMedian'])} | {fmt(a['peakRssMiBMedian'])} |")
lines+=['','![Repeated comparison](comparison-fps.png)','',
'OBS captures external Chromium through XSHM/Xvfb and records x264 veryfast CRF18/H.264 MKV with silent AAC. SuiteCut captures CEF/I420 and records VP9 CRF18/realtime/cpu-used5/four threads WebM without audio. This is an end-to-end production-pipeline comparison; codecs and capture paths differ. Software Mesa llvmpipe, four CPU equivalents, 16 GiB memory limit. Summed RSS counts shared pages more than once. This run restarted OBS; use its concurrent repeated results for the current ranking, not the old OBS medians.','',
 ]
if (OUT/'comparison-obs-thread-limited/trials.json').exists():
 tuned_trials=read(OUT/'comparison-obs-thread-limited/trials.json');tuned_groups=defaultdict(list)
 for row in tuned_trials:tuned_groups[(row['case']['name'],row['backend'])].append(row)
 tuned_aggregates=[]
 for (case,backend),rows in tuned_groups.items():
  passed=[r for r in rows if r['status']=='passed']
  tuned_aggregates.append({'case':case,'backend':backend,'attempted':len(rows),'passed':len(passed),'distinctFpsMedian':median([r['decoded']['distinctActiveFps'] for r in passed]),'longestHoldMsMedian':median([r['decoded']['longestActiveHoldMs'] for r in passed]),'finalizationMsMedian':median([r['finalizationMs'] for r in passed]),'cpuPercentMedian':median([r['resources']['meanCpuPercent'] for r in passed]),'peakRssMiBMedian':median([r['resources']['peakRssBytes']/MIB for r in passed]),'fpsValues':[r['decoded']['distinctActiveFps'] for r in passed]})
 (OUT/'comparison-obs-thread-limited/aggregates.json').write_text(json.dumps(tuned_aggregates,indent=2))
 lines+=['## Encoder-thread control comparison', '', 'OBS automatic threading is a default-policy result, while SuiteCut explicitly limits native encoder threads for this four-core quota. A second runtime applies OBS `x264opts=threads=4` and repeats all 24 alternating trials on the same final SuiteCut build. Preset, codec, rate control and capture paths remain unchanged. This controls that threading choice, not all codec/browser/GPU differences. Raw settings before/after are in `checks/obs-thread-tuning.json`.', '', '| Case | Pipeline | Passed/attempted | Distinct FPS median | Longest hold ms | Finalize s | CPU % | Peak summed RSS MiB |', '|---|---|---:|---:|---:|---:|---:|---:|']
 for a in tuned_aggregates:lines.append(f"| {a['case']} | {a['backend']} | {a['passed']}/{a['attempted']} | {fmt(a['distinctFpsMedian'])} | {fmt(a['longestHoldMsMedian'])} | {fmt((a['finalizationMsMedian'] or 0)/1000)} | {fmt(a['cpuPercentMedian'])} | {fmt(a['peakRssMiBMedian'])} |")
 fig,ax=plt.subplots(figsize=(10,4.5))
 for offset,backend,color in [(-.19,'obs','#5d76d1'),(.19,'native','#18a184')]:
  vals=[next((a['distinctFpsMedian'] for a in tuned_aggregates if a['case']==c and a['backend']==backend),0) or 0 for c in cases];ax.bar(x+offset,vals,.36,label='OBS x264 threads=4' if backend=='obs' else 'SuiteCut repeated control',color=color)
  for i,c in enumerate(cases):
   a=next(a for a in tuned_aggregates if a['case']==c and a['backend']==backend);ax.scatter(np.full(len(a['fpsValues']),i+offset),a['fpsValues'],color='black',s=12,zorder=3)
 ax.set_xticks(x,cases);ax.set_ylabel('Distinct decoded marker transitions / second');ax.set_title('Four-core quota: explicit encoder-thread settings');ax.legend();plot_save('comparison-thread-limited-fps')
 lines+=['', '![Thread-limited comparison](comparison-thread-limited-fps.png)', '', 'Active encoder log excerpts are saved in `checks/obs-runtime-thread-settings.json`, and captured on-disk settings are restored after shutdown. An earlier control launch collided with a duplicate runtime: X/Pulse binding failures, conflicting PID records and profile restoration exposed the problem. Its media checks passed, but isolation/process accounting was uncertain, so it is excluded from this ranking and preserved with `-runtime-collision` suffixes. The final control coordinator uses an exclusive lock and verifies its runtime and restored settings. See `checks/thread-control-runtime-collision.json` for relocation and failure details.', '']
lines+=['## Bottleneck experiments','', 
'| Experiment | Mode | Pixels | Decoded distinct FPS | Node % of one core | Loop utilization | Loop p99 ms | Finalize s |','|---|---|---|---:|---:|---:|---:|---:|']
for p in profile_rows:lines.append(f"| {p['label']} | {p['mode']} | {p['dimensions']} | {fmt(p['decodedDistinctFps'])} | {fmt(p['nodeCpuPercent'])} | {fmt(p['eventLoopUtilization'])} | {fmt(p['eventLoopP99Ms'])} | {fmt((p['finalizationMs'] or 0)/1000)} |")
lines+=['',
'These diagnostics used instrumentation and different builds/settings, so they are attribution experiments, not the serial comparison table. Baseline recorder forwarding wrote 606 pixel buffers, 600 reusing the preceding Buffer, and spent 24.65 seconds blocked on FFmpeg drain across a ten-second capture and finalization. Only six decoded marker transitions remained. Timestamped Matroska input removes the stale-frame catch-up loop. The four-thread x264 experiment improves streaming; disabling sliced threads was worse and was rejected. The H.264 recorder substitution was diagnostic only; the production recorder remains VP9. Static unpaced encoder results and complete arguments/stderr are in `profiles/isolated-encoders.json`; they do not measure browser capture throughput.','',
'## Recording-session retention','',
 f"25 recordings reused one live 960x540 I420 source. Post-GC array buffers grew by {reuse['before']['bytesPerCycle']:,.0f} bytes per recording before the fix (one full pixel frame), ending at {before[-1]['arrayBuffers']/MIB:.2f} MiB. After removable failure/close listeners and reference cleanup, the last ten recordings stayed between {reuse['after']['lastTenArrayBufferRange'][0]/MIB:.3f} and {reuse['after']['lastTenArrayBufferRange'][1]/MIB:.3f} MiB. Raw samples and manifests are retained. Forced GC isolates retained memory here; it was not used during the continuous soak.",
'','![Source reuse memory](reuse-memory.png)','', 'Page wrappers now share one weakly cached browser lifetime signal. A regression creates 2,000 wrappers and verifies only one failure and one close observer, while preserving source failure and caller cancellation. The final comparison and continuous soak include this change. The earlier diagnostic comparison is preserved separately.', '',
'An extended lifetime-fix build test completed 200 recordings on one live source. Heap snapshots after recording 11 and 200 show Buffer objects 60 → 60, Promise objects 241 → 241, ChildProcess objects 1 → 1 and AbortController objects 2 → 2. Post-GC heap increased by about 0.52 MiB overall; the largest shallow-size increases are V8 code/byte-array structures, and the harness deliberately retains 200 measurement rows. AbortSignal objects increased by six, not one per recording. These object counts and shallow sizes support the lifetime fix but are not a complete dominator/allocation audit. Raw snapshots, memory samples and `profiles/extended-reuse-after/heap-comparison.json` are preserved.', '',
'## Streaming audio regression found during the first long test','',
'The first complete 30-minute run had bounded Node heap and passed recorded-audio windows, but the received AAC audio was silent in the midpoint window. It is preserved as `soak-before-audio-window-fix/`, with its failed result and diagnostic packet traces. It does not count as a passing long run. Lossless remuxing reproduced the silence, excluding a seek-only artifact. Native audio allowed presentation times up to 250 ms ahead of receipt while the live buffer discarded packets more than 100 ms ahead. The mapped handshake clock also included transit delay. Capture timestamps now refine that clock upper bound; the live buffer has a bounded 500 ms future allowance. Regression tests exercise both cases. The final run adds observed audio timestamp offsets; its complete media results, rather than the plausible mechanism alone, determine whether streaming passes.', '',
'A second 30-minute run with the audio window/clock fix delivered audible midpoint audio, but its live received midpoint p95 synchronization error was about 219 ms and the finalized recording’s midpoint error was about 217 ms, exceeding the unchanged 200 ms fixture limit. Finalization stopped at the recording assertion, so the live receiver check is retained separately rather than represented as a completed final receiver validation. It is preserved separately as `soak-before-capture-stamp-fix`. The encoder had associated selected pixels with their scheduled time slot instead of their actual capture time; backpressure or sparse capture can make those differ. The encoded output now carries the selected frame’s capture timestamp, retaining valid codec dependencies. A delayed-processing regression exercises that association. The overlapping short pilot failed the unchanged validation count/sync criteria under contention and is preserved with all six decoded windows. The first serial pilot had p95 errors below 84 ms in all six windows but only four flashes at its end, failing the unchanged five-match requirement. A 250 ms pulse replaces the 120 ms diagnostic pulse to improve visibility under sparse capture; the following serial pilot and final long run use that fixture. Source/codec paths are unchanged, and all earlier failed results remain saved. Both failed runs remain evidence, not passing results.', '',
'## 60 fps stress failure and system/sampler stall', '', 'The subsequent 1080p60 dual-output run completed 30 minutes with stable managed memory and passed all recorded A/V windows. Its first received sync window remained a strict failure at 203.33 ms against 200 ms (checker resolution is 10 ms audio and 16.67 ms resampled video; sparse capture adds uncertainty). Full-file audio analysis then found 15.20 s recorded silence and 19.86 s received silence near minute 24. Node had a 16.18 s event-loop gap and the independent process sampler had long concurrent gaps with little CPU progress across native processes. This is not evidence of a JavaScript-only stall, and observer/host causality is not established. The complete failed run, all six windows, decoded full PCM/RMS, process-sampler gaps and private-memory traces remain under `soak-60fps-private-sampler-stall/`.', '', 'The final long verification uses the supported 1080p30 target with lightweight RSS/CPU/descriptor sampling and no periodic private-memory walks. It too completed the workload but failed received start-window sync (298.33 ms, with one outlying periodic match) and full-file audio continuity (a 13.18 s silent interval in both outputs). Thus lowering the target and disabling private-memory walks did not establish reliable long media on this host. These failures are retained; no clean long-run media pass is claimed. Target and monitor both differ, so this is a supported-workload validation rather than an isolated proof that private-memory sampling caused the earlier stall. All 60 fps stress failures remain explicit.', '',
'## Continuous recording and streaming memory test','']
if soak:
 lines+=[f"Observed status: **{soak['status']}**. Requested {soak['requestedMs']/60000:.0f} minutes; observed active interval {soak.get('activeMs',0)/60000:.3f} minutes. A single caller-owned 1080p{soak['targetFps']} I420 CEF source simultaneously records VP9 plus Opus and streams H.264 plus AAC to a local RTMP receiver. Canvas motion, periodic real wheel scrolling and synchronized 250 ms flash/440 Hz tone bursts remain active. Earlier diagnostics used 120 ms bursts; the final longer pulse improves observability when video capture is sparse, while the five-match and 200 ms synchronization requirements remain unchanged. This concurrent stress run is separate from FPS comparison trials.", '']
 if soak.get('error'):lines+=[f"Run error: `{soak['error']}`",'']
 for name in ['recording','receiver']:
  if name in soak:
   lines+=[f"{name.capitalize()}: {soak[name]['durationSeconds']:.3f} seconds, {soak[name]['bytes']:,} bytes. Decoded A/V windows:",'','| Window offset seconds | Status | Matched flashes | p95 absolute A/V offset ms |','|---:|---|---:|---:|']
   for w in soak[name]['windows']:lines.append(f"| {w['offset']} | {w.get('status', 'passed')} | {w['matchedFlashes']} | {fmt(w['p95AbsoluteOffsetMs'])} |")
   lines+=['']
 continuity_path=OUT/'soak/audio-continuity-validation.json'
 if continuity_path.exists():
  continuity=read(continuity_path)
  lines+=['Complete audio is separately decoded to 8 kHz mono PCM and analyzed in 20 ms RMS blocks. The one-second pulse fixture deliberately includes silence; the continuity check reports all gaps over two seconds and fails on any gap over five seconds between 3 s and five seconds before the requested end. This checks audible coverage, not continuous A/V synchronization. Full PCM, RMS traces and minute metrics are preserved.', '']
  for name,row in continuity.items():lines.append(f"{name.capitalize()} continuity: **{row['status']}**, longest silent interval {row['longestSilentInterval']['durationSeconds']:.2f} s, intervals over two seconds: {len(row['silentIntervalsOver2Seconds'])}, over five seconds: {len(row['silentIntervalsOver5Seconds'])}.")
  lines+=['']
 lines+=['Full video packet analysis finds ordered, unique presentation/decode timestamps but maximum video gaps of about 11.33 seconds in the recording and 11.40 seconds in the receiver. Positive per-minute packet counts and motion in three sampled windows do not establish uninterrupted video. The final run also has a 7.34-second maximum Node event-loop delay and a 9.60-second independent process-sampler gap. Their correlation does not isolate JavaScript, IPC, observer or host causality. Full gap observations are retained in `soak/process-sampler-gaps.json` and `soak/packet-motion-validation.json`.', '']
 lines+=[f"Captured audio offsets relative to Node receipt: `{soak.get('audioOffsets', {})}`. These diagnostics count source packets; the decoded receiver checks independently verify delivered sound.", '', '![Continuous memory](soak-memory.png)','',f"Node samples: {len(memory)}. Memory is sampled without forced GC during recording. The final 30 fps run samples per-process CPU, RSS, threads and open descriptors every half second and records sampler execution time. Periodic PSS/USS walks are disabled here after a correlated system/sampler stall in the 60 fps stress run; that earlier private-memory data remains saved. RSS sums count shared pages repeatedly and are not private-memory totals. See `soak/memory-summary.json` for five-minute buckets, `soak/processes.jsonl` for individual processes, and `soak/node-memory.jsonl` for Node RSS, heap and buffers.",'',
 'The shaded first minute marks startup. The final soak has no concurrent source-reuse tests, OBS comparisons, unit/build checks or live media decodes. The prior stress run supplies PSS/USS measurements; final-run role RSS is explicitly shown as RSS. The red line marks the end of the active run; later process samples reflect finalization/media validation.', '', 'This finite run cannot prove leak freedom forever or cover all CEF/FFmpeg paths. Conclusions concern the measured workload/build/duration. Post-cleanup forced-GC memory is retained separately in the result; external native libraries are not fully allocation-audited.']
else:lines+=['The continuous run is still in progress. No completed soak is claimed.']
lines+=['','## Repeated 1080p60 browser stream checks','', '| Pipeline | 20-second runs | Checks passed | Distinct FPS median | p95 A/V ms median | Received muxed kbps median |', '|---|---:|---:|---:|---:|---:|']
for row in stream_summary:lines.append(f"| {row['backend']} | {row['runs']} | {row['passedChecks']}/{row['attemptedChecks']} | {fmt(row['distinctFpsMedian'])} | {fmt(row['p95AbsoluteOffsetMsMedian'])} | {fmt(row['averageMuxedKbpsMedian'])} |")
if tuned_stream_summary:
 lines+=['', 'The restarted OBS runtime with explicit four-thread x264 settings repeats the same received-stream checks:', '', '| Pipeline | 20-second runs | Checks passed | Distinct FPS median | p95 A/V ms median | Received muxed kbps median |', '|---|---:|---:|---:|---:|---:|']
 for row in tuned_stream_summary:lines.append(f"| {row['backend']} | {row['runs']} | {row['passedChecks']}/{row['attemptedChecks']} | {fmt(row['distinctFpsMedian'])} | {fmt(row['p95AbsoluteOffsetMsMedian'])} | {fmt(row['averageMuxedKbpsMedian'])} |")
lines+=['', 'Each stream is received as real RTMP media and decoded. The medians summarize three repetitions, not a codec/quality-normalized claim: OBS uses x264 veryfast/high with its normal B-frame settings and AAC160; SuiteCut uses x264 veryfast/zerolatency with four encoder threads, adaptive video control and AAC128. Both target 4,500 kbps, but actual muxed bitrate is measured above. Periodic flash/tone matching resolves offsets only within half a second and does not measure network/display latency. Raw receiver packets and media are preserved.', '', '## Correctness and saved evidence','',
'Native integration checks cover BGRA/I420 sources, browser input, navigation, narration preparation pauses, rendered audio/page selection, checkpoints, source ownership, callback failure/cancellation, streaming reconnect/source replacement and A/V sync. The real-FFmpeg container regression checks orientation and irregular 0/17/400/1000 ms timestamps without manufacturing missing frames. Logs and reports are saved under `checks/` and `logs/`. Test totals are read from saved test logs.','',
'The final OBS-comparable feature runs contain four recording runs with 40 checks, six default-policy RTMP runs with 24 checks, and six additional encoder-thread-control RTMP runs with 24 checks. Each check’s measured outcome is retained; failed long media is a separate finding and is not overwritten by passing short checks. Read `features-record-final/results.json` and `features-stream-final-{1,2,3}/results.json` for each operation, source-state sequence, codec/packet check, tone analysis and A/V result. Streaming FPS is reported from received media duration and the original hold calculation is retained as a diagnostic approximation; the recording comparison uses exact frame PTS.', '',
'Final verification: 66 focused unit tests in ten files, run serially with a 30-second test timeout after one parallel worklet test exceeded its default five-second limit during overlapping encoder load; type checking, focused ESLint and formatting; two C++ CTest helpers; real FFmpeg timestamp/orientation regression. AddressSanitizer/UndefinedBehaviorSanitizer helper tests with leak detection are saved under `logs/asan-*.log`. The prebuilt CEF and libyuv libraries are not instrumented by those helper tests; they are not a full native/browser allocation audit.', '',
'The original 90-trial/56-functional-check report is `../2026-10-09/report.md`. Its original complete archive remains `/workspace/shared/suitecut-obs-evidence-2026-10-09.tar.gz`. This optimization report, source snapshots, patch, CPU profiles, repeated trials, raw memory samples, manifests, failed/rejected diagnostics, recordings and RTMP receiver files are saved separately. `notes.md` describes measurement boundaries; `reproduce.sh` records the commands for this run.']
(OUT/'report.md').write_text('\n'.join(lines)+'\n')
import html
(OUT/'report.html').write_text('<!doctype html><meta charset="utf-8"><title>SuiteCut optimization evidence</title><style>body{max-width:1100px;margin:40px auto;font:16px/1.6 system-ui;padding:20px}pre{white-space:pre-wrap}img{max-width:100%}</style><h1>SuiteCut optimization evidence</h1><p>See report.md for the full tables and clickable relative evidence paths.</p>'+''.join(f'<img src="{n}.png" alt="{n}">' for n in ['comparison-fps','reuse-memory']+(['comparison-thread-limited-fps'] if (OUT/'comparison-thread-limited-fps.png').exists() else [])+(['soak-memory'] if memory else []))+'<pre>'+html.escape('\n'.join(lines))+'</pre>')
print(json.dumps({'trials':len(trials),'passed':sum(r['status']=='passed' for r in trials),'soak':soak and soak['status']}))
