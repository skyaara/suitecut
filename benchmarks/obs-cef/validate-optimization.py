"""Check measured durations, completed trials, frame timestamps and evidence files."""
import json
from pathlib import Path
root=Path.cwd();out=root/'benchmarks/obs-cef/2026-10-09-optimization'
def read(path):return json.loads(path.read_text())
for comparison in ['comparison-final','comparison-obs-thread-limited']:
 r=read(out/comparison/'trials.json');assert len(r)==24;assert len({x['id'] for x in r})==24
 for trial in r:
  assert trial['status']=='passed',trial['id']
  directory=out/comparison/'trials'/trial['id'];pts=[float(f['best_effort_timestamp_time']) for f in read(directory/'frame-timestamps.json')['frames']];ids=read(directory/'marker-ids.json')
  assert len(pts)==len(ids) and len(ids)>3
  assert all(b>=a for a,b in zip(pts,pts[1:]));assert trial['decoded']['activeDurationSeconds']>=9.5
  assert trial['decoded']['distinctActiveFps']>0;assert Path(trial['mediaPath']).is_file()
  if trial['case']['workload']=='scroll':assert trial['workload']['snapshot']['scrollEvents']>=2
for name in ['features-record-final',*[f'features-stream-final-{i}' for i in range(1,4)],*[f'features-stream-thread-limited-{i}' for i in range(1,4)]]:
 for run in read(out/name/'results.json'):
  assert run.get('completedAt') and not run.get('error')
  assert all(c['status']=='passed' for c in run['checks'])
for name in ['recording','stream']:
 check=read(out/'checks'/f'{name}.json');assert check
for row in read(out/'checks/container/results.json'):assert row['status']=='passed';assert len(row['pts'])==4
before=read(out/'profiles/before-lifecycle-fix/memory.json');after=read(out/'profiles/after-lifecycle-fix/memory.json');assert len(before)==25 and len(after)==25
assert before[-1]['arrayBuffers']-before[0]['arrayBuffers']>15*1024**2
assert max(x['arrayBuffers'] for x in after[-10:])-min(x['arrayBuffers'] for x in after[-10:])<1024**2
soak=read(out/'soak/result.json');assert soak.get('completedAt');assert soak['activeMs']>=soak['requestedMs']*.99;assert soak['scrollCommands']>100;assert soak['audioPackets']>10000;assert soak['sourceStateAfterRecording']=='running'
completed_media=read(out/'soak/complete-media-validation.json')
media_failures=[]
for name in ['recording','receiver']:
 soak[name]=completed_media['media'][name]
 assert soak[name]['durationSeconds']>=soak['requestedMs']/1000*.95;assert Path(soak[name+'Path']).is_file()
 assert len(soak[name]['windows'])==3
 for w in soak[name]['windows']:
  assert w['matchedFlashes']>=5
  if w['status']!='passed':media_failures.append({'media':name,'offset':w['offset'],'p95AbsoluteOffsetMs':w['p95AbsoluteOffsetMs']})
continuity=read(out/'soak/audio-continuity-validation.json')
for row in continuity.values():assert row['decodedSeconds']>=soak['requestedMs']/1000*.95
packet_motion=read(out/'soak/packet-motion-validation.json')
for row in packet_motion.values():assert row['status']=='passed' and row['videoPackets']>1000 and row['duplicatePresentationTimes']==0 and row['nonIncreasingDecodeTimes']==0
# The packet total is copied before stopping; the shared offset object continues
# observing source audio until cleanup finishes.
assert soak['audioOffsets']['count']>=soak['audioPackets']
extended=read(out/'profiles/extended-reuse-after/memory.json');assert len(extended)==200
heap=read(out/'profiles/extended-reuse-after/heap-comparison.json')
for obj in heap['selectedCounts']:
 if obj['type']=='object' and obj['name'] in ['Buffer','Promise','ChildProcess','AbortController']:assert obj['countDelta']==0
summary=read(out/'soak/memory-summary.json');assert summary['nodeSamples']>=1700;assert summary['processSamples']>=3000
# Treat growth as a finding rather than hiding it behind an arbitrary leak pass threshold.
assert read(out/'final-checks-driver.json')['status']=='passed'
control=read(out/'thread-limited-checks-driver.json')
assert control['status']=='passed' and control['exclusiveCoordinatorLock']
assert control['runtimeStopped'] and control['profileEncoderSettingsRestored']
runtime_log=(out/'logs/thread-limited-runtime-driver.log').read_text()
for role in ['Xvfb','pulseaudio','obs']:assert str(control['runtimePids'][role]) in runtime_log
assert 'Fatal server error' not in (out/'logs/thread-limited-runtime/reproduce-xvfb.log').read_text()
assert 'Address already in use' not in (out/'logs/thread-limited-runtime/reproduce-pulse.log').read_text()
thread_settings=read(out/'checks/obs-runtime-thread-settings.json')
assert thread_settings['firstRuntime']['customThread4Blocks']==0
assert thread_settings['fourThreadRuntime']['customThread4Blocks']==15
result={'status':'completed with long-run media failures' if media_failures or any(v['status']!='passed' for v in continuity.values()) else 'passed','longRunMediaStatus':soak['status'],'failedSyncWindows':media_failures,'audioContinuityStatus':{k:v['status'] for k,v in continuity.items()},'memoryConclusion':'No per-recording retained-frame growth after fixes in the measured reuse tests; continuous memory observations are finite, not an indefinite leak guarantee.','repeatedTrials':48,'passedTrials':48,'retentionCyclesPerBuild':25,'finalExtendedReuseCycles':200,'updatedFeatureChecks':88,'soakObservedActiveSeconds':soak['activeMs']/1000,'recordedSeconds':soak['recording']['durationSeconds'],'receivedSeconds':soak['receiver']['durationSeconds'],'syncWindows':6,'nodeSamples':summary['nodeSamples'],'processSamples':summary['processSamples']}
(out/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
