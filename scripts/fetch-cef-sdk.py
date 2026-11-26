#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Download one pinned CEF binary SDK, check it against native/browser/cef-build.json
and extract it to a new folder.

  python3 scripts/fetch-cef-sdk.py --pin linux64 --dest .suitecut/cef-sdk
  python3 scripts/fetch-cef-sdk.py --pin macosarm64Official --dest .suitecut/cef-official-sdk

A folder that already holds the same pin is kept, so CI caches can reuse it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
PINS = json.loads((ROOT / 'native/browser/cef-build.json').read_text())
MARKER = '.suitecut-cef-pin.json'


def pin(name):
    if name == 'linux64':
        return {key: PINS[key] for key in ('url', 'sha256', 'size')}
    if name in ('macosarm64', 'macosarm64Official'):
        return {key: PINS[name][key] for key in ('url', 'sha256', 'size')}
    raise ValueError(f'Unknown pin {name}')


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--pin', required=True, choices=['linux64', 'macosarm64', 'macosarm64Official'])
    parser.add_argument('--dest', required=True, type=Path)
    args = parser.parse_args()
    wanted = {'pin': args.pin, 'cefVersion': PINS['cefVersion'], **pin(args.pin)}
    dest = args.dest.resolve()
    marker = dest / MARKER
    if marker.exists() and json.loads(marker.read_text()) == wanted:
        print(f'{dest} already holds {args.pin}')
        return
    if dest.exists():
        raise SystemExit(f'{dest} exists and holds another SDK; remove it first')
    work = dest.with_name(dest.name + '.download')
    shutil.rmtree(work, ignore_errors=True)
    (work / 'x').mkdir(parents=True)
    archive = work / 'cef.tar.bz2'
    with urllib.request.urlopen(wanted['url'], timeout=600) as response, archive.open('wb') as out:
        shutil.copyfileobj(response, out)
    if archive.stat().st_size != wanted['size'] or digest(archive) != wanted['sha256']:
        raise SystemExit('The CEF archive differs from the pin in native/browser/cef-build.json')
    with tarfile.open(archive) as tar:
        tar.extractall(work / 'x', filter='data')
    (top,) = list((work / 'x').iterdir())
    top.rename(dest)
    shutil.rmtree(work)
    marker.write_text(json.dumps(wanted, indent=2) + '\n')
    print(f'{dest}: {args.pin} {wanted["sha256"]}')


if __name__ == '__main__':
    sys.exit(main())
