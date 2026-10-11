"""Collect all long-run media windows even when the first sync assertion failed."""
import json,subprocess
from pathlib import Path
out=Path('benchmarks/obs-cef/2026-10-09-optimization/soak');r=json.loads((out/'result.json').read_text());summary={'originalStatus':r['status'],'originalError':r.get('error'),'activeWorkloadCompleted':r.get('activeMs',0)>=r['requestedMs']*.99,'media':{}}
assert summary['activeWorkloadCompleted']
for name in ['recording','receiver']:
 rows=[]
 for offset in [5,900,1780]:
  directory=out/f'{name}-{offset}';directory.mkdir(exist_ok=True);audio=directory/'audio.f32';video=directory/'flashes.gray'
  if not video.exists():subprocess.run(['ffmpeg','-v','error','-threads','2','-ss',str(offset),'-i',r[name+'Path'],'-t','15','-an','-vf','crop=64:64:400:0,scale=1:1,fps=60:start_time=0,format=gray','-f','rawvideo','-y',str(video)],check=True)
  if not audio.exists():subprocess.run(['ffmpeg','-v','error','-ss',str(offset),'-i',r[name+'Path'],'-t','15','-vn','-ac','1','-ar','48000','-f','f32le','-y',str(audio)],check=True)
  details=json.loads(subprocess.check_output(['python3','benchmarks/obs-cef/analyze-sync.py',str(video),str(audio)]));(directory/'sync.json').write_text(json.dumps(details,indent=2));rows.append({'offset':offset,**details,'status':'passed' if details['matchedFlashes']>=5 and details['p95AbsoluteOffsetMs']<200 else 'failed'})
 probe=json.loads((out/f'{name}-probe.json').read_text());summary['media'][name]={'durationSeconds':float(probe['format']['duration']),'bytes':int(probe['format']['size']),'windows':rows}
summary['allSyncWindowsPassed']=all(w['status']=='passed' for row in summary['media'].values() for w in row['windows']);(out/'complete-media-validation.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary))
