"""Prepare a two-minute MP4 and verify actual audio/video decoding."""
from pathlib import Path
import subprocess
import json
import math
import array
import hashlib
import sys

directory = Path(sys.argv[1]).resolve()
destination = Path('/workspace/shared/gofns-twitch-2-minutes.mp4')
destination.parent.mkdir(exist_ok=True)
source = directory / ('recorded.mp4' if (directory / 'recorded.mp4').exists() else 'received.flv')
with (directory / 'mp4-remux.log').open('w') as log:
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source), '-t', '120',
                    '-c', 'copy', '-movflags', '+faststart', str(destination)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
probe = subprocess.run(['ffprobe', '-v', 'error', '-show_format', '-show_streams',
                        '-of', 'json', str(destination)], capture_output=True, text=True, check=True)
metadata = json.loads(probe.stdout)
(directory / 'final-probe.json').write_text(probe.stdout)
assert 119.8 <= float(metadata['format']['duration']) <= 120.2
assert {'video', 'audio'} <= {s['codec_type'] for s in metadata['streams']}
with (directory / 'final-decode.log').open('w') as log:
    subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(destination),
                    '-map', '0:v:0', '-map', '0:a:0', '-f', 'null', '-'],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(destination), '-vn', '-ac', '1',
                      '-ar', '16000', '-f', 's16le', '-'], capture_output=True, check=True).stdout
samples = array.array('h', pcm)
rms = math.sqrt(sum(float(v) * v for v in samples) / len(samples))
assert rms > 1, 'Audio track is entirely silent'
for label, timestamp in [('start', '2'), ('middle', '60'), ('end', '118')]:
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', timestamp, '-i', str(destination),
                    '-frames:v', '1', str(directory / (label + '.jpg'))], check=True)
video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
audio = next(s for s in metadata['streams'] if s['codec_type'] == 'audio')
with destination.open('rb') as file:
    digest = hashlib.file_digest(file, 'sha256').hexdigest()
summary = {'file': str(destination), 'durationSeconds': float(metadata['format']['duration']),
           'video': {'codec': video['codec_name'], 'width': video['width'], 'height': video['height'], 'fps': video['avg_frame_rate']},
           'audio': {'codec': audio['codec_name'], 'sampleRate': audio['sample_rate'], 'channels': audio['channels'], 'rmsDbfs': 20 * math.log10(rms / 32768)},
           'fullAudioVideoDecode': 'passed', 'sha256': digest}
(directory / 'verification.json').write_text(json.dumps(summary, indent=2) + '\n')
destination.with_suffix('.mp4.sha256').write_text(digest + '  ' + destination.name + '\n')
print(json.dumps(summary))
