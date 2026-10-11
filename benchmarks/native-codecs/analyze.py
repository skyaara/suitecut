"""Decode every saved codec output independently of SuiteCut/libav counters."""
import hashlib
import json
import pathlib
import subprocess
import sys

import numpy as np

directory = pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'benchmarks/native-codecs/2026-10-10')
result = json.loads((directory/'matrix.json').read_text())
analysis = {'cases': [], 'concurrent': [], 'lossless': [], 'core': []}

def run(args):
    return subprocess.check_output(args)

def decode(path, marker=False):
    probe = json.loads(run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)]))
    frames = json.loads(run(['ffprobe','-v','error','-select_streams','v','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',str(path)]))['frames']
    pts = np.array([float(f['best_effort_timestamp_time']) for f in frames])
    # A complete decode also catches damage beyond the marker/audio samples.
    subprocess.run(['ffmpeg','-v','error','-xerror','-threads','2','-i',str(path),'-f','null','-'],check=True,stdout=subprocess.DEVNULL)
    pcm = np.frombuffer(run(['ffmpeg','-v','error','-threads','2','-i',str(path),'-vn','-ac','1','-ar','8000','-f','f32le','-']),dtype='<f4')
    count = len(pcm)//800
    rms = np.sqrt(np.mean(pcm[:count*800].reshape(count,800)**2,axis=1))
    max_silence = consecutive = 0
    for silent in rms < .001:
        consecutive = consecutive+1 if silent else 0
        max_silence = max(max_silence,consecutive)
    middle=pcm[len(pcm)//2:len(pcm)//2+8000]
    dominant=float(np.fft.rfftfreq(len(middle),1/8000)[np.argmax(np.abs(np.fft.rfft(middle)))])
    data={'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
          'probe':probe,'decodedFrames':len(pts),'maximumTimestampGapMs':float(np.max(np.diff(pts))*1000),
          'audioSeconds':len(pcm)/8000,'maximumSilence100msWindowsMs':max_silence*100,
          'dominantMiddleAudioHz':dominant,'audioRmsMedian':float(np.median(rms)),'fullDecode':'passed'}
    windows=len(pcm)//80
    quiet=np.sqrt(np.mean(pcm[:windows*80].reshape(windows,80)**2,axis=1))<.001
    changes=np.diff(np.r_[False,quiet,False].astype(np.int8))
    intervals=[{'startMs':int(a*10),'durationMs':int((b-a)*10),
        'position':'leading' if a==0 else 'trailing' if b==windows else 'interior'}
        for a,b in zip(np.flatnonzero(changes==1),np.flatnonzero(changes==-1))]
    data.update(silence10msWindows=intervals,
        maximumInteriorSilenceMs=max((i['durationMs'] for i in intervals if i['position']=='interior'),default=0),
        leadingSilenceMs=next((i['durationMs'] for i in intervals if i['position']=='leading'),0))
    assert len(pts)>0 and np.all(np.diff(pts)>0), 'Invalid frame timestamps'
    assert np.median(rms)>.005, 'Output audio is silent'
    if marker:
        pixels=run(['ffmpeg','-v','error','-threads','2','-filter_threads','2','-i',str(path),'-an','-vf','crop=320:32:0:0,scale=20:1:flags=neighbor,format=gray','-fps_mode','passthrough','-f','rawvideo','-'])
        bits=np.frombuffer(pixels,dtype=np.uint8).reshape(-1,20)>128
        ids=np.sum(bits.astype(np.uint32)*(2**np.arange(20,dtype=np.uint32)),axis=1)
        assert len(ids)==len(pts), 'Marker and timestamp frame counts differ'
        changes=np.flatnonzero(np.diff(ids)!=0)+1
        boundaries=np.r_[0,changes,len(ids)]
        holds=[((pts[b] if b<len(pts) else pts[-1]+1/30)-pts[a]) for a,b in zip(boundaries[:-1],boundaries[1:])]
        data.update(distinctFrames=int(len(changes)+1),distinctFps=float((len(changes)+1)/(pts[-1]-pts[0]+1/30)),
                    maximumMarkerHoldMs=float(max(holds)*1000),markerFirst=int(ids[0]),markerLast=int(ids[-1]),
                    distinctFramesDuringNodeStall=int(len(np.unique(ids[(pts>=5)&(pts<9)]))))
        assert data['distinctFps']>20, 'Codec did not sustain usable browser capture on this host'
        assert data['distinctFramesDuringNodeStall']>80, 'Capture stalled with Node'
        assert max_silence<=5, 'More than 500ms of audio silence'
    return data

for entry in result['cases']:
    capture=decode(pathlib.Path(entry['capturePath']),True)
    render=decode(pathlib.Path(entry['renderPath']))
    assert capture['probe']['streams'][0]['codec_name']==entry['decodedCodec']
    assert render['probe']['streams'][0]['codec_name']==entry['decodedCodec']
    assert render['decodedFrames']==round(entry['renderReport']['presentationDurationMs']*30/1000)
    manifest=json.loads(pathlib.Path('benchmarks/native-engine/2026-10-09/presentation-visual-fixed/manifest.json').read_text())
    events=manifest['tests'][0]['attempts'][0]['events']
    narration=next(e['atMs'] for e in events if e['type']=='narration')
    selections=[e['atMs'] for e in events if e['type']=='page-selected']
    pixels=[]
    for at in [narration+300,selections[0]+150,selections[1]+100]:
        pixel=run(['ffmpeg','-v','error','-threads','2','-filter_threads','2','-ss',str(at/1000),'-i',entry['renderPath'],'-frames:v','1','-vf','crop=2:2:8:8,format=rgb24','-f','rawvideo','-'])
        pixels.append(list(pixel[:3]))
    assert pixels[0][2]>pixels[0][1] and pixels[1][1]>pixels[1][2]*1.3 and pixels[2][2]>pixels[2][1], 'Visual page selection failed'
    audio=np.frombuffer(run(['ffmpeg','-v','error','-threads','2','-i',entry['renderPath'],'-vn','-ac','1','-ar','48000','-f','f32le','-']),dtype='<f4')
    def amplitude(at,hz):
        window=audio[round(at*48):round((at+200)*48)]
        return float(2*abs(np.sum(window*np.exp(2j*np.pi*hz*np.arange(len(window))/48000)))/len(window))
    tones={'selected880':amplitude(selections[0]+150,880),'selected660':amplitude(selections[0]+150,660),
           'returned660':amplitude(selections[1]+100,660),'returned880':amplitude(selections[1]+100,880),
           'narration440':amplitude(narration+300,440),'narrationPage660':amplitude(narration+300,660)}
    assert tones['selected880']>tones['selected660']*3 and tones['returned660']>tones['returned880']*3, 'Page audio selection failed'
    assert tones['narration440']>.03 and tones['narrationPage660']>.009, 'Narration mix failed'
    render.update(visualSelectionPixels=pixels,toneAmplitudes=tones,presentationSelectionAndMix='passed')
    analysis['cases'].append({'videoCodec':entry['videoCodec'],'audioCodec':entry['audioCodec'],'capture':capture,'render':render})
    print(entry['videoCodec'],entry['audioCodec'],'decoded; distinct fps',round(capture['distinctFps'],2),flush=True)
for path in [result['concurrent']['recordPath'],result['concurrent']['receiverPath']]:
    analysis['concurrent'].append(decode(pathlib.Path(path),True))
for path in sorted((directory/'core').iterdir()):
    if path.suffix in ['.mp4','.webm','.mkv','.mov']:
        analysis['core'].append(decode(path))

# FFV1 preserves the actual I420 encoder input, not the original browser RGB.
expected_video=b''.join(bytes([16+i])*(320*180)+bytes([128])*(320*180//2) for i in range(30))
expected_audio=(directory/'core'/'reference.s16').read_bytes()
for audio in ['flac','pcm_s16le']:
    path=directory/'core'/f'ffv1-{audio}.mkv'
    video=run(['ffmpeg','-v','error','-threads','2','-i',str(path),'-an','-pix_fmt','yuv420p','-fps_mode','passthrough','-f','rawvideo','-'])
    pcm=run(['ffmpeg','-v','error','-threads','2','-i',str(path),'-vn','-ac','2','-ar','48000','-f','s16le','-'])
    assert video==expected_video, 'FFV1 video is not bit exact'
    assert pcm[:len(expected_audio)]==expected_audio and len(pcm)==len(expected_audio), 'Lossless audio is not bit exact'
    analysis['lossless'].append({'audioCodec':audio,'videoBytes':len(video),'audioBytes':len(pcm),'videoBitExact':True,'audioBitExact':True})

samples=[json.loads(line) for line in (directory/'processes.jsonl').read_text().splitlines()]
for entry, findings in zip(result['cases'],analysis['cases']):
    selected=[s for s in samples if entry['startMonotonic']+2<s['monotonic']<entry['endMonotonic']-1]
    totals=[];memory=[];threads=[]
    for sample in selected:
        rows=[p for p in sample['processes'] if p['role'] not in ['sampler','receiver','node']]
        totals.append(sum(p['cpuPercent'] or 0 for p in rows)/100)
        memory.append(sum(p['rssBytes'] for p in rows))
        threads.append(sum(p['threads'] for p in rows))
    findings['resources']={'samples':len(totals),'meanNativeCores':float(np.mean(totals)),
        'peakSummedNativeRssMiB':max(memory)/1048576,'peakNativeThreads':max(threads),
        'nodeCpuPercentOneCore':sum(entry['nodeCpu'].values())/entry['wallMs']/10,
        'nodeCpuPercentExcludingStall':(sum(entry['nodeCpu'].values())-sum(entry['nodeStallCpu'].values()))/(entry['wallMs']-entry['nodeStallMs'])/10,
        'summedRssDoubleCountsSharedLibraries':True}
analysis['status']='passed_with_continuity_findings'
analysis['codecDecode']='passed'
analysis['gapFree']=False
analysis['criteria']={'minimumDistinctFps':20,'maximumSilence100msWindowsMs':500,
    'note':'Codec/decode acceptance is separate from gap-free audio; precise interior silence is reported at 10ms resolution.'}
(directory/'decoded-validation.json').write_text(json.dumps(analysis,indent=2))
print('All capture, render, concurrent and lossless decode checks passed',flush=True)
