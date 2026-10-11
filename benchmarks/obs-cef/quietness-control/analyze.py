"""Shared decoder/oracle; continuous audio applies to OBS recording AND streaming."""
import csv,importlib.util,json,pathlib,re,subprocess,sys
import numpy as np
root=pathlib.Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('publication_analysis',root/'benchmarks/publication/analyze.py');common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
def analyze(directory):
 directory=pathlib.Path(directory);result=json.loads((directory/'result.json').read_text());report={'case':directory.name,'backend':'obs-cef','width':result['width'],'height':result['height'],'fps':result['fps'],'mode':result['mode'],'durationRequestedMs':result['durationRequestedMs'],'workloadStatus':result['status'],'harnessValid':False,'qualityPassed':False,'issues':[]}
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
   for state in [0,7,15]:
    if state not in states[cursor:]:report['issues'].append(f'Recorded input-state sequence missing/out of order: {state}');break
    cursor=states.index(state,cursor)+1
  audio=common.decode_audio(media,directory,probe,timestamps,audio_clock);assert audio['present'] and audio['silence']['activeBins']>100,'Continuous source audio missing'
  assert audio['stream']['codec_name']=='aac';assert int(audio['stream']['sample_rate'])==48000;assert audio['stream']['channels']==2
  rms=np.load(directory/'audio-rms.npz')['rms'];audio_start=float(audio['stream'].get('start_time',0));audio['activeWindow']=common.active_audio_summary(rms,timestamps[selected],audio_start)
  step=.005;frame_step=float(np.median(np.diff(timestamps[selected])));begin=max(0,int(np.ceil((timestamps[selected][0]-audio_start)/step)));end=min(len(rms),int(np.floor((timestamps[selected][-1]+frame_step-audio_start)/step)))
  q=rms[begin:end]<.001;edges=np.diff(np.r_[False,q,False].astype(np.int8));starts=np.flatnonzero(edges==1);ends=np.flatnonzero(edges==-1)
  intervals=[{'outputStartMs':float(audio_start*1000+(begin+a)*5),'activeStartMs':int(a*5),'durationMs':int((z-a)*5)} for a,z in zip(starts,ends)]
  assert sum(i['durationMs'] for i in intervals)==audio['activeWindow']['quietTotalMs']
  with (directory/'quiet-intervals.csv').open('w') as f:
   w=csv.DictWriter(f,fieldnames=['outputStartMs','activeStartMs','durationMs']);w.writeheader();w.writerows(intervals)
  audio['activeWindow'].update(intervalCount=len(intervals),quietFraction=audio['activeWindow']['quietTotalMs']/1000/audio['activeWindow']['observedAudioSeconds'],quietMsPerMinute=audio['activeWindow']['quietTotalMs']/(audio['activeWindow']['observedAudioSeconds']/60))
  movement=common.motion(markers[selected],timestamps[selected]);resource=common.resources(directory,result)
  private=json.loads((directory/'resource-series.json').read_text())['privateMemory'];resource['ussPeakMiB']=max((r['ussMiB'] for r in private),default=None);settled=[r for r in private if r['atSeconds']>=120]
  resource['ussSlopeMiBPerMinuteAfterWarmup']=float(np.polyfit([r['atSeconds']/60 for r in settled],[r['ussMiB'] for r in settled],1)[0]) if len(settled)>=5 else None
  sampled=[json.loads(line) for line in (directory/'resources.jsonl').read_text().splitlines()];host=[r for r in sampled if 'host' in r and result['activeStartMonotonic']<=r['monotonic']<=result['activeEndMonotonic']]
  def counters(row,key):return {k:int(v) for k,v in (line.split() for line in row['host'].get(key,'').splitlines())}
  if len(host)>1:
   before,after=counters(host[0],'memory.events'),counters(host[-1],'memory.events');cpu_before,cpu_after=counters(host[0],'cpu.stat'),counters(host[-1],'cpu.stat');resource['hostMemoryEventsDelta']={k:after[k]-before[k] for k in before};resource['hostQuotaThrottledPeriodsDelta']=cpu_after.get('nr_throttled',0)-cpu_before.get('nr_throttled',0);resource['hostCgroupPeakSampledMemoryMiB']=max(int(r['host'].get('memory.current','0')) for r in host)/1024**2
  (directory/'resource-summary.json').write_text(json.dumps(resource,indent=2)+'\n')
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
  lines=(directory/'obs.log').read_text(errors='replace').splitlines();audio_lines=[l for l in lines if re.search(r'audio buffering|audio.*lagged|Max audio|audio.*timestamp|audio.*leak',l,re.I)]
  (directory/'obs-audio-events.txt').write_text('\n'.join(audio_lines)+'\n')
  report.update(harnessValid=True,qualityPassed=not report['issues'],mediaBytes=media.stat().st_size,mediaDurationSeconds=float(probe['format']['duration']),markerConfidence=confidence,motion=movement,fullOutputMotion=common.motion(markers,timestamps),capturedActionStates=sorted(set(actions.tolist())),audio=audio,resources=resource,obsBefore=result.get('obsBefore'),obsAfter=result.get('obsAfter'),obsAudioEvents=audio_lines)
 except Exception as error:report['error']=repr(error)
 (directory/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report.get(k) for k in ['case','harnessValid','qualityPassed','issues','error']}),flush=True);return report
if __name__=='__main__':
 reports=[analyze(p) for p in sys.argv[1:]];sys.exit(0 if all(r['harnessValid'] for r in reports) else 1)
