"""Relocate Debian OBS's helper and create the isolated benchmark profile."""
from pathlib import Path
import json
import shlex

root = Path.cwd()
deps = root/'.suitecut/obs-deps/root'
plugin = deps/'usr/lib/x86_64-linux-gnu/obs-plugins/obs-ffmpeg.so'
original = b'/usr/lib/x86_64-linux-gnu/obs-plugins/obs-ffmpeg/obs-ffmpeg-mux'
relocated = str(root/'.suitecut/obs-ffmpeg-mux').encode()
if len(relocated) > len(original):
    raise RuntimeError('Checkout path too long for this fixed-size relocation; use a shorter checkout path.')
data = plugin.read_bytes()
if data.count(original) != 1 and relocated not in data:
    raise RuntimeError('Unsupported OBS binary; expected one known muxer path.')
plugin.write_bytes(data.replace(original, relocated+b'\0'*(len(original)-len(relocated))))
helper = root/'.suitecut/obs-ffmpeg-mux'
helper.unlink(missing_ok=True)
helper.write_text('#!/bin/sh\nexport LD_LIBRARY_PATH='+shlex.quote(str(deps/'usr/lib/x86_64-linux-gnu'))+'\nexec '+shlex.quote(str(deps/'usr/lib/x86_64-linux-gnu/obs-plugins/obs-ffmpeg/obs-ffmpeg-mux'))+' "$@"\n')
helper.chmod(0o755)
profile = root/'.suitecut/obs-profile/obs-studio'
(profile/'basic/profiles/Benchmark').mkdir(parents=True,exist_ok=True)
(profile/'global.ini').write_text('[General]\nFirstRun=true\nLanguage=en-US\n[Basic]\nProfile=Benchmark\nProfileDir=Benchmark\nSceneCollection=Benchmark\nSceneCollectionFile=Benchmark\n[BasicWindow]\nPreviewEnabled=false\n')
(profile/'basic/profiles/Benchmark/basic.ini').write_text('[General]\nName=Benchmark\n[Video]\nBaseCX=1280\nBaseCY=720\nOutputCX=1280\nOutputCY=720\nFPSType=0\nFPSCommon=60\n[Output]\nMode=Advanced\n[AdvOut]\nRecType=Standard\nRecFormat2=mkv\nRecEncoder=obs_x264\nRecTracks=1\nTrackIndex=1\nEncoder=obs_x264\nRescale=false\nRecRescale=false\n[Audio]\nSampleRate=48000\nChannelSetup=Stereo\n')
(profile/'basic/profiles/Benchmark/recordEncoder.json').write_text(json.dumps({'rate_control':'CRF','crf':18,'preset':'veryfast','profile':'high','keyint_sec':2}))
(profile/'basic/profiles/Benchmark/streamEncoder.json').write_text(json.dumps({'rate_control':'CBR','bitrate':4500,'preset':'veryfast','profile':'high','keyint_sec':2}))
(profile/'plugin_config/obs-websocket').mkdir(parents=True,exist_ok=True)
(profile/'plugin_config/obs-websocket/config.json').write_text(json.dumps({'server_enabled':True,'server_port':4455,'auth_required':False,'alerts_enabled':False,'first_load':False}))
