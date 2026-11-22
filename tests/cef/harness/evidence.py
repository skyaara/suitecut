# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
import sys

from .paths import ROOT, SUITE
from .contracts import require, load_json, check_contract


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_reports(directory, case, seed=None):
    """Validate the declared report only; unrelated JSON cannot satisfy a case."""
    contract = case['evidence']
    path = directory / contract['file']
    require(path.resolve().is_relative_to(directory.resolve()), 'Report escaped attempt directory')
    require(path.is_file() and path.stat().st_size <= 8 * 1024 * 1024, f'Missing or oversized declared evidence: {path}')
    return check_contract(load_json(path.read_bytes()), contract)


def pin_inputs(args, cases):
    paths = {args.engine, Path(__file__).resolve(), Path(sys.executable).resolve(),
             ROOT/'scripts/run-cef-regressions.py'}
    for dependency in ('ffmpeg','ffprobe'):
        if getattr(args,dependency,None):
            paths.add(Path(getattr(args,dependency)).resolve())
    paths.update(p for p in SUITE.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    paths.update(p for p in (ROOT / 'native/client/tests').rglob('*') if p.is_file())
    paths.update(p for p in (ROOT / 'native/client/src').rglob('*') if p.is_file())
    paths.update(p for p in (ROOT / 'native/client/include').rglob('*') if p.is_file())
    paths.update(p for p in (ROOT / 'native/client/cmake').rglob('*') if p.is_file())
    paths.update(p for p in (ROOT / 'native/shared').rglob('*') if p.is_file())
    paths.update(p for p in (ROOT / 'native/browser').rglob('*')
                 if p.is_file() and '__pycache__' not in p.parts)
    paths.add(ROOT / 'native/client/CMakeLists.txt')
    paths.add(ROOT / 'native/client/CMakePresets.json')
    if args.client_build:
        paths.update(args.client_build.glob('libsuitecut_client.*'))
        paths.update(args.client_build / c['binary'] for c in cases)
    # Capture the actual CEF renderer helper as well as the main process.
    if sys.platform == 'darwin' and args.engine.parent.name == 'MacOS':
        paths.update(p for p in args.engine.parents[1].rglob('*') if p.is_file())
        cef=args.engine.parents[1]/'Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework'
        if cef.is_file():
            paths.add(cef)
    elif (args.engine.parent/'libcef.so').is_file():
        paths.add(args.engine.parent/'libcef.so')
    return {str(path): sha(path) for path in sorted(paths) if path.is_file()}
