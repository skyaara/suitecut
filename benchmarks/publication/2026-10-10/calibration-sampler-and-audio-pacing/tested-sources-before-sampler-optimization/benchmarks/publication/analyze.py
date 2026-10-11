"""Analyze actual decoded media and bounded process samples after capture."""
import gzip,json,math,pathlib,subprocess,sys
import numpy as np
from scipy.signal import correlate


def silence_summary(rms, resolution_ms=5):
 quiet=np.asarray(rms)<.001;active=np.flatnonzero(~quiet);intervals=[];begin=None
 for i,value in enumerate(np.append(quiet,False)):
  if value and begin is None:begin=i
  if not value and begin is not None:
   intervals.append({'fromMs':int(begin*resolution_ms),'durationMs':int((i-begin)*resolution_ms),'interior':bool(len(active) and begin>active[0] and i<=active[-1])});begin=None
 inside=[x for x in intervals if x['interior']]
 return {'thresholdRms':.001,'resolutionMs':resolution_ms,'activeBins':len(active),'leadingMs':int(active[0]*resolution_ms) if len(active) else len(quiet)*resolution_ms,'trailingMs':int((len(quiet)-1-active[-1])*resolution_ms) if len(active) else 0,'interiorTotalMs':sum(x['durationMs'] for x in inside),'interiorMaximumMs':max((x['durationMs'] for x in inside),default=0),'intervals':intervals}


def motion(ids, timestamps):
 ids=np.asarray(ids,dtype=np.int64);ts=np.asarray(timestamps,dtype=float)
 if len(ids)<2:return {'frames':len(ids),'distinctFps':None,'maxHoldMs':None,'repeatedFraction':None}
 changed=np.diff(ids)!=0;dt=np.diff(ts);boundaries=np.r_[0,np.flatnonzero(changed)+1,len(ids)]
 holds=[ts[min(b,len(ts)-1)]-ts[a]+(np.median(dt) if b==len(ids) else 0) for a,b in zip(boundaries[:-1],boundaries[1:])]
 return {'frames':len(ids),'durationSeconds':float(ts[-1]-ts[0]),'distinctFps':float(changed.sum()/(ts[-1]-ts[0])) if ts[-1]>ts[0] else None,'repeatedFraction':float(1-changed.mean()),'maxHoldMs':float(max(holds)*1000),'maxPtsGapMs':float(max(dt)*1000),'nonIncreasingPts':int((dt<=0).sum()),'markerRegressions':int((np.diff(ids)<0).sum()),'missedLogicalTicks':int(np.maximum(0,np.diff(ids)-1).sum())}


def wave_reference(start,count):
 frames=np.arange(start,start+count,dtype=np.int64);n=(frames//24).astype(np.uint32)
 def code(n):
  x=n+np.uint32(0x9e3779b9);x=(x^(x>>16))*np.uint32(0x7feb352d);x=(x^(x>>15))*np.uint32(0x846ca68b);x=x^(x>>16)
  return (x.astype(np.float64)/4294967296*2-1)*.15
 f=(frames%24)/24
 return code(n)*(1-f)+code(n+np.uint32(1))*f


def audio_anchor(values,predicted,radius=48000):
 values=np.asarray(values,dtype=np.float64)
 if np.sqrt(np.mean(values**2))<.001:return None
 start=max(0,int(predicted)-radius);reference=wave_reference(start,2*radius+len(values))
 signal=reference[::6];needle=values[::6];dots=correlate(signal,needle,mode='valid',method='fft')
 sums=np.r_[0,np.cumsum(signal**2)];energy=sums[len(needle):]-sums[:-len(needle)]
 scores=dots/np.sqrt(np.maximum(energy*np.dot(needle,needle),1e-30));peak=int(np.argmax(scores));position=peak*6
 norm=np.sqrt(np.dot(values,values));best=(-1,None)
 for index in range(max(0,position-7),min(len(reference)-len(values),position+7)+1):
  segment=reference[index:index+len(values)];score=float(np.dot(segment,values)/(np.sqrt(np.dot(segment,segment))*norm))
  if score>best[0]:best=(score,start+index)
 return {'frame':best[1],'correlation':best[0]}


def read_frames(file,directory):
 cmd=['ffprobe','-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time,pkt_duration_time','-of','json',str(file)]
 data=subprocess.check_output(cmd)
 with gzip.open(directory/'video-frames.json.gz','wb') as f:f.write(data)
 timestamps=np.array([float(frame['best_effort_timestamp_time']) for frame in json.loads(data)['frames'] if 'best_effort_timestamp_time' in frame])
 cmd=['ffmpeg','-v','error','-threads','2','-i',str(file),'-an','-filter_threads','1','-vf','crop=864:32:0:0,scale=54:1:flags=neighbor,format=gray','-fps_mode','passthrough','-f','rawvideo','pipe:1']
 decoded=subprocess.check_output(cmd);pixels=np.frombuffer(decoded,dtype=np.uint8)
 assert len(pixels)%54==0,'Truncated marker frame'
 pixels=pixels.reshape(-1,54);assert len(pixels)==len(timestamps),'Decoded frame / timestamp count differs'
 bits=(pixels>180).astype(np.int64)
 marker=bits[:,:20]@(1<<np.arange(20));audio=bits[:,30:50]@(1<<np.arange(20));actions=bits[:,50:54]@(1<<np.arange(4))
 np.savez_compressed(directory/'decoded-markers.npz',timestamps=timestamps,markers=marker,audioClockTicks=audio,actions=actions,pixels=pixels)
 return timestamps,marker,audio,actions,pixels


def decode_audio(file,directory,probe,timestamps,audio_clock):
 streams=[s for s in probe['streams'] if s['codec_type']=='audio']
 if not streams:return {'present':False}
 stream=streams[0];audio_start=float(stream.get('start_time',probe['format'].get('start_time',0)));rms=[];anchors=[];offset=0;next_anchor=48000
 child=subprocess.Popen(['ffmpeg','-v','error','-threads','2','-i',str(file),'-vn','-ac','1','-ar','48000','-f','f32le','pipe:1'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 pending=b''
 while True:
  data=child.stdout.read(96000*4)
  if not data:break
  values=np.frombuffer(pending+data,dtype='<f4');count=(len(values)//240)*240
  rms.extend(np.sqrt(np.mean(values[:count].reshape(-1,240).astype(np.float64)**2,axis=1)).tolist())
  pending=values[count:].tobytes()
  if offset<=next_anchor and next_anchor+4800<=offset+count:
   index=next_anchor-offset;values_anchor=values[index:index+4800]
   output_time=audio_start+next_anchor/48000
   frame=int(np.argmin(abs(timestamps-output_time)));video_time=audio_clock[frame]/100
   anchor=audio_anchor(values_anchor,video_time*48000)
   anchors.append({'outputTime':output_time,'videoClockTime':float(video_time),'videoPts':float(timestamps[frame]),'anchor':anchor,'audioMinusVideoMs':None if anchor is None or anchor['correlation']<.85 else (anchor['frame']/48000-video_time)*1000})
   next_anchor+=30*48000
  offset+=count
 error=child.stderr.read().decode();assert child.wait()==0,error
 np.savez_compressed(directory/'audio-rms.npz',rms=np.asarray(rms,dtype=np.float32),resolutionMs=5)
 valid=[a['audioMinusVideoMs'] for a in anchors if a['audioMinusVideoMs'] is not None]
 return {'present':True,'stream':stream,'decodedSamples':offset,'silence':silence_summary(rms),'anchors':anchors,'sync':{'method':'Known generated sample content versus encoded AudioContext clock marker; nearest video frame','acceptedAnchors':len(valid),'totalAnchors':len(anchors),'correlationThreshold':.85,'medianMs':float(np.median(valid)) if valid else None,'maximumAbsoluteMs':float(max(map(abs,valid))) if valid else None,'quantization':'10 ms marker plus nearest output video frame; excludes display/network latency'}}


def resources(directory,result):
 raw=[json.loads(line) for line in (directory/'resources.jsonl').read_text().splitlines()]
 active=[r for r in raw if result.get('activeStartMonotonic',math.inf)<=r['monotonic']<=result.get('activeEndMonotonic',-math.inf)]
 assert len(active)>=result['durationRequestedMs']/1000*1.5,'Resource samples cover less than 75% of the expected active intervals'
 excluded=['receiver','display','audio-server'];series=[];by_role={};pss=[]
 for r in active:
  product=[p for p in r['processes'] if p['role'] not in excluded]
  cpu=sum(p['cpuCores'] or 0 for p in product);rss=sum(p['rssBytes'] for p in product)
  series.append({'atSeconds':r['monotonic']-result['activeStartMonotonic'],'cpuCores':cpu,'rssMiB':rss/1024**2,'threads':sum(p['threads'] for p in product),'processes':len(product),'nodeHeapMiB':None})
  for p in product:
   by_role.setdefault(p['role'],[]).append(p['cpuCores'] or 0)
  if product and all(p.get('pssBytes') is not None for p in product):
   pss.append({'atSeconds':series[-1]['atSeconds'],'pssMiB':sum(p['pssBytes'] for p in product)/1024**2,'ussMiB':sum(p['ussBytes'] for p in product)/1024**2})
 node=[json.loads(line) for line in (directory/'status.jsonl').read_text().splitlines()]
 def slope(rows,key):
  selected=[row for row in rows if row['atSeconds']>=120 and row.get(key) is not None]
  if len(selected)<5:return None
  return float(np.polyfit([x['atSeconds']/60 for x in selected],[x[key] for x in selected],1)[0])
 node_series=[{'atSeconds':r['atMs']/1000,'heapMiB':r['nodeMemory']['heapUsed']/1024**2,'externalMiB':r['nodeMemory']['external']/1024**2,'arrayBuffersMiB':r['nodeMemory']['arrayBuffers']/1024**2} for r in node]
 roles={}
 for role in sorted(by_role):
  values=[sum(p['cpuCores'] or 0 for p in r['processes'] if p['role']==role) for r in active]
  roles[role]=float(np.mean(values)) if values else None
 summary={'activeSamples':len(active),'boundary':'Controller, capture/browser children and encoders; receiver, display and Pulse reported separately/excluded','cpuMeanCores':float(np.mean([x['cpuCores'] for x in series])) if series else None,'cpuP95Cores':float(np.percentile([x['cpuCores'] for x in series],95)) if series else None,'rssPeakMiB':max((x['rssMiB'] for x in series),default=None),'rssMeanMiB':float(np.mean([x['rssMiB'] for x in series])) if series else None,'rssSlopeMiBPerMinuteAfterWarmup':slope(series,'rssMiB'),'pssCompleteSamples':len(pss),'pssPeakMiB':max((x['pssMiB'] for x in pss),default=None),'pssSlopeMiBPerMinuteAfterWarmup':slope(pss,'pssMiB'),'nodeHeapSlopeMiBPerMinuteAfterWarmup':slope(node_series,'heapMiB'),'nodeExternalSlopeMiBPerMinuteAfterWarmup':slope(node_series,'externalMiB'),'cpuMeanByRole':roles,'samplerDurationP95Ms':float(np.percentile([r['sampleDurationMs'] for r in active],95)) if active else None,'memoryGuardExceeded':any(r['memoryGuardExceeded'] for r in raw)}
 (directory/'resource-summary.json').write_text(json.dumps(summary,indent=2)+'\n');(directory/'resource-series.json').write_text(json.dumps({'series':series,'privateMemory':pss,'nodeMemory':node_series},indent=2)+'\n')
 return summary


def analyze(directory):
 directory=pathlib.Path(directory);result=json.loads((directory/'result.json').read_text());report={'backend':result['backend'],'mode':result['mode'],'workloadStatus':result['status'],'valid':False,'mediaDecoded':False,'validationIssues':[]}
 try:
  assert result['status']=='completed; media validation pending',result.get('error',result['status'])
  file=pathlib.Path(result['recordingPath'] if result['mode']=='record' else result['receiverPath'])
  if not file.exists():file=directory/file.name
  data=subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(file)]);probe=json.loads(data);(directory/'probe.json').write_bytes(data)
  video=next(s for s in probe['streams'] if s['codec_type']=='video');assert (video['width'],video['height'])==(1920,1080),'Wrong dimensions';assert video['codec_name']=='h264','Wrong normalized recording/streaming codec'
  timestamps,markers,audio_clock,actions,pixels=read_frames(file,directory)
  assert len(markers)>20,'No decoded motion frames';assert len(set(markers))>10,'No visible marker motion'
  confidence=np.mean((pixels[:,:20]<80)|(pixels[:,:20]>180));assert confidence>.99,'Marker location/color confidence failed; screen crop may be wrong'
  before=result.get('activeBaseline',result['beforeCapture'])['marker'];after=result['afterCapture']['marker'];selected=(markers>=before)&(markers<=after)
  assert selected.sum()>20,'No captured frames from the active workload'
  if result['durationRequestedMs']>=30000:
   states=actions[selected].tolist();cursor=0
   for state in [0,1,3,7,15]:
    if state not in states[cursor:]:report['validationIssues'].append(f'Recorded input-state sequence missing/out of order: {state}');break
    cursor=states.index(state,cursor)+1
  audio=decode_audio(file,directory,probe,timestamps,audio_clock)
  if result['mode']=='stream':
   if not audio['present'] or audio['silence']['activeBins']<=100:report['validationIssues'].append('Expected audible streaming track missing')
   elif audio['silence']['interiorTotalMs']>0:report['validationIssues'].append('Unexpected interior audio silence in continuous signal')
  if result['mode']=='record':assert audio['present'] and audio['silence']['activeBins']==0,'Recording fixture must be silent with a matched AAC track'
  report.update(valid=not report['validationIssues'],mediaDecoded=True,mediaBytes=file.stat().st_size,mediaDurationSeconds=float(probe['format']['duration']),video=video,markerConfidence=float(confidence),motion=motion(markers[selected],timestamps[selected]),fullOutputMotion=motion(markers,timestamps),capturedActionStates=sorted(set(actions.tolist())),audio=audio,resources=resources(directory,result))
 except Exception as error:report['error']=repr(error)
 (directory/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
 return report

if __name__=='__main__':
 report=analyze(sys.argv[1]);print(json.dumps({'valid':report['valid'],'error':report.get('error'),'backend':report['backend'],'mode':report['mode']}));sys.exit(0 if report['valid'] else 1)
