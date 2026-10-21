#!/usr/bin/env python3
"""Compile every private runtime module source without PCH, one worker at a time."""
# SPDX-License-Identifier: MIT
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def without_pch(args):
    result = []
    i = 0
    while i < len(args):
        value = args[i]
        if value == '-Xclang' and args[i + 1] in ('-include-pch', '-include'):
            if args[i + 2] != '-Xclang':
                raise ValueError('Unrecognized forced include command')
            i += 4
        elif value in ('-o', '-MF', '-MT', '-MQ', '-include', '-include-pch'):
            i += 2
        elif value in ('-c', '-MD', '-MMD', '-MP', '-fpch-preprocess', '-Winvalid-pch'):
            i += 1
        else:
            result.append(value)
            i += 1
    return result + ['-fsyntax-only']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New report directory')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = json.loads((args.build / 'compile_commands.json').read_text())
    sources = {p.resolve() for folder in ('protocol', 'runtime', 'page', 'process')
               for p in (ROOT / folder).glob('*.cc') if p.name != 'precompiled.cc'}
    selected = [row for row in rows if Path(row['file']).resolve() in sources]
    if {Path(row['file']).resolve() for row in selected} != sources or len(selected) != len(sources):
        raise ValueError('Compile database does not cover each module source exactly once')
    report = {'passed': False, 'workers': 1, 'planned': len(selected), 'results': []}
    try:
        for i, row in enumerate(sorted(selected, key=lambda x: x['file'])):
            command = without_pch(row.get('arguments') or shlex.split(row['command']))
            if any('cmake_pch' in value or value == '-include-pch' for value in command):
                raise ValueError('A PCH argument survived filtering')
            log = args.output / f'{i:02d}.log'
            entry = {'file': row['file'], 'command': command, 'log': str(log), 'exitCode': None}
            report['results'].append(entry)
            print(f'Checking {i + 1}/{len(selected)}: {row["file"]}', flush=True)
            start = time.monotonic()
            with log.open('wb') as stream:
                result = subprocess.run(command, cwd=row['directory'], stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=180)
            entry.update(exitCode=result.returncode, seconds=round(time.monotonic() - start, 3))
            if result.returncode:
                raise RuntimeError(f'Source depends on forced includes or fails standalone: {log}')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
