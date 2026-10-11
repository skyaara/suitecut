"""Inspect retained partial artifacts without changing failed trial status."""
import json, subprocess
from pathlib import Path

root=Path('benchmarks/obs-cef/2026-10-09')
media=Path('.suitecut/obs-cef-evidence/2026-10-09')
rows=json.loads((root/'trials.json').read_text())
observations=[]
for row in rows:
    if row['status']!='failed':continue
    directory=root/'trials'/row['id']
    observation={'id':row['id'],'trialStatus':'failed','artifacts':[]}
    for path in (media/row['id']).glob('*.webm'):
        item={'path':str(path.resolve()),'bytes':path.stat().st_size}
        try:
            result=subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)],capture_output=True,text=True,timeout=15)
            item['probeExitCode']=result.returncode
            if result.returncode==0:
                item['probe']=json.loads(result.stdout)
                (directory/'partial-probe.json').write_text(result.stdout)
                decode=subprocess.run(['ffmpeg','-v','error','-threads','2','-i',str(path),'-frames:v','1','-y',str(directory/'partial-frame.png')],capture_output=True,text=True,timeout=15)
                item['frameDecodeExitCode']=decode.returncode
                if decode.returncode:item['decodeError']=decode.stderr
            else:item['probeError']=result.stderr
        except Exception as error:item['inspectionError']=str(error)
        observation['artifacts'].append(item)
    (directory/'failure-artifacts.json').write_text(json.dumps(observation,indent=2)+'\n')
    observations.append(observation)
(root/'failure-artifacts.json').write_text(json.dumps(observations,indent=2)+'\n')
print(json.dumps({'failedTrialsInspected':len(observations)}))
