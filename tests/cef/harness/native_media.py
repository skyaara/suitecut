#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Drive the real C++ media workflow and decode its output independently."""
import argparse
import functools
import http.server
import json
from pathlib import Path
import struct
import subprocess
import threading

from contracts import require, load_json

p=argparse.ArgumentParser(description=__doc__)
for key in ('client','engine','ffmpeg','ffprobe','output'):
    p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args()
out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
def tool(binary,args,timeout=40):
    result=subprocess.run([str(binary),*map(str,args)],capture_output=True,timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{binary} exit {result.returncode}: {result.stderr[-8192:].decode(errors="replace")}')
    return result.stdout
clip=out/'fixture.webm'
tool(a.ffmpeg,['-nostdin','-v','error','-f','lavfi','-i','testsrc2=size=640x360:rate=30',
    '-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-t','12','-c:v','libvpx-vp9',
    '-threads','2','-deadline','realtime','-cpu-used','8','-b:v','600k','-c:a','libopus','-y',clip])
(out/'index.html').write_text('<!doctype html><style>body{margin:0;background:#122}video{width:100%}</style><video src=/fixture.webm autoplay loop controls></video>')
class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*_args):
        pass
    def end_headers(self):
        self.send_header('Connection','close')
        super().end_headers()
server=http.server.HTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(out)))
server.timeout=1
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
result={'ok':False,'checks':[]}
try:
    process=subprocess.run([str(a.client),str(a.engine),str(a.ffmpeg),f'http://127.0.0.1:{server.server_port}/',str(out/'cpp')],capture_output=True,timeout=100)
    (out/'client-stdout.log').write_bytes(process.stdout);(out/'client-stderr.log').write_bytes(process.stderr)
    require(process.returncode == 0, process.stdout[-8192:].decode(errors='replace'))
    native=load_json((out/'cpp/results.json').read_bytes());require(native['ok'] is True, str(native))
    video=out/'cpp/playback.webm'
    streams=json.loads(tool(a.ffprobe,['-v','error','-count_frames','-show_streams','-of','json',video]))['streams']
    v=next(s for s in streams if s['codec_type']=='video');audio=next(s for s in streams if s['codec_type']=='audio')
    require(int(v['nb_read_frames']) == native['recording']['outputFrames'], 'Decoded/native frame count mismatch')
    pcm=tool(a.ffmpeg,['-nostdin','-v','error','-i',video,'-map','0:a:0','-ac','1','-ar','48000','-f','f32le','pipe:1'])
    require(len(pcm) > 48000*4, 'Missing decoded audio samples')
    energy=sum(s*s for s, in struct.iter_unpack('<f',pcm));rms=(energy/(len(pcm)//4))**.5
    require(rms > .01, f'Silent decoded output: {rms}')
    for name in ('static.mp4','static-audio.mp4'):
        path=out/'cpp'/name
        require(path.is_file(), f'Missing required static recording: {path}')
        probe=json.loads(tool(a.ffprobe,['-v','error','-count_frames','-show_streams','-of','json',path]))
        require(int(next(s for s in probe['streams'] if s['codec_type']=='video')['nb_read_frames']) >= 120, f'Insufficient decoded static frames: {path}')
    result.update(ok=True,checks=native['checks']+['Decoded frame count equals native outputFrames','Decoded real browser audio RMS exceeds 0.01'],streams=streams,audioRms=rms,native=native,
        boundary='Short media lifecycle/decoding regression; not sustained unique 1080p120 qualification.')
except Exception as error:
    result['error']=str(error)
finally:
    server.shutdown();server.server_close();thread.join(timeout=3)
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['ok'] else 1)
