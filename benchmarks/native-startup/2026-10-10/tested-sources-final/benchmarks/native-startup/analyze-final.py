"""Verify the final common-origin and helper-confirmation builds independently."""
import json,pathlib,subprocess,sys
import numpy as np
root=pathlib.Path(sys.argv[1]).resolve();validated=[]
for version in ['current','patch','stable']:
    directory=root/version/'final-cold'
    matrix=json.loads((directory/'matrix.json').read_text())
    for entry in matrix['cases']:
        case=directory/entry['label'];path=case/pathlib.Path(entry['destination']).name
        with (root/'logs'/f'{version}-final-{entry["label"]}-analysis.log').open('w') as log:
            subprocess.run([sys.executable,'benchmarks/native-audio/analyze.py',str(case)],check=True,stdout=log)
        pre=np.fromfile(path.with_suffix(path.suffix+'.s16'),dtype='<i2').reshape(-1,2)
        pcm=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-f','s16le','-acodec','pcm_s16le','-'],capture_output=True,check=True).stdout
        decoded=np.frombuffer(pcm,dtype='<i2').reshape(-1,2)
        def first(a):
            locations=np.flatnonzero(np.any(np.abs(a.astype(np.int32))>32,axis=1))
            return None if not len(locations) else int(locations[0])/48
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(path)],text=True))
        video=next(s for s in probe['streams'] if s['codec_type']=='video')
        if int(video['nb_read_frames'])!=entry['final']['videoFrames']:raise RuntimeError('Final video frame mismatch')
        if first(pre)!=0:raise RuntimeError('Final native startup contains leading silence')
        if entry['audioCodec']=='pcm_s16le' and pcm!=path.with_suffix(path.suffix+'.s16').read_bytes():raise RuntimeError('Final PCM decode differs')
        result={'version':version,'cefVersion':entry['cefVersion'],'label':entry['label'],
                'readyLatencyMs':entry['readyLatencyMs'],'preEncoderFirstSignalMs':first(pre),
                'decodedFirstSignalMs':first(decoded),'probe':probe,
                'sourceAttribution':json.loads((case/'analysis.json').read_text()),'status':entry['final']}
        (case/'validation.json').write_text(json.dumps(result,indent=2)+'\n');validated.append(result)
(root/'final-validation.json').write_text(json.dumps(validated,indent=2)+'\n')
for row in validated:print(row['version'],row['label'],'native',row['preEncoderFirstSignalMs'],'decoded',row['decodedFirstSignalMs'],'ready',row['readyLatencyMs'])
