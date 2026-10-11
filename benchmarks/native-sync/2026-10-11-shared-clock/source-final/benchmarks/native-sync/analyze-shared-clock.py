"""Decode every retained output; keep delivery defects separate from harness validity."""
import csv, importlib.util, json, pathlib, subprocess, sys
import numpy as np

root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('publication_analysis',root/'benchmarks/publication/analyze.py')
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)

def trace(directory):
 paths=list(directory.glob('cef-*.csv'))
 if not paths:return None
 source=list(csv.DictReader(paths[0].open()))
 starts=np.array([float(r['mapped_ms']) for r in source]);arrivals=np.array([float(r['received_ms']) for r in source]);frames=np.array([int(r['frames']) for r in source]);ends=starts+frames/48
 raw=np.fromfile(paths[0].with_suffix('.s16'),dtype='<i2').reshape(-1,2);offsets=np.r_[0,np.cumsum(frames)]
 gaps=[{'fromMs':float(ends[i]),'durationMs':float(starts[i+1]-ends[i])} for i in range(len(starts)-1) if starts[i+1]-ends[i]>2]
 encode=[p for p in directory.glob('*.csv') if not p.name.startswith(('cef-','audio-lifecycle-')) and not p.name.endswith('.encode.csv')]
 report={'sourcePackets':len(source),'sourceFrames':int(frames.sum()),'sourceSilentPackets':sum(float(r['rms'])<.001 for r in source),'sourceTimelineGaps':gaps,'maximumCallbackDelayMs':float(max(arrivals-starts)),'packetFrames':sorted(set(frames.tolist()))}
 raw_starts=np.array([float(r.get('raw_mapped_ms',r['mapped_ms'])) for r in source]);raw_ends=raw_starts+frames/48
 report['rawSourceTimelineGaps']=[{'fromMs':float(raw_ends[i]),'durationMs':float(raw_starts[i+1]-raw_ends[i])} for i in range(len(raw_starts)-1) if raw_starts[i+1]-raw_ends[i]>2]
 report['normalizerMaximumCorrectionMs']=float(max(abs(starts-raw_starts)))
 report['normalizerIntroducedGapEvents']=[{'packetIndex':i+1,'mappedGapMs':float(starts[i+1]-ends[i]),'rawGapMs':float(raw_starts[i+1]-raw_ends[i])} for i in range(len(starts)-1) if starts[i+1]-ends[i]>2 and raw_starts[i+1]-raw_ends[i]<=2]
 if not encode:return report
 blocks=list(csv.DictReader(encode[0].open()));pre=np.fromfile(encode[0].with_suffix('.s16'),dtype='<i2').reshape(-1,2)
 begin=float(blocks[0]['at_ms']);finish=float(blocks[-1]['at_ms'])+int(blocks[-1]['frames'])/48
 report['sourceSilentPacketsWithinOutput']=int(sum(float(row['rms'])<.001 and starts[i]>=begin and ends[i]<=finish for i,row in enumerate(source)))
 report['sourceTraceWindowNote']='All-source counts include capture warmup and shutdown; within-output counts use the encoder PCM trace window.'
 missing=eventual=interior_missing=interior_eventual=mismatches=0
 for b in blocks:
  at=float(b['at_ms']);count=int(b['frames']);encoded=float(b['encoded_ms']);index=int(b['sample_index']);covered=np.zeros(count,dtype=bool);expected=np.zeros((count,2),dtype='<i2')
  for j in range(max(0,np.searchsorted(starts,at-100)),np.searchsorted(starts,at+count/48+1)):
   offset=round((starts[j]-at)*48);a=max(0,offset);z=min(count,offset+int(frames[j]))
   if z<=a:continue
   covered[a:z]=True
   if arrivals[j]<=encoded:expected[a:z]=raw[offsets[j]+a-offset:offsets[j]+z-offset]
  missing+=int(b['missing_frames']);eventual+=int((~covered).sum())
  if at>max(begin+25,starts[0]+25) and at+count/48<finish-150:
   interior_missing+=int(b['missing_frames']);interior_eventual+=int((~covered).sum());mismatches+=int(np.any(expected!=pre[index:index+count],axis=1).sum())
 report.update(encodedMissingMs=missing/48,eventuallyUncoveredMs=eventual/48,interiorMissingMs=interior_missing/48,interiorEventuallyUncoveredMs=interior_eventual/48,interiorSourceCopyMismatchedFrames=mismatches,outputOriginMs=begin)
 return report

def analyze(directory):
 directory=pathlib.Path(directory);result=json.loads((directory/'result.json').read_text());report={'case':directory.name,'width':result['width'],'height':result['height'],'fps':result['fps'],'mode':result['mode'],'durationRequestedMs':result['durationRequestedMs'],'workloadStatus':result['status'],'harnessValid':False,'qualityPassed':False,'issues':[]}
 try:
  assert result['status']=='completed; media validation pending',result.get('error',result['status'])
  media=directory/('recording.mkv' if result['mode']=='record' else 'receiver.flv')
  probe_data=subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(media)]);probe=json.loads(probe_data);(directory/'probe.json').write_bytes(probe_data)
  video=next(s for s in probe['streams'] if s['codec_type']=='video');assert (video['width'],video['height'])==(result['width'],result['height']);assert video['codec_name']=='h264'
  timestamps,markers,audio_clock,actions,pixels=common.read_frames(media,directory)
  confidence=float(np.mean((pixels[:,:20]<80)|(pixels[:,:20]>180)));assert confidence>.99,'Marker decoding confidence failed'
  selected=(markers>=result['activeBaseline']['marker'])&(markers<=result['afterCapture']['marker']);assert selected.sum()>20,'No active workload frames'
  if result['durationRequestedMs']>=30000:
   states=actions[selected].tolist();cursor=0
   for state in ([0,1,3,7,15] if result.get('inputDwellMs') else [0,7,15]):
    if state not in states[cursor:]:report['issues'].append(f'Recorded input-state sequence missing/out of order: {state}');break
    cursor=states.index(state,cursor)+1
  audio=common.decode_audio(media,directory,probe,timestamps,audio_clock);assert audio['present'] and audio['silence']['activeBins']>100,'Continuous source audio missing'
  rms=np.load(directory/'audio-rms.npz')['rms'];audio['activeWindow']=common.active_audio_summary(rms,timestamps[selected],float(audio['stream'].get('start_time',0)))
  movement=common.motion(markers[selected],timestamps[selected]);status=result['finalOutput'];resources=common.resources(directory,result)
  private=json.loads((directory/'resource-series.json').read_text())['privateMemory']
  resources['ussPeakMiB']=max((r['ussMiB'] for r in private),default=None)
  settled=[r for r in private if r['atSeconds']>=120]
  resources['ussSlopeMiBPerMinuteAfterWarmup']=float(np.polyfit([r['atSeconds']/60 for r in settled],[r['ussMiB'] for r in settled],1)[0]) if len(settled)>=5 else None
  native=[json.loads(line)['output'] for line in (directory/'status.jsonl').read_text().splitlines()]
  resources['nativeQueuedVideoPeakMiB']=max((s['queuedVideoBytes']/1024**2 for s in native),default=None)
  resources['nativeQueuedAudioPeakMiB']=max((s['queuedAudioBytes']/1024**2 for s in native),default=None)
  resources['nativeAdaptiveBufferInitialMs']=native[0]['bufferedDelayMs'] if native else None
  resources['nativeAdaptiveBufferMaximumMs']=max((s['bufferedDelayMs'] for s in native),default=None)
  sampled=[json.loads(line) for line in (directory/'resources.jsonl').read_text().splitlines()]
  host=[r for r in sampled if 'host' in r and result['activeStartMonotonic']<=r['monotonic']<=result['activeEndMonotonic']]
  def counters(row,key):return {k:int(v) for k,v in (line.split() for line in row['host'].get(key,'').splitlines())}
  if len(host)>1:
   before,after=counters(host[0],'memory.events'),counters(host[-1],'memory.events');cpu_before,cpu_after=counters(host[0],'cpu.stat'),counters(host[-1],'cpu.stat')
   resources['hostMemoryEventsDelta']={k:after[k]-before[k] for k in before};resources['hostQuotaThrottledPeriodsDelta']=cpu_after.get('nr_throttled',0)-cpu_before.get('nr_throttled',0)
   resources['hostCgroupPeakSampledMemoryMiB']=max(int(r['host'].get('memory.current','0')) for r in host)/1024**2
  (directory/'resource-summary.json').write_text(json.dumps(resources,indent=2)+'\n')
  if audio['activeWindow']['quietTotalMs']:report['issues'].append('Silence in continuous source')
  if audio['activeWindow']['missingCoverageMs']>60:report['issues'].append('Missing audio coverage')
  if movement['maxHoldMs']>2*1000/result['fps']+1:report['issues'].append('Video marker held beyond two frame intervals')
  if movement['durationSeconds']*1000<result['durationRequestedMs']-100:report['issues'].append('Video does not cover the requested active duration')
  if movement['maxPtsGapMs']>1.5*1000/result['fps']+1:report['issues'].append('Video output PTS gap exceeds one and a half frame intervals')
  if movement['nonIncreasingPts'] or movement['markerRegressions']:report['issues'].append('Invalid video timeline')
  sync=audio['sync'];limit=1000/result['fps']+10
  if sync['acceptedAnchors']==0:report['issues'].append('No accepted content-clock alignment anchors')
  elif sync['maximumAbsoluteMs']>limit:report['issues'].append('Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution')
  audio['sync']['investigationThresholdMs']=limit
  if status['droppedAudioSamples'] or status['lateAudioSamples']:report['issues'].append('Native audio dropped/arrived after publication')
  if status['queuedAudioBytes'] or status['queuedVideoBytes']:report['issues'].append('Stop retained queued buffers')
  report.update(harnessValid=True,qualityPassed=not report['issues'],mediaBytes=media.stat().st_size,mediaDurationSeconds=float(probe['format']['duration']),markerConfidence=confidence,motion=movement,fullOutputMotion=common.motion(markers,timestamps),capturedActionStates=sorted(set(actions.tolist())),audio=audio,resources=resources,finalOutput=status,trace=trace(directory))
 except Exception as error:report['error']=repr(error)
 (directory/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report.get(k) for k in ['case','harnessValid','qualityPassed','issues','error']}));return report

if __name__=='__main__':
 results=[analyze(p) for p in sys.argv[1:]];sys.exit(0 if all(r['harnessValid'] for r in results) else 1)
