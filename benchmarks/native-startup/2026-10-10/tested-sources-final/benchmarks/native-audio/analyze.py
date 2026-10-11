"""Attribute gaps using source packets and pre-encoder coverage, then decode files."""
import csv, glob, hashlib, json, pathlib, subprocess, sys
import numpy as np

directory=pathlib.Path(sys.argv[1])
source_path=next(directory.glob('cef-*.csv'))
source=list(csv.DictReader(source_path.open()))
starts=np.array([float(r['mapped_ms']) for r in source])
arrivals=np.array([float(r['received_ms']) for r in source])
frames=np.array([int(r['frames']) for r in source])
ends=starts+frames/48
offsets=np.concatenate(([0],np.cumsum(frames)))
raw=np.fromfile(source_path.with_suffix('.s16'),dtype='<i2').reshape(-1,2)
gaps=[dict(fromMs=float(ends[i]),toMs=float(starts[i+1]),durationMs=float(starts[i+1]-ends[i]),
           followingCallbackDelayMs=float(arrivals[i+1]-starts[i+1]))
      for i in range(len(source)-1) if starts[i+1]-ends[i]>2]
report={'sourcePackets':len(source),'sourceFrames':int(sum(frames)),
        'sourceSilentPackets':sum(float(r['rms'])<.001 for r in source),
        'sourceTimelineGaps':gaps,'maxCallbackDelayMs':float(max(arrivals-starts)),
        'cases':[]}
results=json.loads((directory/'results.json').read_text())
for entry in results['cases']:
    path=pathlib.Path(entry['destination'])
    # Archives can be moved: the original absolute destination is only metadata.
    path=directory/path.name
    blocks=list(csv.DictReader(path.with_suffix(path.suffix+'.csv').open()))
    pcm_path=path.with_suffix(path.suffix+'.s16')
    pre=np.fromfile(pcm_path,dtype='<i2').reshape(-1,2)
    origin=float(blocks[0]['at_ms']); finish=float(blocks[-1]['at_ms'])+int(blocks[-1]['frames'])/48
    missing=0;eventual_missing=0;timely_missing=0; mismatched_frames=0; interior_total=0;interior_eventual=0
    for b in blocks:
        at=float(b['at_ms']); count=int(b['frames']); encoded=float(b['encoded_ms']); index=int(b['sample_index'])
        eventual=np.zeros(count,dtype=bool);timely=np.zeros(count,dtype=bool)
        expected=np.zeros((count,2),dtype='<i2')
        # Include all overlaps; rare quantized boundary overlaps are deliberate.
        first=max(0,np.searchsorted(starts,at-100,side='left'))
        last=np.searchsorted(starts,at+count/48+1,side='right')
        for j in range(first,last):
            offset=round((starts[j]-at)*48);a=max(0,offset);z=min(count,offset+int(frames[j]))
            if z<=a:continue
            eventual[a:z]=True
            if arrivals[j]<=encoded:
                timely[a:z]=True
                expected[a:z]=raw[offsets[j]+a-offset:offsets[j]+z-offset]
        missing+=int(b['missing_frames'])
        eventual_missing+=int(sum(~eventual));timely_missing+=int(sum(~timely))
        # The packet straddling output creation may predate subscription. Stop
        # drains the jitter buffer. Exclude both edges from copy verification.
        if at>max(origin+20,starts[0]+20) and at+count/48<finish-150:
            interior_total+=int(b['missing_frames']);interior_eventual+=int(sum(~eventual))
            mismatched_frames+=int(np.count_nonzero(np.any(expected!=pre[index:index+count],axis=1)))
    decoded=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-f','s16le','-acodec','pcm_s16le','-'],capture_output=True,check=True).stdout
    audio=np.frombuffer(decoded,dtype='<i2').reshape(-1,2).mean(axis=1)/32768
    n=len(audio)//480;rms=np.sqrt(np.mean(audio[:n*480].reshape(n,480)**2,axis=1))
    silent=rms<.001; bounds=[];begin=None
    for i,quiet in enumerate(np.append(silent,False)):
        if quiet and begin is None:begin=i
        if not quiet and begin is not None:
            bounds.append({'startMs':begin*10,'endMs':i*10,'durationMs':(i-begin)*10});begin=None
    case={'audioCodec':entry['audioCodec'],'originMs':origin,'finishMs':finish,
          'missingInputMs':missing/48,'eventuallyUncoveredInputMs':eventual_missing/48,
          'estimatedMissingAtEncodeMs':timely_missing/48,
          'interiorMissingInputMs':interior_total/48,'interiorEventuallyUncoveredMs':interior_eventual/48,
          'interiorSourceCopyMismatchedFrames':mismatched_frames,
          'sourceGapsWithinOutput':[{**g,'outputStartMs':g['fromMs']-origin} for g in gaps if origin<g['fromMs']<finish],
          'decodedSilence10msWindows':bounds,'final':entry['final'],
          'decodedSamples':len(audio),'decodeErrors':False}
    if entry['audioCodec']=='pcm_s16le':
        case['decodedPCMEqualsPreEncoderPCM']=decoded==pcm_path.read_bytes()
        case['decodedPCMSha256']=hashlib.sha256(decoded).hexdigest()
    report['cases'].append(case)
sampler=directory/'processes.jsonl'
if sampler.exists():
    rows=[json.loads(line) for line in sampler.read_text().splitlines()]
    def stat(row):return {k:int(v) for k,v in (line.split() for line in row['host']['cpu.stat'].splitlines())}
    first,last=stat(rows[0]),stat(rows[-1])
    report['hostCPUQuota']=(pathlib.Path('/sys/fs/cgroup/cpu.max').read_text().strip())
    report['hostCPUStatDelta']={k:last[k]-first[k] for k in first}
    report['profileSamples']=len(rows)
(directory/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
