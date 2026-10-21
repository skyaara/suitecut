#!/usr/bin/env python3
"""Create a deterministic CEF-free native SDK source archive; never overwrite."""
# SPDX-License-Identifier: MIT
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile

SDK = Path(__file__).resolve().parents[1]
ROOT = SDK.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY_NOTICES.md', ROOT / 'native/README.md']
    files += [SDK / name for name in ('CMakeLists.txt', 'CMakePresets.json', 'README.md',
                                     'VALIDATION.md', 'ARCHITECTURE.md')]
    for directory in ('src', 'include', 'cmake', 'examples', 'tests', 'tools', 'skills'):
        files += [p for p in (SDK / directory).rglob('*') if p.is_file()
                  and '__pycache__' not in p.parts and p.suffix != '.pyc']
    files += list((SDK.parent / 'shared').glob('*.h'))
    payload = {}
    for path in sorted(files):
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise ValueError(f'Unexpected source symlink: {path}')
        payload[str(path.relative_to(ROOT))] = path.read_bytes()
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in payload.items()}
    payload['SOURCE-MANIFEST.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    with args.output.open('xb') as output:
        with gzip.GzipFile(fileobj=output, mode='wb', filename='', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as archive:
                for name, data in sorted(payload.items()):
                    info = tarfile.TarInfo('SuiteCutNative/' + name)
                    info.size = len(data)
                    info.mode = 0o644
                    info.mtime = 0
                    archive.addfile(info, io.BytesIO(data))
    print(json.dumps({'archive': str(args.output.resolve()), 'files': len(payload),
                      'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest()}))


if __name__ == '__main__':
    main()
