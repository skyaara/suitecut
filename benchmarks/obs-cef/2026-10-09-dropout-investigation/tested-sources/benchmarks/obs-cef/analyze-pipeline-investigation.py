"""Same complete-content/audio/sync checks for a separately labeled diagnostic run."""
import json,subprocess,sys
from pathlib import Path
import numpy as np
OUT=Path(sys.argv[1]);obs=json.loads((OUT/'result.json').read_text());end=obs['requestedMs']/1000-5
assert obs.get('completedAt') and obs.get('activeMs',0)>=obs['requestedMs']*.99
def run(args):return subprocess.check_output(args)
def audio(directory,media,existing=None):
 pcm=directory/'full-audio-8k.f32'
 if existing and existing.exists():pcm=existing
 else:subprocess.run(['ffmpeg','-v','error','-i',media,'-vn','-ac','1','-ar','8000','-f','f32le','-y',str(pcm)],check=True)
 data=np.fromfile(pcm,dtype='<f4');blocks=data[:len(data)//160*160].reshape(-1,160);rms=np.sqrt(np.mean(blocks.astype(np.float64)**2,axis=1));audible=rms>.015;gaps=[];start=None;last=min(len(rms),int(end/.02))
 for i in range(150,last):
  if not audible[i] and start is None:start=i
  if audible[i] and start is not None:gaps.append({'startSeconds':start*.02,'endSeconds':i*.02,'durationSeconds':(i-start)*.02});start=None
 if start is not None:gaps.append({'startSeconds':start*.02,'endSeconds':last*.02,'durationSeconds':(last-start)*.02})
 (directory/'audio-rms.json').write_text(json.dumps({'blockMs':20,'rms':rms.tolist()}))
 return {'status':'failed' if any(x['durationSeconds']>5 for x in gaps) else 'passed','decodedSeconds':len(data)/8000,'pcmPath':str(pcm),'longestSilentInterval':max(gaps,key=lambda x:x['durationSeconds']) if gaps else None,'silentIntervalsOver2Seconds':[x for x in gaps if x['durationSeconds']>2],'silentIntervalsOver5Seconds':[x for x in gaps if x['durationSeconds']>5]}
def video(directory,media,probe):
 frames=json.loads(run(['ffprobe','-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time,pkt_duration_time','-of','json',media]));(directory/'frame-timestamps.json').write_text(json.dumps(frames));pts=[float(x['best_effort_timestamp_time']) for x in frames['frames']]
 raw=run(['ffmpeg','-v','error','-threads','2','-i',media,'-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-vsync','0','-f','rawvideo','pipe:1'])
 assert len(raw)%20==0
 ids=[sum(1<<bit for bit in range(20) if raw[n+bit]>128) for n in range(0,len(raw),20)];assert len(pts)==len(ids);assert all(b>a for a,b in zip(pts,pts[1:]));(directory/'marker-ids.json').write_text(json.dumps(ids))
 holds=[];start=0;duration=float(probe['format']['duration'])
 for i in range(1,len(ids)+1):
  if i==len(ids) or ids[i]!=ids[start]:
   left=max(3,pts[start]);right=min(end,pts[i] if i<len(ids) else duration)
   if right>left:holds.append({'startSeconds':left,'endSeconds':right,'durationSeconds':right-left,'marker':ids[start],'encodedFrames':i-start})
   start=i
 gaps=[b-a for a,b in zip(pts,pts[1:])];active=[(t,v) for t,v in zip(pts,ids) if 3<=t<end];transitions=sum(i==0 or v!=active[i-1][1] for i,(t,v) in enumerate(active));longest=max(holds,key=lambda x:x['durationSeconds']);seconds=end-3
 return {'status':'failed' if longest['durationSeconds']>5 else 'passed','decodedFrames':len(ids),'activeFrames':len(active),'distinctActiveFps':transitions/seconds,'repeatFraction':1-transitions/len(active),'longestContentHold':longest,'contentHoldsOver1Second':[x for x in holds if x['durationSeconds']>1],'contentHoldsOver5Seconds':[x for x in holds if x['durationSeconds']>5],'maximumPresentationGapSeconds':max(gaps),'markerRange':[min(ids),max(ids)]}
def sync(directory,media):
 windows=[]
 for offset in [5,int(obs['requestedMs']/2000),int(obs['requestedMs']/1000)-20]:
  d=directory/f'sync-{offset}';d.mkdir(exist_ok=True)
  subprocess.run(['ffmpeg','-v','error','-threads','2','-ss',str(offset),'-i',media,'-t','15','-an','-vf','crop=64:64:400:0,scale=1:1,fps=60:start_time=0,format=gray','-f','rawvideo','-y',str(d/'flashes.gray')],check=True)
  subprocess.run(['ffmpeg','-v','error','-ss',str(offset),'-i',media,'-t','15','-vn','-ac','1','-ar','48000','-f','f32le','-y',str(d/'audio.f32')],check=True)
  details=json.loads(run(['python3','benchmarks/obs-cef/analyze-sync.py',str(d/'flashes.gray'),str(d/'audio.f32')]))
  details.update(offsetSeconds=offset,status='passed' if details['matchedFlashes']>=5 and details['p95AbsoluteOffsetMs']<200 else 'failed');(d/'sync.json').write_text(json.dumps(details,indent=2));windows.append(details)
 return windows
summary={'requestedMs':obs['requestedMs'],'injection':obs.get('injection'),'thresholds':{'audibleRms':.015,'rmsBlockMs':20,'silenceFailureSeconds':5,'videoContentHoldFailureSeconds':5,'syncP95Ms':200},'outputs':{}}
for name in ['recording','receiver']:
 directory=OUT/'analysis'/name;directory.mkdir(parents=True,exist_ok=True)
 media=obs[name+'Path'];probe=json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',media]));(directory/'probe.json').write_text(json.dumps(probe,indent=2))
 row={'mediaPath':media,'durationSeconds':float(probe['format']['duration'])};assert row['durationSeconds']>=obs['requestedMs']/1000*.95
 print('Analyzing',name,flush=True);row['audio']=audio(directory,media);row['video']=video(directory,media,probe);row['syncWindows']=sync(directory,media)
 row['status']='passed' if row['audio']['status']=='passed' and row['video']['status']=='passed' and all(w['status']=='passed' for w in row['syncWindows']) else 'failed'
 summary['outputs'][name]=row;(OUT/'media-validation.json').write_text(json.dumps(summary,indent=2))
summary['status']='passed' if all(r['status']=='passed' for r in summary['outputs'].values()) else 'failed';(OUT/'media-validation.json').write_text(json.dumps(summary,indent=2));print(json.dumps({'status':summary['status']}))
