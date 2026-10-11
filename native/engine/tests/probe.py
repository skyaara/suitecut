"""Verify combined native A/V outputs over plain IPC; this is not a Python SDK."""
import argparse
import http.server
import json
import math
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time

from protocol_probe import Fixture, packet, response, send


def inspect(path, out):
    metadata = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)
    ], timeout=20))
    streams = metadata['streams']
    audio = next(s for s in streams if s['codec_type'] == 'audio')
    video = next(s for s in streams if s['codec_type'] == 'video')
    assert (video['width'], video['height']) == (640, 360)
    assert int(audio['sample_rate']) == 48000 and audio['channels'] == 2
    assert float(metadata['format']['duration']) > 1
    packets = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-show_packets', '-show_entries',
        'packet=stream_index,pts_time,duration_time', '-of', 'json', str(path)
    ], timeout=20))['packets']
    tracks = {}
    for stream in (audio, video):
        values = [p for p in packets if p['stream_index'] == stream['index'] and 'pts_time' in p]
        pts = [float(p['pts_time']) for p in values]
        assert len(pts) > 10 and all(b >= a for a, b in zip(pts, pts[1:])), pts
        end = max(float(p['pts_time']) + float(p.get('duration_time', 0)) for p in values)
        tracks[stream['codec_type']] = {'firstPtsSeconds': pts[0], 'endSeconds': end, 'packets': len(values)}
    initial_skew = abs(tracks['audio']['firstPtsSeconds'] - tracks['video']['firstPtsSeconds'])
    end_skew = abs(tracks['audio']['endSeconds'] - tracks['video']['endSeconds'])
    assert initial_skew <= .12 and end_skew <= .12, tracks
    pcm = subprocess.check_output([
        'ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:a:0', '-f', 's16le',
        '-ar', '48000', '-ac', '2', 'pipe:1'
    ], timeout=20)
    peak = max(abs(v[0]) for v in struct.iter_unpack('<h', pcm))
    assert peak > 1000
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:v:0',
                    '-f', 'null', '-'], check=True, timeout=20)
    (out / f'{path.name}.ffprobe.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return {'file': str(path), 'videoCodec': video['codec_name'], 'audioCodec': audio['codec_name'],
            'tracks': tracks, 'initialSkewMs': initial_skew * 1000, 'endSkewMs': end_skew * 1000,
            'peakAudioSample': peak, 'decodedVideo': True}


def wait_listening(port, process):
    deadline = time.monotonic() + 10
    needle = ':' + f'{port:04X}'
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('RTMP receiver exited')
        for line in Path('/proc/net/tcp').read_text().splitlines()[1:]:
            fields = line.split()
            if fields[1].endswith(needle) and fields[3] == '0A':
                return
        time.sleep(.025)
    raise TimeoutError('RTMP receiver failed to listen')


def run(executable, out, url):
    with tempfile.TemporaryDirectory(prefix='suitecut-native-probe-') as profile, (out / 'stderr.log').open('wb') as stderr:
        process = subprocess.Popen([str(executable), '--suitecut-width=640', '--suitecut-height=360',
                                    '--suitecut-scale=1', '--suitecut-fps=30', '--suitecut-pixel-format=i420',
                                    '--suitecut-graphics=auto', '--suitecut-webrtc-policy=default',
                                    f'--suitecut-profile={profile}', '--suitecut-frame-subscriptions'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr)
        counter = 0
        def command(name, **fields):
            nonlocal counter
            counter += 1
            send(process, {'version': 1, 'id': counter, 'command': name, **fields})
            header, value = response(process, counter)
            assert header['ok'], header
            return value
        try:
            ready, _ = packet(process, time.monotonic() + 30)
            assert ready['type'] == 'ready' and ready['nativeMedia'] is True
            command('frames', enabled=False)
            command('audio', enabled=False)
            command('navigate', url=url)
            encoders = command('media.encoders')['encoders']
            assert all(any(e['name'] == n and e['linked'] for e in encoders)
                       for n in ['libx264', 'aac', 'ffv1', 'pcm_s16le', 'libvpx-vp9', 'libopus'])
            results = []
            for filename, vc, ac in [('recording.mp4', 'libx264', 'aac'),
                                     ('lossless.mkv', 'ffv1', 'pcm_s16le'),
                                     ('recording.webm', 'libvpx-vp9', 'libopus'),
                                     ('stream.flv', 'libx264', 'aac')]:
                path = out / filename
                receiver = None
                receiver_log = None
                try:
                    streaming = filename.endswith('.flv')
                    if streaming:
                        with socket.socket() as probe:
                            probe.bind(('127.0.0.1', 0))
                            port = probe.getsockname()[1]
                        destination = f'rtmp://127.0.0.1:{port}/live/native'
                        receiver_log = (out / 'receiver.log').open('wb')
                        receiver = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-listen', '1',
                                                     '-i', destination, '-c', 'copy', str(path)],
                                                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=receiver_log)
                        wait_listening(port, receiver)
                    else:
                        destination = str(path)
                    start = command('media.start', destination=destination, width=640, height=360,
                                    fps=30, audio=True, streaming=streaming, videoCodec=vc, audioCodec=ac)
                    assert start['audioReadinessRequired'] is True and start['audioReadiness'] == 'engine'
                    oid = start['outputId']
                    deadline = time.monotonic() + 12
                    while True:
                        status = command('media.status', outputId=oid)
                        assert not status['error'], status
                        if status['ready']:
                            break
                        assert time.monotonic() < deadline, status
                        time.sleep(.05)
                    time.sleep(1.2)
                    if filename == 'recording.mp4':
                        paused = command('media.pause', outputId=oid, paused=True)
                        assert paused['paused']
                        time.sleep(.2)
                        resumed = command('media.pause', outputId=oid, paused=False)
                        assert not resumed['paused']
                    time.sleep(1.2)
                    final = command('media.stop', outputId=oid)
                    assert final['stopped'] and not final['error']
                    assert final['videoFrames'] > 20 and final['audioSamples'] > 48000
                    assert final['queuedVideoBytes'] == 0 and final['queuedAudioBytes'] == 0
                    if receiver:
                        assert receiver.wait(15) == 0
                    metadata = command('media.probe', input=str(path))
                    assert metadata['audio'] and metadata['width'] == 640 and metadata['height'] == 360
                    results.append({'nativeStatus': final, 'nativeProbe': metadata, **inspect(path, out)})
                finally:
                    if receiver and receiver.poll() is None:
                        receiver.kill()
                        receiver.wait()
                    if receiver_log:
                        receiver_log.close()
            # Invalid readiness bypass and incompatible codecs are explicit failures.
            for fields in [{'audioStartup': 'immediate'}, {'videoCodec': 'libx264', 'audioCodec': 'aac', 'container': 'webm'}]:
                counter += 1
                send(process, {'version': 1, 'id': counter, 'command': 'media.start',
                               'destination': str(out / 'invalid.webm'), 'width': 640, 'height': 360, 'fps': 30, **fields})
                assert response(process, counter)[0]['ok'] is False
            return {'nativeMedia': True, 'allPassed': True, 'ready': ready, 'outputs': results,
                    'pauseResume': True, 'invalidOptionsRejected': True}
        finally:
            process.stdin.close()
            process.stdin = None
            try:
                process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise AssertionError('Native shutdown timed out')
            assert process.returncode == 0, process.returncode
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path(os.environ.get('SUITECUT_NATIVE_EXECUTABLE', '/missing')))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        result = run(args.executable, out, f'http://127.0.0.1:{server.server_port}/')
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    (out / 'probe-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
