"""Stage CEF, the native engine and FFmpeg's additional runtime dependency closure."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def dependencies(path, env):
    text = subprocess.check_output(['ldd', str(path)], env=env, text=True)
    if 'not found' in text:
        raise RuntimeError('Unresolved runtime dependency: ' + text)
    result = {}
    for line in text.splitlines():
        match = re.match(r'\s*(\S+) => (/\S+) \(', line)
        if match:
            result[match[1]] = Path(match[2]).resolve()
    return result


def package_owner(path):
    owner = subprocess.check_output(['dpkg-query', '-S', str(path)], text=True).splitlines()[0].split(': ', 1)[0]
    fields = subprocess.check_output(['dpkg-query', '-W', '-f',
                                     '${binary:Package}\t${Version}\t${source:Package}\t${source:Version}', owner], text=True).split('\t')
    return {'package': fields[0], 'version': fields[1], 'sourcePackage': fields[2], 'sourceVersion': fields[3]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise ValueError('Use a new staging directory; existing runtimes are preserved')
    out.mkdir(parents=True)
    binary = args.binary_dir.resolve()
    executable = binary / 'suitecut-browser'
    dynamic = subprocess.check_output(['readelf', '-d', str(executable)], text=True)
    if 'Library rpath: [$ORIGIN:$ORIGIN/lib]' not in dynamic:
        raise ValueError('Build with SUITECUT_BUNDLE_RUNTIME=ON before staging')
    sdk = ROOT / '.suitecut/cef-codecs-sdk'
    pin = json.loads((ROOT / 'native/browser/cef-build-linux-codecs.json').read_text())
    if sha(binary / 'libcef.so') != pin['runtimeLibrarySha256']:
        raise ValueError('CEF library differs from the verified codec-enabled pin')
    for directory in ['Release', 'Resources']:
        for path in (sdk / directory).rglob('*'):
            if path.is_file():
                relative = path.relative_to(sdk / directory)
                compiled = binary / relative
                if sha(compiled) != sha(path):
                    raise ValueError(f'CEF runtime differs from SDK: {relative}')
                target = out / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(compiled, target)
    shutil.copy2(executable, out / 'suitecut-browser')
    for filename in ['CEF-LICENSE.txt', 'CEF-CREDITS.html', 'libyuv-LICENSE.txt']:
        shutil.copy2(binary / filename, out / filename)
    env = dict(os.environ, LD_LIBRARY_PATH=str(ROOT / '.suitecut/obs-deps/root/usr/lib/x86_64-linux-gnu'))
    baseline = dependencies(binary / 'libcef.so', env)
    baseline.update({name: None for name in ['libstdc++.so.6', 'libm.so.6', 'libgcc_s.so.1', 'libc.so.6', 'libdl.so.2', 'libpthread.so.0']})
    full = dependencies(executable, env)
    media = {name: path for name, path in full.items() if name not in baseline and not (out / name).is_file() and name != 'libcef.so'}
    libs = out / 'lib'
    libs.mkdir()
    notices = out / 'licenses'
    notices.mkdir()
    packages = {}
    records = []
    for name, path in sorted(media.items()):
        owner = package_owner(path)
        key = owner['package'].split(':')[0]
        packages[key] = owner
        copyright = Path('/usr/share/doc') / key / 'copyright'
        if not copyright.is_file():
            raise ValueError(f'Missing package license: {key}')
        notice_dir = notices / key
        notice_dir.mkdir(exist_ok=True)
        shutil.copy2(copyright, notice_dir / 'copyright')
        shutil.copy2(path, libs / name)
        records.append({'name': name, 'path': 'lib/' + name, 'sha256': sha(path),
                        'bytes': path.stat().st_size, 'origin': str(path), **owner})
    for name in ['GPL-2', 'GPL-3', 'LGPL-2.1', 'LGPL-3', 'Apache-2.0', 'BSD', 'MPL-2.0', 'CC0-1.0']:
        path = Path('/usr/share/common-licenses') / name
        if path.is_file():
            shutil.copy2(path, notices / name)
    (notices / 'packages.json').write_text(json.dumps(list(packages.values()), indent=2) + '\n')
    remaining = []
    # Verify the staged binary resolves every bundled dependency from this tree.
    staged = dependencies(out / 'suitecut-browser', env)
    for name in media:
        if staged[name] != (libs / name).resolve():
            raise RuntimeError(f'Bundled library did not resolve locally: {name}: {staged[name]}')
    for name, path in staged.items():
        if not path.is_relative_to(out):
            remaining.append({'name': name, 'resolvedOnBuildHost': str(path)})
    manifest = {'bundled': records, 'systemDependencies': remaining,
                'cefSha256': sha(out / 'libcef.so'), 'binarySha256': sha(out / 'suitecut-browser')}
    (out / 'runtime-dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'output': str(out), 'bundledMediaLibraries': len(records),
                      'mediaLibraryBytes': sum(r['bytes'] for r in records),
                      'remainingSystemLibraries': len(remaining)}, indent=2))


if __name__ == '__main__':
    main()
