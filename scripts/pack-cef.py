"""Package the standalone native CEF integration; never publish or overwrite."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'native/cef'
SOURCES = [
    'CMakeLists.txt', 'browser.cc', 'main_linux.cc', 'main_mac.mm', 'helper_mac.cc',
    'Info.plist.in', 'audio_clock.h', 'audio_trace.h', 'memory_bounds.h',
    'audio_clock_test.cc', 'pixel_test.cc', 'cdp_json.h', 'cdp_json_test.cc', 'cef-build.json',
    'cef-build-linux.json', 'cef-build-linux-codecs.json', 'libyuv-build.json',
]
DOCS = ['README.md', 'PROTOCOL.md', 'protocol.schema.json', 'manifest.json']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def archive(output, entries, prefix):
    inventory = []
    for name, path in sorted(entries.items()):
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Expected a regular file: {path}')
        inventory.append({'path': name, 'bytes': path.stat().st_size, 'sha256': digest(path)})
    manifest = (json.dumps(inventory, indent=2) + '\n').encode()
    # x mode deliberately rejects collisions with an existing release.
    with output.open('xb') as file, gzip.GzipFile(filename='', mode='wb', fileobj=file, mtime=0, compresslevel=6) as compressed, tarfile.open(fileobj=compressed, mode='w') as package:
        for name, path in sorted(entries.items()):
            info = package.gettarinfo(str(path), arcname=f'{prefix}/{name}')
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = 0
            info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
            with path.open('rb') as stream:
                package.addfile(info, stream)
        info = tarfile.TarInfo(f'{prefix}/FILES.json')
        info.size = len(manifest)
        info.mode = 0o644
        info.mtime = 0
        package.addfile(info, io.BytesIO(manifest))
    return {'file': output.name, 'bytes': output.stat().st_size, 'sha256': digest(output), 'files': inventory}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary-dir', type=Path)
    parser.add_argument('--verification', type=Path)
    args = parser.parse_args()
    manifest = json.loads((CORE / 'manifest.json').read_text())
    name = f"{manifest['name']}-{manifest['version']}"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    expected = [output / f'{name}-source.tar.gz', output / 'packages.json', output / 'SHA256SUMS']
    if args.binary_dir:
        expected.append(output / f'{name}-linux-x86_64.tar.gz')
    if any(path.exists() for path in expected):
        raise ValueError('Destination contains package artifacts; use a new output directory')
    entries = {f'native/browser/{file}': ROOT / 'native/browser' / file for file in SOURCES}
    entries.update({file: CORE / file for file in DOCS})
    entries['CMakeLists.txt'] = CORE / 'CMakeLists.txt'
    entries['LICENSE'] = ROOT / 'LICENSE'
    entries['scripts/install-cef-codecs.py'] = ROOT / 'scripts/install-cef-codecs.py'
    entries['tests/probe.py'] = CORE / 'tests/probe.py'
    if args.verification:
        for file in args.verification.rglob('*'):
            if file.is_file():
                entries[f'verification/{file.relative_to(args.verification)}'] = file
    runtime = None
    if args.binary_dir:
        binary = args.binary_dir.resolve()
        report = json.loads((args.verification / 'summary.json').read_text()) if args.verification else None
        executable = binary / 'suitecut-browser'
        if not report or report.get('allPassed') is not True or report.get('nativeMedia') is not False or report.get('binarySha256') != digest(executable):
            raise ValueError('Binary packaging requires matching CEF-only verification evidence')
        runtime = {'runtime/suitecut-browser': executable}
        # Only CEF runtime files, not object files, build caches, tests or encoders.
        pin = json.loads((ROOT / 'native/browser/cef-build-linux-codecs.json').read_text())
        if digest(binary / 'libcef.so') != pin['runtimeLibrarySha256']:
            raise ValueError('Runtime library does not match the pinned CEF distribution')
        sdk = ROOT / '.suitecut/cef-codecs-sdk'
        for directory in ['Release', 'Resources']:
            for file in (sdk / directory).rglob('*'):
                if file.is_file():
                    relative = file.relative_to(sdk / directory)
                    bundled = binary / relative
                    if digest(bundled) != digest(file):
                        raise ValueError(f'Built runtime differs from pinned SDK: {relative}')
                    runtime[f'runtime/{relative}'] = bundled
        for file in ['CEF-LICENSE.txt', 'CEF-CREDITS.html', 'libyuv-LICENSE.txt']:
            runtime[f'runtime/{file}'] = binary / file
        runtime.update({file: CORE / file for file in DOCS})
        runtime['LICENSE'] = ROOT / 'LICENSE'
        runtime.update({key: value for key, value in entries.items() if key.startswith('verification/')})
        for file in ['cef-build-linux-codecs.json', 'libyuv-build.json']:
            runtime[f'native/browser/{file}'] = ROOT / 'native/browser' / file
    records = [archive(output / f'{name}-source.tar.gz', entries, name)]
    if runtime is not None:
        records.append(archive(output / f'{name}-linux-x86_64.tar.gz', runtime, name))
    (output / 'packages.json').write_text(json.dumps({'package': manifest, 'archives': records}, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(f"{record['sha256']}  {record['file']}\n" for record in records))
    for record in records:
        print(f"{record['file']}: {record['bytes']} bytes, SHA-256 {record['sha256']}")


if __name__ == '__main__':
    main()
