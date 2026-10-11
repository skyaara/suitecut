"""Decode every output; inspect sound/video activity and saved process samples."""
import hashlib,json,pathlib,subprocess,sys
import numpy as np
root=pathlib.Path(sys.argv[1]).resolve()
result=json.loads((root/'result.json').read_text())
native_final_available='recordStatus' in result and 'streamStatus' in result
snapshot=result.get('interruption',{}).get('lastProgress',{})
(root/'receiver.flv.csv').write_bytes((root/'native.csv').read_bytes())
(root/'receiver.flv.s16').write_bytes((root/'native.s16').read_bytes())
cases=[{'audioCodec':'aac','destination':str(root/name),'final':result.get(key,snapshot.get(track)), 'nativeFinalStatusAvailable':native_final_available} for name,key,track in [('recording.mp4','recordStatus','record'),('receiver.flv','streamStatus','stream')]]
(root/'results.json').write_text(json.dumps({'cases':cases},indent=2)+'\n')
subprocess.run([sys.executable,'benchmarks/native-audio/analyze.py',str(root)],check=True,stdout=(root/'attribution.log').open('w'))
analysis=json.loads((root/'analysis.json').read_text());out={'result':result,'audioAttribution':analysis,'nativeFinalStatusesAvailable':native_final_available,'outputs':[]}
for entry,attribution in zip(cases,analysis['cases']):
 path=pathlib.Path(entry['destination'])
 # The binary frame marker and audio-context-driven white/black overlay remain
 # in the existing fixture. Decode all video frames, not a sampled excerpt.
 pixels=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-an','-vf','crop=464:32:0:0,scale=29:1:flags=area,format=gray','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','gray','-'],check=True,capture_output=True).stdout
 pixels=np.frombuffer(pixels,dtype=np.uint8).reshape(-1,29)
 values=(pixels[:,:20]>127).astype(np.uint32)@np.array([1<<i for i in range(20)],dtype=np.uint32)
 boundaries=np.r_[0,np.flatnonzero(values[1:]!=values[:-1])+1,len(values)]
 holds=np.diff(boundaries)
 visible=pixels[:,25:].mean(axis=1)>127
 visual_onsets=np.flatnonzero(visible[1:] & ~visible[:-1])+1
 pre=np.fromfile(path.with_suffix(path.suffix+'.s16'),dtype='<i2').reshape(-1,2).mean(axis=1)/32768
 n=len(pre)//480;rms=np.sqrt(np.mean(pre[:n*480].reshape(n,480)**2,axis=1));audible=rms>.003
 sound_onsets=(np.flatnonzero(audible[1:] & ~audible[:-1])+1)*10
 visual_ms=visual_onsets*1000/30
 onset_errors=[];unmatched=[];one_to_one=[];unmatched_audio=[];unmatched_video=[]
 if 'pulse' in result['audioPattern']:
  for onset in sound_onsets:
   if not 1000<onset<len(pre)/48-1000:continue
   error=float(visual_ms[np.argmin(abs(visual_ms-onset))]-onset) if len(visual_ms) else None
   if error is None or abs(error)>250:unmatched.append({'audioMs':int(onset),'nearestVideoMinusAudioMs':error})
   else:onset_errors.append(error)
  # Also report unique, bidirectional matches. Nearest-neighbor statistics alone
  # can reuse a visible pulse and overlook a pulse missing from either track.
  audio_interior=[float(x) for x in sound_onsets if 1000<x<len(pre)/48-1000]
  video_interior=[float(x) for x in visual_ms if 1000<x<len(pre)/48-1000]
  ai=vi=0
  while ai<len(audio_interior) and vi<len(video_interior):
   error=video_interior[vi]-audio_interior[ai]
   if abs(error)<=250:one_to_one.append(error);ai+=1;vi+=1
   elif error>250:unmatched_audio.append(audio_interior[ai]);ai+=1
   else:unmatched_video.append(video_interior[vi]);vi+=1
  unmatched_audio.extend(audio_interior[ai:]);unmatched_video.extend(video_interior[vi:])
 silence=attribution['decodedSilence10msWindows']
 interior=[g for g in silence if g['startMs']>=1000 and g['endMs']<=len(pre)/48-1000]
 item={'path':str(path),'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),
       'fullyDecodedVideoFrames':len(values),'nativeVideoFrames':entry['final']['videoFrames'] if native_final_available else None,
       'distinctImageFPS':len(holds)/(len(values)/30),'maximumImageHoldMs':float(max(holds)*1000/30),
       'decodedInteriorSilenceMs':sum(g['durationMs'] for g in interior),
       'maximumInteriorSilenceMs':max((g['durationMs'] for g in interior),default=0),
       'decodedSilenceLongerThanExpectedPulseMs':[g for g in interior if g['durationMs']>850],
       'preEncoderPulseOnsets':len(sound_onsets),'visualPulseOnsets':len(visual_onsets),
       'pulseVisualMinusNativeAudioMs':{'samples':len(onset_errors),'median':float(np.median(onset_errors)) if onset_errors else None,
       'p95Absolute':float(np.percentile(np.abs(onset_errors),95)) if onset_errors else None,'maximumAbsolute':max(map(abs,onset_errors),default=None),
       'unmatched':unmatched},'probe':json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)]))}
 item['uniqueBidirectionalPulseMatches']={'samples':len(one_to_one),'p95AbsoluteMs':float(np.percentile(np.abs(one_to_one),95)) if one_to_one else None,'unmatchedAudioOnsetsMs':unmatched_audio,'unmatchedVideoOnsetsMs':unmatched_video,'matchingToleranceMs':250,'caveat':'Coarse content test; missing video frames and multiple onsets from interrupted tones affect detection. This is not a hardware clock measurement.'}
 if native_final_available and len(values)!=entry['final']['videoFrames']:raise RuntimeError('Video frame count mismatch')
 out['outputs'].append(item)
profiles=[json.loads(line) for line in (root/'processes.jsonl').read_text().splitlines()]
start=result['runStartMonotonicSeconds'];profiles=[p for p in profiles if p['monotonic']>=start]
rss=[];role_cpu={}
for p in profiles:
 native=[x for x in p['processes'] if x['role'] not in ['node','sampler','receiver']]
 rss.append((p['monotonic']-start,sum(x['rssBytes'] for x in native)))
 for x in native:role_cpu.setdefault(x['role'],[]).append(x['cpuPercent'] or 0)
active_ms=result.get('activeMs',snapshot.get('atMs',0))
window=min(300,max(5,active_ms/4000))
first=[v for t,v in rss if 10<t<10+window];last=[v for t,v in rss if t>active_ms/1000-window]
out['resources']={'captureSamples':len(profiles),'nativeMeanCPUCores':sum(sum(v) for v in role_cpu.values())/max(1,len(profiles))/100,
 'nativeAggregateRSSPeakMiB':max((v for t,v in rss),default=0)/1048576,
 'aggregateRSSFirstWindowMedianMiB':float(np.median(first))/1048576 if first else None,
 'aggregateRSSLastWindowMedianMiB':float(np.median(last))/1048576 if last else None,
 'rssWindowSeconds':window,'rssIsSummedNotPrivateMemory':True,'wholeCEFLeakFreedomProven':False}
(root/'validation.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'outputs':[{k:v for k,v in o.items() if k not in ['probe','sha256']} for o in out['outputs']], 'resources':out['resources'], 'sourceGapCount':len(analysis['sourceTimelineGaps']), 'maxCallbackDelayMs':analysis['maxCallbackDelayMs']},indent=2))
