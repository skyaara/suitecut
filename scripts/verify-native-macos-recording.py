#!/usr/bin/env python3
"""Measure distinct encoded browser frames and actual audio in a native runtime."""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time


def record_with_load(command, env, out, name, workers, delay_ms):
    """Own a bounded CPU load until capture settles; never touch other work."""
    process = subprocess.Popen([str(x) for x in command], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    loads, samples, events = [], [], []
    seen = set()
    installation = str(Path(command[0]).parent.parent)
    began, next_sample = time.monotonic(), 0
    try:
        while process.poll() is None:
            elapsed = time.monotonic() - began
            if workers and not loads and elapsed * 1000 >= delay_ms:
                code = ('import time,hashlib,os;os.nice(10);parent=os.getppid();end=time.monotonic()+300;data=bytes(1048576)\n'
                        'while time.monotonic()<end and os.getppid()==parent: hashlib.sha256(data).digest()')
                loads = [subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for _ in range(workers)]
                events.append({'event':'loadStarted','elapsedSeconds':elapsed,'workerPids':[p.pid for p in loads],'nice':10})
            if elapsed >= next_sample:
                sample = subprocess.run(['/bin/ps','-axo','pid=,ppid=,%cpu=,rss=,comm='],capture_output=True,text=True)
                disk = os.statvfs(out)
                entry = {'elapsedSeconds':elapsed,'processes':sample.stdout,
                         'diskAvailableBytes':disk.f_bavail * disk.f_frsize,
                         'loadAverage':os.getloadavg()}
                for line in sample.stdout.splitlines():
                    fields = line.strip().split(None,4)
                    if len(fields) == 5 and fields[4].startswith(installation) and fields[0] not in seen:
                        seen.add(fields[0])
                        actual = subprocess.run(['/bin/ps','-p',fields[0],'-o','command='],capture_output=True,text=True)
                        entry.setdefault('newOwnedCommands',[]).append({'pid':int(fields[0]),'command':actual.stdout.strip()})
                samples.append(entry)
                next_sample = elapsed + 1
            if elapsed > 180:
                raise subprocess.TimeoutExpired(command, 180)
            time.sleep(.05)
        stdout, stderr = process.communicate()
        return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    finally:
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        for child in loads:
            if child.poll() is None: child.terminate()
            child.wait(timeout=10)
        events.append({'event':'loadRetired','elapsedSeconds':time.monotonic()-began,'workerExitCodes':[p.returncode for p in loads]})
        (out/(name+'.load.json')).write_text(json.dumps(events,indent=2)+'\n')
        (out/(name+'.host.json')).write_text(json.dumps(samples)+'\n')


def run(command, timeout=120, env=None):
    return subprocess.run([str(x) for x in command], capture_output=True, timeout=timeout, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--duration-ms', type=int, default=6000)
    parser.add_argument('--case', action='append', help='Override matrix with WIDTHxHEIGHT@FPS or WIDTHxHEIGHT@FPS:audio')
    parser.add_argument('--require-native-capture', action='store_true', help='Require actual CEF timestamps and GPU NV12 admission for all active input')
    parser.add_argument('--require-source-continuity', action='store_true',
                        help='Require every decoded painted ID to advance by one across the complete stream')
    parser.add_argument('--audio-sync-proof', action='store_true', help='Pulse audio with painted counter phase and measure decoded A/V alignment')
    parser.add_argument('--format', choices=['webm','mp4'], default='webm')
    parser.add_argument('--fixture-mode', choices=['reallocate','steady'], default='reallocate',
                        help='Keep the original per-frame canvas reallocation stress, or draw into a stable surface')
    parser.add_argument('--load-workers', type=int, default=0)
    parser.add_argument('--load-delay-ms', type=int, default=0)
    parser.add_argument('--required-distinct-fps', type=float)
    parser.add_argument('--require-no-recorder-drops', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.load_workers <= 32 or args.load_delay_ms < 0:
        parser.error('Load must use 0..32 owned workers and a nonnegative delay')
    install = args.installation.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # The runtime can find no Node or system/Homebrew media tools via PATH.
    env = dict(os.environ, PATH='/usr/bin:/bin')
    for key in list(env):
        if key.startswith(('DYLD_', 'SUITECUT_')):
            del env[key]
    fixture = out / 'cadence.html'
    fixture.write_text('''<!doctype html><meta charset="utf-8"><title>Native cadence proof</title>
<style>html,body{margin:0;width:100%;height:100%;overflow:hidden}canvas{display:block;width:100%;height:100%}</style>
<canvas></canvas><script>
const canvas=document.querySelector('canvas'),context=canvas.getContext('2d',{alpha:false});
let id=0;function paint(){if(FIXTURE_REALLOCATE||canvas.width!==innerWidth||canvas.height!==innerHeight){canvas.width=innerWidth;canvas.height=innerHeight}++id;
for(let bit=0;bit<16;bit++){context.fillStyle=(id>>bit)&1?'white':'black';context.fillRect(bit*canvas.width/16,0,canvas.width/16+1,canvas.height)}
if(SYNC_PROOF)gain.gain.setValueAtTime(id%120<60?.1:0,audio.currentTime);
requestAnimationFrame(paint)}requestAnimationFrame(paint);
const audio=new AudioContext({sampleRate:48000}),osc=audio.createOscillator(),gain=audio.createGain();
osc.frequency.value=440;gain.gain.value=.1;osc.connect(gain).connect(audio.destination);osc.start();audio.resume();
</script>'''.replace('FIXTURE_REALLOCATE', 'true' if args.fixture_mode=='reallocate' else 'false').replace('SYNC_PROOF', 'true' if args.audio_sync_proof else 'false'))
    cases = [(320,180,120,False),(1280,720,30,False),(1280,720,60,False),
             (1280,720,90,False),(1280,720,120,False),(1920,1080,60,False),
             (1920,1080,120,False),(1920,1080,120,False),(2560,1440,120,False),
             (3840,2160,120,False),(1280,720,59.94,True),(1280,720,120,True)]
    if args.case:
        cases=[]
        for case in args.case:
            dimensions,rate=case.split('@')
            width,height=map(int,dimensions.split('x'))
            audio=rate.endswith(':audio')
            cases.append((width,height,float(rate.removesuffix(':audio')),audio))
    results = []
    recorder, ffmpeg, ffprobe = [install/'bin'/name for name in ['suitecut-native-record','ffmpeg','ffprobe']]
    for index,(width,height,fps,audio) in enumerate(cases):
        name = f'{index:02d}-{width}x{height}-{fps}fps' + ('-audio' if audio else '')
        video = out/(name+'.'+args.format)
        command = [recorder,'--url',fixture.as_uri(),'--output',video,'--duration-ms',args.duration_ms,
                   '--width',width,'--height',height,'--fps',fps]
        if audio:
            command.append('--audio')
        started = time.monotonic()
        result = record_with_load(command, env, out, name, args.load_workers, args.load_delay_ms)
        (out/(name+'.stdout')).write_bytes(result.stdout)
        (out/(name+'.stderr')).write_bytes(result.stderr)
        row = {'name':name,'width':width,'height':height,'requestedFps':fps,'audio':audio,
               'wallSeconds':time.monotonic()-started,'returncode':result.returncode,'ok':False}
        try:
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors='replace')[-4000:])
            receipt = json.loads(result.stdout)
            row['recording'] = receipt['recording']
            probe = run([ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',video],env=env)
            if probe.returncode:
                raise RuntimeError(probe.stderr.decode())
            metadata = json.loads(probe.stdout)
            (out/(name+'.ffprobe.json')).write_bytes(probe.stdout)
            stream = next(s for s in metadata['streams'] if s['codec_type']=='video')
            assert (stream['width'],stream['height'])==(width,height)
            numerator,denominator=map(int,stream['avg_frame_rate'].split('/'))
            assert abs(numerator/denominator-fps)<.001
            decoded = run([ffmpeg,'-v','error','-i',video,'-an','-vf','scale=16:1:flags=neighbor',
                           '-fps_mode','passthrough','-f','rawvideo','-pix_fmt','gray','pipe:1'],env=env)
            if decoded.returncode:
                raise RuntimeError(decoded.stderr.decode())
            data = decoded.stdout
            assert len(data)%16==0
            ids=[sum((1<<bit) for bit,value in enumerate(data[offset:offset+16]) if value>128)
                 for offset in range(0,len(data),16)]
            assert len(ids)==int(stream['nb_read_frames'])==int(receipt['recording']['outputFrames']), (
                f'Frame count mismatch: decoded={len(ids)}, probed={stream["nb_read_frames"]}, '
                f'reported={receipt["recording"]["outputFrames"]}')
            accounting = receipt['recording']
            assert accounting['inputFrames'] == (accounting['supersededFrames'] +
                accounting['coalescedFrames'] + accounting['outputFrames'] - accounting['heldFrames']), 'Input paint accounting mismatch'
            # Admit the document's painted IDs only; zero is a pre-paint blank.
            valid=[i for i,value in enumerate(ids) if value>0]
            assert len(valid)>1,'No advancing painted page frames'
            first,last=valid[0],valid[-1]
            window=ids[first:last+1]
            changes=sum(a!=b for a,b in zip(window,window[1:]))
            seconds=(len(window)-1)/fps
            backwards=sum(b<a for a,b in zip(window,window[1:]))
            assert not backwards,'Encoded page counter moved backward'
            row.update({'encodedFrames':len(ids),'validPaintedFrames':len(window),
                        'measuredSeconds':seconds,'distinctFrameChanges':changes,
                        'distinctFps':changes/seconds,'repeatPercent':100*(1-changes/(len(window)-1)),
                        'completeStreamRepeatedFrames':len(window)-1-changes,
                        'firstPaintId':window[0],'lastPaintId':window[-1],
                        'meets95PercentTarget':changes/seconds>=fps*.95,
                        'bytes':video.stat().st_size,'sha256':hashlib.sha256(video.read_bytes()).hexdigest()})
            requested_frames = max(2, math.ceil(args.duration_ms * fps / 1000))
            assert len(window) >= requested_frames, 'Video is shorter than the requested measurement window'
            requested = window[:requested_frames]
            requested_changes = sum(a != b for a,b in zip(requested,requested[1:]))
            row['requestedWindowFrames'] = requested_frames
            row['requestedWindowDistinctFps'] = requested_changes * fps / (len(requested)-1)
            row['requestedWindowRepeatedFrames'] = len(requested)-1-requested_changes
            row['missingPaintIds'] = sum(max(0,b-a-1) for a,b in zip(window,window[1:]))
            row['loadWorkers'] = args.load_workers
            row['format'] = args.format
            row['fixtureMode'] = args.fixture_mode
            audio_streams=[s for s in metadata['streams'] if s['codec_type']=='audio']
            assert bool(audio_streams)==audio,'Unexpected audio stream presence'
            if audio:
                decoded_audio=run([ffmpeg,'-v','error','-i',video,'-vn','-ac','1','-ar','48000',
                                   '-f','f32le','pipe:1'],env=env)
                assert decoded_audio.returncode==0
                samples=array.array('f');samples.frombytes(decoded_audio.stdout)
                rms=math.sqrt(sum(x*x for x in samples)/len(samples))
                # Inspect a center-second Fourier projection, not just stream metadata.
                center=len(samples)//2
                segment=samples[max(0,center-24000):center+24000]
                def amplitude(hz):
                    sine=sum(v*math.sin(2*math.pi*hz*i/48000) for i,v in enumerate(segment))
                    cosine=sum(v*math.cos(2*math.pi*hz*i/48000) for i,v in enumerate(segment))
                    return 2*math.hypot(sine,cosine)/len(segment)
                tone,off=amplitude(440),amplitude(660)
                assert rms>.005 and tone>.01 and tone>off*10,'Captured audio lacks the actual fixture tone'
                # The fixture emits a continuous tone. Inspect the entire
                # recording for quiet intervals, including stalls outside the
                # center-second spectral check.
                quiet = longest_quiet = 0
                for offset in range(0, len(samples)-479, 480):
                    block = samples[offset:offset+480]
                    quiet = quiet+1 if sum(v*v for v in block)/480 < .000001 else 0
                    longest_quiet = max(longest_quiet, quiet)
                if not args.audio_sync_proof:
                    assert longest_quiet <= 5, 'Captured fixture audio has more than 50 ms of silence'
                video_seconds=receipt['recording']['encodedDurationMs']/1000
                assert abs(len(samples)/48000-video_seconds)<.15,'Audio/video durations differ'
                row['audioVerification']={'rms':rms,'tone440Amplitude':tone,'offTone660Amplitude':off,
                                          'seconds':len(samples)/48000,'videoSeconds':video_seconds,
                                          'longestQuietMs':longest_quiet*10}
                if args.audio_sync_proof:
                    # Each painted counter phase sets page audio gain in that
                    # same animation callback. Compare decoded events rather
                    # than merely checking equal track lengths.
                    envelope=[]
                    for i in range(len(ids)):
                        begin=round(i*48000/fps); end=round((i+1)*48000/fps)
                        block=samples[begin:end]
                        envelope.append(bool(block) and sum(v*v for v in block)/len(block)>.0004)
                    candidates=[]
                    for shift in range(-round(fps*.2),round(fps*.2)+1):
                        pairs=[((ids[i]%120)<60,envelope[i+shift]) for i in range(round(fps),len(ids)-round(fps))
                               if 0<=i+shift<len(envelope)]
                        agreement=sum(a==b for a,b in pairs)/len(pairs)
                        candidates.append((agreement,-abs(shift),shift))
                    agreement,_,shift=max(candidates)
                    sync={'audioDelayMs':shift*1000/fps,'eventAgreement':agreement,'method':'decoded counter phase versus decoded audio RMS'}
                    row['audioVerification']['eventSync']=sync
                    assert agreement>=.98 and abs(shift*1000/fps)<=50, 'Decoded audio events are out of sync with painted images'
            if args.required_distinct_fps is not None:
                assert row['requestedWindowDistinctFps'] >= args.required_distinct_fps, 'Requested-window distinct FPS is below the required target'
            if args.require_no_recorder_drops:
                assert receipt['recording']['supersededFrames'] == 0, 'Recorder discarded active-capture input frames'
                assert receipt['recording']['coalescedFrames'] == 0, 'Recorder coalesced genuine input paints during resampling'
            if args.require_native_capture:
                assert accounting.get('encodingBackend')=='native-videotoolbox', 'Native hardware encoding was not used'
                assert accounting.get('captureTimestampsExact') is True and accounting.get('captureLookaheadMs')==0, 'Native capture timestamps were not used'
                assert accounting.get('gpuNv12Frames')==accounting['inputFrames'] and accounting['inputFrames']>0, 'Not every active input used GPU NV12 capture'
            if args.require_source_continuity:
                assert row['missingPaintIds']==0, 'CEF or recorder omitted painted image IDs'
                assert changes==len(window)-1, 'The complete encoded stream repeats painted images'
            row['ok']=True
        except Exception as error:
            row['error']=str(error)
        results.append(row)
        (out/'results.json').write_text(json.dumps({'cases':results},indent=2)+'\n')
        print(json.dumps({k:row[k] for k in ['name','ok','distinctFps','requestedWindowDistinctFps','requestedWindowRepeatedFrames','repeatPercent','error'] if k in row}),flush=True)
    rejected=run([recorder,'--url',fixture.as_uri(),'--output',out/'unsupported.webm','--fps','121'],env=env)
    assert rejected.returncode!=0 and not (out/'unsupported.webm').exists()
    (out/'unsupported-121fps.stderr').write_bytes(rejected.stderr)
    report={'scope':'Actual CEF page animation through bundled native C++ recorder; encoded pixel IDs and audio independently decoded',
            'durationMs':args.duration_ms,'nodeAvailable':False,'homebrewMediaOnPath':False,
            'supportedFpsCeiling':120,'aboveCeilingRejected':True,'cases':results,
            'requirements':{'minimumDistinctFps':args.required_distinct_fps,
                            'noRecorderDrops':args.require_no_recorder_drops,
                            'nativeGpuCapture':args.require_native_capture,
                            'completeStreamSourceContinuity':args.require_source_continuity,
                            'decodedAudioEventAlignment':args.audio_sync_proof},
            'ok':all(r['ok'] for r in results)}
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    if not report['ok']:
        raise SystemExit(1)


if __name__=='__main__':
    main()
