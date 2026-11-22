#!/usr/bin/env python3
"""Check a macOS embed kit (scripts/package-embed-kit-macos.py) as an app
would ship it.

Static checks on the kit itself:
  - kit.json lists every file, with the right size and sha256;
  - every Mach-O links only system libraries and @rpath/@loader_path/
    @executable_path, and needs no macOS newer than kit.json's minimum; the
    engine, the session and the helpers are built for exactly that minimum;
  - codesign --verify --deep --strict passes for the engine, the session, the
    framework and each helper, each has the hardened runtime, and each has
    exactly the entitlements kit.json names.

Then the kit is copied with `cp -R`, as Napdock installs it, the copy's
signatures are checked again, and everything that runs uses the copy:
  - suitecut-native-cef_host_events and suitecut-native-cef_embed_frames (the
    commands `ctest -L embed-host-events` and `ctest -L embed-frames` run);
  - scripts/verify-embed-sample-macos.py;
  - a protocol 0 smoke test the way Napdock's browserhost speaks today:
    `suitecut-session ... --browser KIT/suitecut-browser.app/Contents/MacOS/
    suitecut-browser ... start` (browserhost's path for a kit engine), then
    tabs open, list, view screenshot and tabs close on the socket, then close.

usage: scripts/verify-embed-kit-macos.py --kit .suitecut/embed-kit
           --client-build .suitecut/kit-client-build --output NEW_DIR
"""
import argparse
import atexit
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parent.parent
SYSTEM = ('/usr/lib/', '/System/Library/')
RELATIVE = ('@rpath/', '@loader_path/', '@executable_path/')
PAGE_COLOUR = (34, 102, 170)
PAGE = ('<!doctype html><title>kit smoke</title>'
        '<style>html,body{margin:0;height:100%;background:rgb(34,102,170)}</style><p>kit</p>').encode()


class Report:
    def __init__(self):
        self.checks = []

    def check(self, ok, name, **detail):
        self.checks.append({'name': name, 'ok': bool(ok), **detail})
        print(('ok   ' if ok else 'FAIL ') + name + (f'  {json.dumps(detail)[:300]}' if detail and not ok else ''),
              flush=True)
        return ok


_LINKS = None


def plain(binary):
    """otool reads `name(member)` as an archive member: give it a plain name."""
    global _LINKS
    binary = Path(binary)
    if '(' not in str(binary):
        return binary
    if _LINKS is None:
        _LINKS = Path(tempfile.mkdtemp(prefix='suitecut-otool-'))
        atexit.register(shutil.rmtree, _LINKS, True)
    link = _LINKS / hashlib.sha256(str(binary).encode()).hexdigest()[:16]
    if not link.exists():
        link.symlink_to(binary)
    return link


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def output_of(*command):
    result = subprocess.run([str(x) for x in command], text=True, capture_output=True)
    return result.returncode, result.stdout + result.stderr


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')


def version_tuple(text):
    return tuple(int(x) for x in text.split('.'))


def static_checks(report, kit, manifest):
    listed = {entry['path']: entry for entry in manifest['files']}
    present = {str(p.relative_to(kit)) for p in kit.rglob('*') if (p.is_file() or p.is_symlink())} - {'kit.json'}
    report.check(set(listed) == present, 'kit.json lists exactly the files in the kit',
                 missing=sorted(present - set(listed))[:10], extra=sorted(set(listed) - present)[:10])
    odd = [str(p.relative_to(kit)) for p in kit.rglob('*') if p.is_symlink()
           or any(part.startswith('.') for part in p.relative_to(kit).parts)
           or not re.fullmatch(r'[A-Za-z0-9 ()._@+/-]+', str(p.relative_to(kit)))]
    report.check(not odd, 'no symlinks, hidden names or unusual characters (what Napdock accepts)', odd=odd[:10])
    report.check(all(set(entry) == {'path', 'size', 'sha256'} for entry in manifest['files']),
                 'each kit.json file entry is {path, size, sha256}')
    wrong = [path for path, entry in listed.items() if 'sha256' in entry and (kit / path).is_file()
             and ((kit / path).stat().st_size != entry['size'] or digest(kit / path) != entry['sha256'])]
    report.check(not wrong, "every file's size and sha256 match kit.json", wrong=wrong[:10])

    minimum = manifest['minimumMacOS']
    foreign, newer = [], []
    machos = sorted(p for p in kit.rglob('*') if macho(p))
    for binary in machos:
        _, text = output_of('/usr/bin/otool', '-L', plain(binary))
        own = output_of('/usr/bin/otool', '-D', plain(binary))[1].splitlines()[1:]
        for line in text.splitlines()[1:]:
            name = line.strip().split(' (compatibility')[0]
            if name and name not in own and not name.startswith(SYSTEM + RELATIVE):
                foreign.append(f'{binary.relative_to(kit)}: {name}')
        found = re.findall(r'minos (\S+)', output_of('/usr/bin/vtool', '-show-build', plain(binary))[1])
        if not found or version_tuple(found[0]) > version_tuple(minimum):
            newer.append(f'{binary.relative_to(kit)}: {found}')
    report.check(not foreign, f'otool: the {len(machos)} Mach-O files link only system libraries', foreign=foreign[:10])
    report.check(not newer, f'vtool: none needs macOS newer than {minimum}', newer=newer)
    layout = manifest['layout']
    ours = [layout['engine'], layout['session']] + [
        f'{h}/Contents/MacOS/{Path(h).stem}' for h in layout['helpers']]
    builds = {name: re.findall(r'minos (\S+)', output_of('/usr/bin/vtool', '-show-build', plain(kit / name))[1]) for name in ours}
    report.check(all(found == [minimum] for found in builds.values()),
                 f'the engine, the session and the helpers are built for macOS {minimum}', builds=builds)

    for name, entitlements in manifest['signing']['entitlements'].items():
        path = kit / name
        code, text = output_of('/usr/bin/codesign', '--verify', '--deep', '--strict', path)
        report.check(code == 0, f'codesign --verify --deep --strict: {name}', output=text.strip())
        _, text = output_of('/usr/bin/codesign', '-dv', '--verbose=2', path)
        flags = re.search(r'flags=0x([0-9a-f]+)', text)
        team = re.search(r'TeamIdentifier=(\S+)', text)
        report.check(flags and int(flags.group(1), 16) & 0x10000, f'hardened runtime: {name}', flags=flags and flags.group(0))
        report.check(team and team.group(1) == manifest['signing']['teamIdentifier'],
                     f'signed by team {manifest["signing"]["teamIdentifier"]}: {name}', team=team and team.group(1))
        text = subprocess.run(['/usr/bin/codesign', '-d', '--entitlements', '-', '--xml', str(path)],
                              text=True, capture_output=True).stdout
        start = text.find('<?xml')
        actual = plistlib.loads(text[start:].encode()) if start >= 0 else {}
        expected = plistlib.loads((ROOT / entitlements).read_bytes()) if entitlements else {}
        report.check(actual == expected, f'entitlements as kit.json says: {name}', actual=actual, expected=expected)


def run_test(report, name, command, artifacts, timeout=300):
    artifacts.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    result = subprocess.run([str(x) for x in command], text=True, capture_output=True, timeout=timeout)
    (artifacts / 'output.txt').write_text(result.stdout + result.stderr)
    summary = {'exitCode': result.returncode, 'seconds': round(time.monotonic() - started, 1)}
    results = artifacts / 'results.json'
    if results.exists():
        data = json.loads(results.read_text())
        checks = data.get('checks', [])
        summary.update({'checks': len(checks), 'failed': [c['name'] for c in checks if not c['ok']],
                        'error': data.get('error')})
    return report.check(result.returncode == 0, name, **summary)


class Page(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)

    def log_message(self, *args):
        pass


def png_pixel(data, x, y):
    """The RGB at (x, y) of an 8-bit, non-interlaced RGB or RGBA PNG."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    offset, chunks, header = 8, [], None
    while offset < len(data):
        length, kind = struct.unpack('>I4s', data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            chunks.append(body)
        offset += 12 + length
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError(f'unsupported PNG: depth {depth}, colour {colour}')
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(b''.join(chunks))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for row in range(y + 1):
        kind = raw[row * (stride + 1)]
        line = bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        previous = line
    return (width, height), tuple(previous[x * channels:x * channels + 3])


def call(path, request, timeout=60):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(timeout)
        connection.connect(str(path))
        body = json.dumps(request).encode()
        connection.sendall(struct.pack('<I', len(body)) + body)
        head = b''
        while len(head) < 4:
            more = connection.recv(4 - len(head))
            if not more:
                raise ConnectionError('the session closed the connection')
            head += more
        (length,) = struct.unpack('<I', head)
        reply = b''
        while len(reply) < length:
            more = connection.recv(length - len(reply))
            if not more:
                raise ConnectionError('the session closed the connection')
            reply += more
    return json.loads(reply)


def napdock_environment():
    # What browserhost gives the session (Runtime.environment).
    environment = {'HOME': os.environ['HOME'], 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin'}
    for key in ('USER', 'LOGNAME'):
        if key in os.environ:
            environment[key] = os.environ[key]
    environment['TMPDIR'] = os.confstr('CS_DARWIN_USER_TEMP_DIR') if 'CS_DARWIN_USER_TEMP_DIR' in os.confstr_names \
        else tempfile.gettempdir()
    return environment


def engines_of(kit):
    code, text = output_of('/usr/bin/pgrep', '-f', str(kit) + '/')
    return [int(x) for x in text.split()] if code == 0 else []


def protocol0_smoke(report, kit, artifacts):
    artifacts.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Page)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_address[1]}/'
    session_dir = Path(tempfile.mkdtemp(prefix='sc-kit-', dir='/tmp'))  # short: sun_path
    profile = Path(tempfile.mkdtemp(prefix='sc-kit-profile-', dir='/tmp'))
    tool = kit / 'suitecut-session'
    name = 'kitsmoke'
    environment = napdock_environment()
    owner = 'session-kit-smoke'
    try:
        started = subprocess.run([tool, '--session', name, '--session-dir', session_dir, '--browser',
                                  kit / 'suitecut-browser.app/Contents/MacOS/suitecut-browser', '--profile', profile, 'start'],
                                 env=environment, text=True, capture_output=True, timeout=60)
        if not report.check(started.returncode == 0, 'protocol 0: the session starts as browserhost starts it',
                            exitCode=started.returncode, stderr=started.stderr[-500:]):
            return
        sock = session_dir / f'{name}.sock'
        reply = call(sock, {'owner': owner, 'tabs': {'op': 'open', 'url': url}})
        result = reply.get('result') or {}
        page = result.get('page', (result.get('tab') or {}).get('page'))
        if not report.check(reply.get('ok') and isinstance(page, int), 'protocol 0: tabs open', reply=reply):
            return
        listed, deadline = None, time.monotonic() + 20
        while time.monotonic() < deadline:
            listed = call(sock, {'owner': owner, 'tabs': {'op': 'list'}})
            tabs = (listed.get('result') or {}).get('tabs', [])
            if any(t.get('page') == page and t.get('url') == url and t.get('title') == 'kit smoke' for t in tabs):
                break
            time.sleep(0.25)
        tabs = (listed.get('result') or {}).get('tabs', [])
        report.check(any(t.get('page') == page and t.get('url') == url for t in tabs),
                     'protocol 0: tabs list shows the page with its url and title', tabs=tabs)
        shot = call(sock, {'owner': owner, 'view': {'op': 'screenshot', 'page': page}})
        image = base64.b64decode((shot.get('result') or {}).get('base64', ''))
        (artifacts / 'screenshot.png').write_bytes(image)
        try:
            dimensions, colour = png_pixel(image, 20, 60)
        except Exception as error:  # noqa: BLE001 - reported as a failed check
            dimensions, colour = None, str(error)
        report.check(shot.get('ok') and isinstance(colour, tuple) and all(abs(a - b) <= 8 for a, b in zip(colour, PAGE_COLOUR)),
                     'protocol 0: view screenshot is a PNG of the page', size=dimensions, pixel=colour)
        closed = call(sock, {'owner': owner, 'tabs': {'op': 'close', 'page': page}})
        after = call(sock, {'owner': owner, 'tabs': {'op': 'list'}})
        report.check(closed.get('ok') and not (after.get('result') or {}).get('tabs'),
                     'protocol 0: tabs close leaves no tab', closed=closed, after=after)
    finally:
        subprocess.run([tool, '--session', name, '--session-dir', session_dir, 'close'], env=environment,
                       capture_output=True, timeout=30)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and ((session_dir / f'{name}.sock').exists() or engines_of(kit)):
            time.sleep(0.2)
        report.check(not (session_dir / f'{name}.sock').exists() and not engines_of(kit),
                     'protocol 0: close ends the session and its engine')
        server.shutdown()
        shutil.rmtree(session_dir, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--kit', type=Path, required=True)
    parser.add_argument('--client-build', type=Path, required=True,
                        help='Client build with suitecut-native-cef_host_events, _embed_frames and suitecut-embed-sample')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--static-only', action='store_true', help='Skip the copy and everything that runs')
    args = parser.parse_args()
    kit = args.kit.resolve(strict=True)
    client = args.client_build.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((kit / 'kit.json').read_text())
    report = Report()
    static_checks(report, kit, manifest)
    if not args.static_only:
        # Napdock copies the folder; check and run the copy.
        holder = Path(tempfile.mkdtemp(prefix='suitecut-kit-copy-'))
        copy = holder / 'helpers'
        subprocess.run(['/bin/cp', '-R', str(kit), str(copy)], check=True)
        try:
            for name in manifest['signing']['entitlements']:
                code, text = output_of('/usr/bin/codesign', '--verify', '--deep', '--strict', copy / name)
                report.check(code == 0, f'cp -R copy: codesign --verify --deep --strict: {name}', output=text.strip())
            run_test(report, 'cef_host_events against the copy',
                     [client / 'suitecut-native-cef_host_events', copy, output / 'host-events'], output / 'host-events')
            run_test(report, 'cef_embed_frames against the copy',
                     [client / 'suitecut-native-cef_embed_frames', copy, output / 'embed-frames'], output / 'embed-frames')
            sample = subprocess.run([sys.executable, ROOT / 'scripts/verify-embed-sample-macos.py', '--sample',
                                     client / 'suitecut-embed-sample', '--runtime', copy, '--output', output / 'sample'],
                                    text=True, capture_output=True, timeout=300)
            report.check(sample.returncode == 0, 'verify-embed-sample-macos.py against the copy',
                         output=(sample.stdout + sample.stderr)[-800:])
            protocol0_smoke(report, copy, output / 'protocol0')
        finally:
            shutil.rmtree(holder, ignore_errors=True)
    failed = [c['name'] for c in report.checks if not c['ok']]
    (output / 'report.json').write_text(json.dumps({'passed': not failed, 'kit': str(kit), 'checks': report.checks},
                                                   indent=2) + '\n')
    print(('PASS' if not failed else f'FAIL ({len(failed)})') + f': {output / "report.json"}')
    sys.exit(0 if not failed else 1)


if __name__ == '__main__':
    main()
