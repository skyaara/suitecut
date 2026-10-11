"""Full decodes, source attribution, exact startup samples, and matched profiles."""
import hashlib,json,pathlib,subprocess,sys
import numpy as np

root=pathlib.Path(sys.argv[1]).resolve()
single=(root/'matrix.json').exists()
summary={'versions':[],'gapFree':False,'audioReadinessPolicy':'mandatory native signal readiness' if single else 'historical opt-in native packet/signal gate'}
for name in ['default'] if single else ['current','patch','stable']:
    directory=root if single else root/name
    matrix=json.loads((directory/'matrix.json').read_text())
    version={'name':name,'cefVersion':matrix['cefVersion'],'cases':[]}
    for entry in matrix['cases']:
        case=directory/entry['label']
        subprocess.run([sys.executable,'benchmarks/native-audio/analyze.py',str(case)],check=True,stdout=(root/'logs'/f'{name}-{entry["label"]}-analysis.log').open('w'))
        attribution=json.loads((case/'analysis.json').read_text())
        output=case/pathlib.Path(entry['destination']).name
        pre=np.fromfile(output.with_suffix(output.suffix+'.s16'),dtype='<i2').reshape(-1,2)
        decoded=subprocess.run(['ffmpeg','-v','error','-i',str(output),'-map','0:a:0','-f','s16le','-acodec','pcm_s16le','-'],capture_output=True,check=True).stdout
        audio=np.frombuffer(decoded,dtype='<i2').reshape(-1,2)
        def first_signal(a):
            hits=np.flatnonzero(np.any(np.abs(a.astype(np.int32))>32,axis=1))
            return None if not len(hits) else int(hits[0])/48
        marker=subprocess.run(['ffmpeg','-v','error','-i',str(output),'-an','-vf','crop=320:32:0:0,scale=20:1:flags=area,format=gray','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','gray','-'],capture_output=True,check=True).stdout
        pixels=np.frombuffer(marker,dtype=np.uint8).reshape(-1,20)
        values=(pixels>127).astype(np.uint32)@np.array([1<<i for i in range(20)],dtype=np.uint32)
        holds=[];held=1
        for a,b in zip(values,values[1:]):
            if a==b:held+=1
            else:holds.append(held);held=1
        holds.append(held)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)],text=True))
        profiles=[json.loads(line) for line in (case/'processes.jsonl').read_text().splitlines()]
        in_capture=[p for p in profiles if entry['captureStartedMonotonic']<=p['monotonic']<=entry['captureEndedMonotonic']]
        role_cpu={}
        for p in in_capture:
            roles={}
            for process in p['processes']:
                roles[process['role']]=roles.get(process['role'],0)+(process['cpuPercent'] or 0)/100
            for role,value in roles.items():role_cpu.setdefault(role,[]).append(value)
        means={role:sum(vals)/len(in_capture) for role,vals in role_cpu.items()} if in_capture else {}
        validation={**entry,'destination':str(output),'preEncoderFirstSignalMs':first_signal(pre),
                    'decodedFirstSignalMs':first_signal(audio),'sourceAttribution':attribution,
                    'videoDecodedFrames':len(values),'distinctDecodedFPS':len(holds)/(len(values)/30),
                    'maximumDecodedImageHoldMs':max(holds)*1000/30,
                    'meanCPUCoresByRole':means,'profileCaptureSamples':len(in_capture),
                    'nativeMeanCPUCores':sum(v for k,v in means.items() if k not in ['node','sampler']),
                    'probe':probe,'fileSha256':hashlib.file_digest(output.open('rb'),'sha256').hexdigest()}
        if len(values)!=entry['final']['videoFrames']:raise RuntimeError('Decoded/native video frame counts differ: '+str(output))
        if entry['audioStartup']=='wait-for-signal' and first_signal(pre)!=0:
            raise RuntimeError('Signal-gated native recording has leading silence: '+str(output))
        (case/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
        version['cases'].append(validation)
    summary['versions'].append(version)
(root/'comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
for version in summary['versions']:
    print(version['name'],version['cefVersion'])
    for c in version['cases']:
        a=c['sourceAttribution']['cases'][0]
        spans=a['decodedSilence10msWindows']
        print(c['label'],'ready',round(c['readyLatencyMs'],1),'native first signal',c['preEncoderFirstSignalMs'],
              'decoded first signal',c['decodedFirstSignalMs'],'max silence',max((g['durationMs'] for g in spans),default=0),
              'native CPU',round(c['nativeMeanCPUCores'],3),'fps',round(c['distinctDecodedFPS'],2))
