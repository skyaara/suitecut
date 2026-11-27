#!/usr/bin/env python3
"""Drive suitecut-embed-sample against a real runtime and compare its snapshot.

The sample is the macOS host built on suitecut/embed.h alone
(native/client/examples/embed-macos). This script serves a fixed page on a
free local port and starts the sample under its own session name, a short
session folder and a fresh profile. It clicks the page's input and types into
it, takes the sample's snapshot of the frame on screen, and checks that the
sample quit cleanly and took its session with it. Then it compares the
snapshot with the checked-in reference.

Comparison: both images are scaled to CSS size (1024x684) with sips. A pixel
differs when any channel differs by more than --pixel-threshold (default 40
of 255). The run passes when at most --max-differing (default 0.5%) of pixels
differ. Before that, four probe points must have their expected colours: the
background, the "clicked" block, the "typed" block, and the page's own frame
counter block. They show whether the click and the text reached the page,
independent of font rendering.

To regenerate the reference after an intended change to the page, the sample
or the engine, run with --update-reference on a Mac whose run you have
checked. The probes must pass first, then the snapshot is copied over the
reference. Commit the new PNG with a note of the macOS and runtime versions.

usage: scripts/verify-embed-sample-macos.py --sample BUILD/suitecut-embed-sample
           --runtime /path/to/SuiteCut.app --output NEW_DIR
           [--signing-requirement REQ] [--update-reference]
"""
import argparse
import http.server
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'native/client/examples/embed-macos/reference/snapshot.png'
CSS_WIDTH, CSS_HEIGHT = 1024, 684  # the sample's 1024x720 window less its bar
CLICK = (200, 75)
TYPED = 'hello embed'

# Fixed layout, no animation and a transparent caret, so the frame is stable.
PAGE = '''<!doctype html><meta charset=utf-8><title>embed sample check</title>
<style>
html,body{margin:0;height:100%;background:#f4f1ea;font:24px Helvetica,Arial,sans-serif}
#box{position:absolute;left:40px;top:40px;width:520px;height:70px;font:32px Helvetica,Arial,sans-serif;caret-color:transparent;outline:none;border:2px solid #555}
.block{position:absolute;top:180px;width:300px;height:200px;background:#999}
#clicked{left:40px}#typed{left:380px}
#painted{position:absolute;left:720px;top:180px;width:200px;height:200px;background:#c33}
</style>
<input id=box autocomplete=off spellcheck=false>
<div class=block id=clicked></div><div class=block id=typed></div><div id=painted></div>
<script>
const expected=EXPECTED;
addEventListener('mousedown',()=>{document.getElementById('clicked').style.background='#2266aa'});
document.getElementById('box').addEventListener('input',e=>{
  if(e.target.value===expected)document.getElementById('typed').style.background='#22aa55'});
requestAnimationFrame(()=>{document.getElementById('painted').style.background='#7a3fb0'});
</script>
'''.replace('EXPECTED', json.dumps(TYPED))

# (name, CSS point, expected RGB): what the page shows after a good run.
PROBES = [
    ('background', (980, 600), (0xf4, 0xf1, 0xea)),
    ('clicked block (the click reached the page)', (190, 280), (0x22, 0x66, 0xaa)),
    ('typed block (the text reached the input)', (530, 280), (0x22, 0xaa, 0x55)),
    ('painted block (the page ran its script)', (820, 280), (0x7a, 0x3f, 0xb0)),
]
PROBE_TOLERANCE = 24


def read_bmp(path):
    data = path.read_bytes()
    if data[:2] != b'BM':
        raise ValueError(f'{path} is not a BMP')
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    bits, compression = struct.unpack_from('<HI', data, 28)
    if bits not in (24, 32) or compression not in (0, 3):
        raise ValueError(f'{path}: unsupported BMP ({bits} bits, compression {compression})')
    step = bits // 8
    stride = (width * step + 3) & ~3
    rows = []
    for row in range(abs(height)):
        source = abs(height) - 1 - row if height > 0 else row  # bottom-up unless the height is negative
        start = offset + source * stride
        line = data[start:start + width * step]
        rows.append([(line[i + 2], line[i + 1], line[i]) for i in range(0, width * step, step)])
    return width, abs(height), rows


def css_pixels(png, work, name):
    """The PNG scaled to CSS size, as rows of RGB tuples."""
    bmp = work / f'{name}.bmp'
    subprocess.run(['/usr/bin/sips', '-z', str(CSS_HEIGHT), str(CSS_WIDTH), '-s', 'format', 'bmp', str(png),
                    '--out', str(bmp)], check=True, capture_output=True, timeout=60)
    width, height, rows = read_bmp(bmp)
    if (width, height) != (CSS_WIDTH, CSS_HEIGHT):
        raise ValueError(f'{png} scaled to {width}x{height}, expected {CSS_WIDTH}x{CSS_HEIGHT}')
    return rows


def png_size(path):
    data = path.read_bytes()[:24]
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path} is not a PNG')
    return struct.unpack('>II', data[16:24])


def processes_mentioning(text):
    listing = subprocess.run(['/bin/ps', '-axww', '-o', 'pid=,command='], capture_output=True, text=True, check=True)
    return [line.strip() for line in listing.stdout.splitlines() if text in line]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sample', required=True, type=Path, help='The built suitecut-embed-sample')
    parser.add_argument('--runtime', required=True, type=Path, help='A SuiteCut runtime: an .app or its directory')
    parser.add_argument('--output', required=True, type=Path, help='New directory for the report and images')
    parser.add_argument('--signing-requirement', help='Code requirement the runtime must meet')
    parser.add_argument('--reference', type=Path, default=REFERENCE)
    parser.add_argument('--pixel-threshold', type=int, default=40)
    parser.add_argument('--max-differing', type=float, default=0.005)
    parser.add_argument('--exit-after', type=float, default=8.0)
    parser.add_argument('--update-reference', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('The sample host is macOS only')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'checks': [], 'sample': str(args.sample.resolve()), 'runtime': str(args.runtime.resolve()),
              'reference': str(args.reference), 'macos': platform.mac_ver()[0],
              'tolerance': {'pixelThreshold': args.pixel_threshold, 'maxDiffering': args.max_differing,
                            'probe': PROBE_TOLERANCE}}

    def check(ok, name, **detail):
        report['checks'].append({'name': name, 'ok': bool(ok), **detail})
        if not ok:
            raise AssertionError(f'{name}: {detail}')

    page = PAGE.encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(page)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(page)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_address[1]}/'
    # Short, so the socket path fits sun_path; private, from mkdtemp.
    sessions = Path(tempfile.mkdtemp(prefix='sc-sample-', dir='/tmp'))
    profile = Path(tempfile.mkdtemp(prefix='suitecut-sample-profile-'))
    session = 'verify-sample'
    snapshot = output / 'snapshot.png'
    try:
        command = [str(args.sample), '--runtime', str(args.runtime.resolve()), '--session', session,
                   '--session-dir', str(sessions), '--profile', str(profile), '--url', url,
                   '--click', f'{CLICK[0]},{CLICK[1]}', '--type', TYPED, '--snapshot', str(snapshot),
                   '--exit-after', str(args.exit_after)]
        if args.signing_requirement:
            command[3:3] = ['--signing-requirement', args.signing_requirement]
        report['command'] = command
        started = time.monotonic()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('DYLD_', 'SUITECUT_'))}
        result = subprocess.run(command, capture_output=True, text=True, timeout=args.exit_after + 120, env=env)
        (output / 'sample.stderr').write_text(result.stderr)
        report['sampleSeconds'] = round(time.monotonic() - started, 3)
        check(result.returncode == 0, 'the sample exits 0', exitCode=result.returncode, stderr=result.stderr[-2000:])
        if args.signing_requirement:
            check('signature verified' in result.stderr, 'the runtime meets the signing requirement')
        check(snapshot.is_file(), 'the sample wrote its snapshot')
        width, height = png_size(snapshot)
        check((width, height) == (CSS_WIDTH * 2, CSS_HEIGHT * 2), 'the snapshot is the view at scale 2',
              width=width, height=height)

        # Clean shutdown: the socket is gone and nothing runs with this profile.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and processes_mentioning(str(profile)):
            time.sleep(0.2)
        left = processes_mentioning(str(profile))
        check(not left, 'no session or engine process remains after quit', remaining=left)
        check(not (sessions / f'{session}.sock').exists(), "the session's socket is removed")

        work = output / 'compare'
        work.mkdir()
        actual = css_pixels(snapshot, work, 'actual')
        for name, (x, y), expected in PROBES:
            got = actual[y][x]
            check(all(abs(a - b) <= PROBE_TOLERANCE for a, b in zip(got, expected)), f'probe: {name}',
                  point=[x, y], expected=list(expected), got=list(got))

        if args.update_reference:
            args.reference.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(snapshot, args.reference)
            report['referenceUpdated'] = True
            check(True, 'reference updated from this run')
        else:
            check(args.reference.is_file(), 'a reference snapshot exists', path=str(args.reference))
            reference = css_pixels(args.reference, work, 'reference')
            differing = 0
            for actual_row, reference_row in zip(actual, reference):
                for a, b in zip(actual_row, reference_row):
                    if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) > args.pixel_threshold:
                        differing += 1
            fraction = differing / (CSS_WIDTH * CSS_HEIGHT)
            check(fraction <= args.max_differing, 'the snapshot matches the reference within tolerance',
                  differingPixels=differing, fraction=round(fraction, 6))
        report['passed'] = True
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        server.shutdown()
        if processes_mentioning(str(profile)):
            # The sample left its session running: close it before removing the profile.
            runtime = args.runtime.resolve()
            service = runtime / 'Contents/MacOS/suitecut-session' if runtime.suffix == '.app' else runtime / 'suitecut-session'
            subprocess.run([str(service), '--session', session, '--session-dir', str(sessions), 'close'],
                           capture_output=True, timeout=30)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and processes_mentioning(str(profile)):
                time.sleep(0.2)
            report['closedLeftoverSession'] = True
        shutil.rmtree(sessions, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {output / "report.json"}', flush=True)


if __name__ == '__main__':
    main()
