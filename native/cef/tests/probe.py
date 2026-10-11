"""Black-box native protocol verification, not a Python SDK (standard library only)."""
import argparse
import hashlib
import http.server
import json
import math
import os
from pathlib import Path
import select
import struct
import subprocess
import tempfile
import threading
import time


def exact(pipe, count, deadline):
    data = bytearray()
    while len(data) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([pipe], [], [], max(0, remaining))[0]:
            raise TimeoutError('Native packet timed out')
        chunk = os.read(pipe.fileno(), min(count - len(data), 65536))
        if not chunk:
            raise EOFError('Native packet truncated')
        data.extend(chunk)
    return bytes(data)


def packet(process, deadline):
    size = struct.unpack('<I', exact(process.stdout, 4, deadline))[0]
    assert 2 <= size <= 16384, size
    header = json.loads(exact(process.stdout, size, deadline))
    assert header['version'] == 1
    count = header['bytes']
    assert type(count) is int and 0 <= count <= 33177600
    kind = header['type']
    assert kind in ('ready', 'response', 'event', 'frame', 'audio', 'error')
    if kind in ('ready', 'error'):
        assert count == 0
    elif kind in ('response', 'event'):
        assert count <= 16777216
    elif kind == 'audio':
        assert type(header['frames']) is int and 1 <= header['frames'] <= 4096
        assert count == header['frames'] * 4
    elif kind == 'frame':
        w, h = header['width'], header['height']
        assert type(w) is int and type(h) is int and 1 <= w <= 3840 and 1 <= h <= 2160
        fmt = header.get('pixelFormat', 'bgra')
        assert fmt in ('bgra', 'i420')
        assert fmt != 'i420' or (w % 2 == 0 and h % 2 == 0)
        assert count == (w * h * 4 if fmt == 'bgra' else w * h * 3 // 2)
        assert type(header['dropped']) in (int, float) and header['dropped'] >= 0
    if kind in ('ready', 'audio', 'frame'):
        assert math.isfinite(header['timestampMs']) and header['timestampMs'] >= 0
    return header, exact(process.stdout, count, deadline)


def send(process, request):
    line = (json.dumps(request, separators=(',', ':')) + '\n').encode()
    assert len(line) <= 4194304
    process.stdin.write(line)
    process.stdin.flush()


def response(process, request_id):
    deadline = time.monotonic() + 15
    while True:
        header, payload = packet(process, deadline)
        assert header['type'] != 'error', header
        if header['type'] == 'response' and header['id'] == request_id:
            return header, json.loads(payload) if payload else None


def cdp(process, request_id, method, params=None, session=None):
    # Deliberately separate the two request ID spaces.
    cdp_id = request_id + 10000
    message = {'id': cdp_id, 'method': method, 'params': params or {}}
    if session:
        message['sessionId'] = session
    send(process, {'version': 1, 'id': request_id, 'command': 'cdp', 'message': message})
    deadline = time.monotonic() + 15
    acknowledged, result = False, None
    while not acknowledged or result is None:
        header, payload = packet(process, deadline)
        assert header['type'] != 'error', header
        if header['type'] == 'response' and header['id'] == request_id:
            assert header['ok'], header
            acknowledged = True
        elif header['type'] == 'event':
            value = json.loads(payload)
            if value.get('id') == cdp_id:
                assert 'error' not in value, value
                assert value.get('sessionId') == session
                result = value['result']
    return result


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        html = b'''<!doctype html><canvas width="640" height="360"></canvas><script>
const c=document.querySelector('canvas').getContext('2d');let n=0;
function draw(){c.fillStyle='rgb('+(n++%200)+',80,160)';c.fillRect(0,0,640,360);requestAnimationFrame(draw)}draw();
const a=new AudioContext({sampleRate:48000});const o=a.createOscillator();
const g=a.createGain();g.gain.value=.2;o.connect(g).connect(a.destination);o.start();a.resume();
</script>'''
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(html)))
        self.end_headers()
        self.wfile.write(html)

    def log_message(self, *args):
        pass


def run(executable, out, fmt, url, malformed=False):
    label = 'malformed-command' if malformed else fmt
    with tempfile.TemporaryDirectory(prefix='suitecut-cef-probe-') as profile, (out / f'{label}-stderr.log').open('wb') as stderr:
        args = [str(executable), '--suitecut-width=640', '--suitecut-height=360',
                '--suitecut-scale=1', '--suitecut-fps=30', f'--suitecut-pixel-format={fmt}',
                '--suitecut-graphics=auto', '--suitecut-webrtc-policy=default',
                f'--suitecut-profile={profile}', '--suitecut-frame-subscriptions']
        process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr)
        try:
            ready, payload = packet(process, time.monotonic() + 30)
            assert ready['type'] == 'ready' and not payload, ready
            assert ready['nativeMedia'] is False, ready
            assert ready['frameSubscriptions'] is True and ready['audioSubscriptions'] is True
            if malformed:
                process.stdin.write(b'{"version":999,"id":1,"command":"audio.status"}\n')
                process.stdin.flush()
                deadline = time.monotonic() + 10
                while True:
                    header, _ = packet(process, deadline)
                    if header['type'] == 'error':
                        assert header['error'] == 'Invalid native command', header
                        break
                return {'case': label, 'terminalProtocolError': True}
            for request_id, command in [(1, 'frames'), (2, 'audio')]:
                send(process, {'version': 1, 'id': request_id, 'command': command, 'enabled': False})
                assert response(process, request_id)[0]['ok']
            send(process, {'version': 1, 'id': 3, 'command': 'media.encoders'})
            unavailable, _ = response(process, 3)
            assert unavailable['ok'] is False and 'CEF-only' in unavailable['error']
            version = cdp(process, 4, 'Browser.getVersion')
            context = cdp(process, 5, 'Target.createBrowserContext')['browserContextId']
            target = cdp(process, 6, 'Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
            session = cdp(process, 7, 'Target.attachToTarget', {'targetId': target, 'flatten': True})['sessionId']
            result = cdp(process, 8, 'Runtime.evaluate', {'expression': '21*2', 'returnByValue': True}, session)
            assert result['result']['value'] == 42, result
            cdp(process, 9, 'Target.disposeBrowserContext', {'browserContextId': context})
            send(process, {'version': 1, 'id': 10, 'command': 'navigate', 'url': url})
            assert response(process, 10)[0]['ok']
            for request_id, command in [(11, 'frames'), (12, 'audio')]:
                send(process, {'version': 1, 'id': request_id, 'command': command, 'enabled': True})
                assert response(process, request_id)[0]['ok']
            deadline = time.monotonic() + 15
            hashes, audio, peaks = set(), 0, []
            previous = {}
            while len(hashes) < 3 or audio < 12:
                header, payload = packet(process, deadline)
                kind = header['type']
                assert kind != 'error', header
                if kind in ('frame', 'audio'):
                    assert header['timestampMs'] >= previous.get(kind, 0)
                    previous[kind] = header['timestampMs']
                if kind == 'frame':
                    assert header['pixelFormat'] == fmt
                    assert (header['width'], header['height']) == (640, 360)
                    hashes.add(hashlib.sha256(payload).hexdigest())
                elif kind == 'audio':
                    audio += 1
                    peaks.append(max(abs(sample[0]) for sample in struct.iter_unpack('<h', payload)))
            assert max(peaks) > 1000
            return {'case': label, 'ready': ready, 'browser': version, 'distinctFrames': len(hashes),
                    'audioPackets': audio, 'peakAudioSample': max(peaks),
                    'cdpContextSessionRoundTrip': True, 'mediaRejected': True}
        finally:
            process.stdin.close()
            process.stdin = None
            try:
                # Drain pending complete packets while the writer shuts down.
                process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise AssertionError('Native graceful shutdown timed out')
            assert process.returncode == 0, process.returncode
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path(os.environ.get('SUITECUT_NATIVE_EXECUTABLE', '/missing')))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/'
        cases = [run(args.executable, args.output, fmt, url) for fmt in ('bgra', 'i420')]
        cases.append(run(args.executable, args.output, 'bgra', url, malformed=True))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    result = {'nativeMedia': False, 'cases': cases, 'allPassed': True}
    (args.output / 'probe-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
