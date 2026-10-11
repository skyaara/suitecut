"""Fully decode saved outputs and retain frame markers, probes and errors."""
import json
import pathlib
import subprocess
import sys
import numpy as np

root=pathlib.Path(sys.argv[1])
results={}

def validate_audio(path):
    decoded=path.with_suffix(path.suffix+'.decoded.s16')
    log=path.with_suffix(path.suffix+'.audio-decode.log')
    if not decoded.exists():
        result=subprocess.run(['ffmpeg','-v','error','-threads','1','-i',str(path),'-map','0:a:0',
                               '-ar','48000','-ac','2','-f','s16le','-'],capture_output=True)
        log.write_bytes(result.stderr)
        if result.returncode:raise RuntimeError(f'Audio decode failed: {path}')
        decoded.write_bytes(result.stdout)
    return {'decodedAudioFrames':decoded.stat().st_size//4,'audioDecodeExitCode':0,
            'audioDecodeErrors':bool(log.read_bytes())}

for case in sorted(root.iterdir()):
    if not case.is_dir() or not (case/'result.json').exists():continue
    outputs={}
    for name in ['recording.mkv','receiver.flv']:
        path=case/name
        if not path.exists():continue
        decode=subprocess.run(['ffmpeg','-v','error','-threads','1','-i',str(path),'-an',
                               '-vf','crop=800:32:0:0,scale=50:1:flags=area,format=gray',
                               '-fps_mode','passthrough','-f','rawvideo','-pix_fmt','gray','-'],capture_output=True)
        path.with_suffix(path.suffix+'.video-decode.log').write_bytes(decode.stderr)
        pixels=np.frombuffer(decode.stdout,dtype=np.uint8).reshape(-1,50)
        marker=(pixels[:,:20]>127).astype(np.uint32)@np.array([1<<i for i in range(20)],dtype=np.uint32)
        audio_clock=(pixels[:,30:]>127).astype(np.uint32)@np.array([1<<i for i in range(20)],dtype=np.uint32)
        boundaries=np.r_[0,np.flatnonzero(marker[1:]!=marker[:-1])+1,len(marker)]
        holds=np.diff(boundaries)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(path)]))
        item={**validate_audio(path),'decodedVideoFrames':len(marker),'videoDecodeExitCode':decode.returncode,'videoDecodeErrors':bool(decode.stderr),
              'maximumMarkerHoldMs':float(max(holds,default=0)*1000/30),
              'distinctMarkerFPS':len(holds)/(len(marker)/30) if len(marker) else 0,
              'videoMarkers':marker.tolist(),'visualAudioClock10msTicks':audio_clock.tolist(),'probe':probe,
              'caveat':'Four-second page JavaScript stalls intentionally freeze both visual markers in stress cases. This marker analysis is not a full A/V sync verdict.'}
        outputs[name]=item
        if decode.returncode:raise RuntimeError(f'Video decode failed: {path}')
    results[case.name]=outputs
(root/'media-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({c:{n:{k:v for k,v in d.items() if k not in ['videoMarkers','visualAudioClock10msTicks','probe']} for n,d in ds.items()} for c,ds in results.items()},indent=2))
