"""Decode complete long-run audio and retain periodic-tone silence diagnostics."""
import json,subprocess,sys
from pathlib import Path
import numpy as np
label=sys.argv[1] if len(sys.argv)>1 and not sys.argv[1].startswith('--') else 'soak'
diagnostic='--diagnostic' in sys.argv
out=Path('benchmarks/obs-cef/2026-10-09-optimization')/label;result=json.loads((out/'result.json').read_text())
if not diagnostic:assert result['status']=='passed'
summary={}
for name in ['recording','receiver']:
 pcm=out/(name+'-full-audio-8k.f32')
 subprocess.run(['ffmpeg','-v','error','-i',result[name+'Path'],'-vn','-ac','1','-ar','8000','-f','f32le','-y',str(pcm)],check=True)
 data=np.fromfile(pcm,dtype='<f4');size=160;blocks=data[:len(data)//size*size].reshape(-1,size);rms=np.sqrt(np.mean(blocks.astype(np.float64)**2,axis=1));audible=rms>.015
 # A one-second pulse fixture naturally has silent intervals. Analyze only
 # after startup and before trailing finalization; preserve all longer gaps.
 start=3;end=result['requestedMs']/1000-5;first=int(start/.02);last=min(len(rms),int(end/.02));gaps=[];run=None
 for i in range(first,last):
  if not audible[i] and run is None:run=i
  if audible[i] and run is not None:
   gaps.append({'startSeconds':run*.02,'endSeconds':i*.02,'durationSeconds':(i-run)*.02});run=None
 if run is not None:gaps.append({'startSeconds':run*.02,'endSeconds':last*.02,'durationSeconds':(last-run)*.02})
 longest=max(gaps,key=lambda r:r['durationSeconds']) if gaps else None
 minute_rows=[]
 for offset in range(0,int(end),60):
  window=rms[int(offset/.02):int((offset+60)/.02)]
  minute_rows.append({'startSeconds':offset,'rms':float(np.sqrt(np.mean(window**2))),'audibleFraction':float(np.mean(window>.015))})
 row={'decodedSeconds':len(data)/8000,'analyzedStartSeconds':start,'analyzedEndSeconds':end,'rmsBlockMs':20,'audibleRmsThreshold':.015,'longestSilentInterval':longest,'silentIntervalsOver2Seconds':[g for g in gaps if g['durationSeconds']>2],'silentIntervalsOver5Seconds':[g for g in gaps if g['durationSeconds']>5],'minuteMetrics':minute_rows,'status':'passed' if not any(g['durationSeconds']>5 for g in gaps) else 'failed'}
 (out/(name+'-audio-rms.json')).write_text(json.dumps({'blockMs':20,'rms':rms.tolist()}));summary[name]=row
(out/'audio-continuity-validation.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary))
if not diagnostic:assert all(row['status']=='passed' for row in summary.values()),'Periodic audio has a silent interval exceeding five seconds; inspect preserved PCM and RMS traces.'
