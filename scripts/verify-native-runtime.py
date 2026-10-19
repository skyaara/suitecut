#!/usr/bin/env python3
"""Exercise an extracted, relocated full runtime without Node or system FFmpeg."""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.parse


HTML = '''<!doctype html><title>Native full runtime</title>
<label>Name<input aria-label="Name"></label><button onclick="out.textContent=document.querySelector('input').value">Save</button><p id="out"></p>
<canvas width="640" height="360"></canvas><script>
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d');
function paint(t){ctx.fillStyle=`hsl(${t/5%360} 90% 45%)`;ctx.fillRect(0,0,640,360);ctx.fillStyle='white';ctx.font='32px sans-serif';ctx.fillText(String(t|0),20,80);requestAnimationFrame(paint)}requestAnimationFrame(paint);
const ac=new AudioContext(),osc=ac.createOscillator(),gain=ac.createGain();osc.frequency.value=440;gain.gain.value=.1;osc.connect(gain).connect(ac.destination);osc.start();ac.resume();
</script>'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path,help='New verification directory')
    parser.add_argument('--chrome',type=Path,help='Optional external Chromium executable')
    args=parser.parse_args()
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    extracted=out/'extracted';extracted.mkdir()
    with tarfile.open(args.archive) as archive:
        archive.extractall(extracted,filter='data')
    roots=list(extracted.iterdir())
    assert len(roots)==1 and roots[0].is_dir(),roots
    # Spaces and a second location exercise launcher quoting and relative discovery.
    install=out/'relocated installation'
    roots[0].rename(install)
    manifest=json.loads((install/'runtime-manifest.json').read_text())
    for file in manifest['files']:
        path=install/file['path']
        if 'symlink' in file:
            assert os.readlink(path)==file['symlink']
        else:
            with path.open('rb') as data:
                assert hashlib.file_digest(data,'sha256').hexdigest()==file['sha256'],file['path']
    # Runtime subprocesses cannot discover Node or media tools through PATH.
    empty_path=out/'command-path';empty_path.mkdir()
    (empty_path/'dirname').symlink_to('/usr/bin/dirname')
    env=dict(os.environ,PATH=str(empty_path))
    env.pop('LD_LIBRARY_PATH',None)
    checks=[]

    def run(name,command,binary=False):
        result=subprocess.run([str(x) for x in command],env=env,cwd=out,
                              capture_output=True,timeout=100)
        (out/(name+'.stderr')).write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.returncode}: {result.stderr.decode(errors="replace")[-4000:]}')
        if binary:
            return result.stdout
        value=result.stdout.decode()
        (out/(name+'.stdout')).write_text(value)
        return value

    ffmpeg=install/'bin/ffmpeg';ffprobe=install/'bin/ffprobe'
    version=run('ffmpeg-version',[ffmpeg,'-version']).splitlines()[0]
    assert '9.0.2' in version,version
    checks.append('relocated file checksums and bundled FFmpeg version')
    url='data:text/html,'+urllib.parse.quote(HTML)
    sessions=out/'sessions';sessions.mkdir(mode=0o700)
    control=[install/'bin/suitecut','--session-dir',sessions,'--session','bundle-proof']

    def cli(name,*words):
        reply=json.loads(run(name,[*control,*words]))
        assert reply['ok'],reply
        result=reply['result']
        assert not isinstance(result,dict) or result.get('ok',True),result
        return result

    try:
        initial=cli('session-open','open',url)['observation']
        name=next(c['ref'] for c in initial['controls'] if c.get('name')=='Name')
        cli('session-fill','--response','none','fill',name,'Bundled native control')
        assert cli('session-value','get','value',name)=='Bundled native control'
        cli('session-close','close')
        assert cli('session-status','status')['running'] is False
    finally:
        # This never starts an absent session, including on failed startup.
        cli('session-final-close','close')
    checks.append('bundled CEF named-session launch, observation, input, close')
    recorder=install/'bin/suitecut-native-record'
    cases=[('cef',None,29.97,True,'.webm')]
    if args.chrome:
        cases.append(('chromium',args.chrome.resolve(),120,False,'.mp4'))
    recordings=[]
    for backend,browser,fps,audio,suffix in cases:
        video=out/(backend+suffix)
        command=[recorder,'--backend',backend,'--url',url,'--output',video,
                 '--duration-ms','1100','--fps',str(fps),'--width','640','--height','360']
        if audio:command.append('--audio')
        if browser:command.extend(['--browser',browser,'--browser-arg','--no-sandbox'])
        receipt=json.loads(run(backend+'-record',command))
        assert receipt['ok'] and receipt['ffmpegPath']==str(ffmpeg),receipt
        assert receipt['ffprobePath']==str(ffprobe),receipt
        assert video.stat().st_size>1000
        probe=json.loads(run(backend+'-probe',[ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',video]))
        stream=next(s for s in probe['streams'] if s['codec_type']=='video')
        assert (stream['width'],stream['height'])==(640,360),stream
        assert int(stream['nb_read_frames'])>=20,stream
        numerator,denominator=map(int,stream['avg_frame_rate'].split('/'))
        assert abs(numerator/denominator-fps)<.01,stream
        assert .7<float(probe['format']['duration'])<5,probe['format']
        # Decode two real frames, rather than relying only on container metadata.
        frames=run(backend+'-pixels',[ffmpeg,'-v','error','-i',video,'-an','-vf','fps=2,scale=64:36',
                   '-frames:v','2','-threads','1','-f','rawvideo','-pix_fmt','rgb24','pipe:1'],binary=True)
        frame_bytes=64*36*3
        assert len(frames)==frame_bytes*2 and frames[:frame_bytes]!=frames[frame_bytes:],len(frames)
        rms=None
        if audio:
            assert receipt['recording']['audioInputFrames']>0,receipt
            assert any(s['codec_type']=='audio' for s in probe['streams']),probe
            samples=array.array('f')
            samples.frombytes(run(backend+'-audio',[ffmpeg,'-v','error','-i',video,'-vn','-ac','1',
                              '-ar','8000','-t','0.3','-threads','1','-f','f32le','pipe:1'],binary=True))
            rms=math.sqrt(sum(s*s for s in samples)/len(samples))
            assert rms>.005,rms
        recordings.append({'backend':backend,'path':str(video),'bytes':video.stat().st_size,
                           'fps':fps,'decodedFrames':int(stream['nb_read_frames']),
                           'audioRms':rms,'differentDecodedFrames':True})
        checks.append(backend+' real recording, decoded cadence, changing pixels'+(' and audible page audio' if audio else ''))
        print('PASS '+checks[-1],flush=True)
    report={'ok':True,'nodeUsed':False,'systemFFmpegUsed':False,'ffmpeg':version,
            'installation':str(install),'checks':checks,'recordings':recordings}
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
