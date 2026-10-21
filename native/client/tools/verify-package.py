#!/usr/bin/env python3
"""Build and execute relocated static/shared C/C++ consumers with bounded workers."""
# SPDX-License-Identifier: MIT
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time

SDK = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New report/build directory')
    parser.add_argument('--static-build', type=Path, help='Reuse an existing static SDK build')
    parser.add_argument('--source-archive', type=Path, help='Build the exported source package')
    parser.add_argument('--openssl-root', type=Path)
    parser.add_argument('--workers', type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'workers': args.workers, 'commands': [], 'source': str(SDK)}
    env = os.environ.copy()
    # Tests must use installed loader metadata, not a developer's library path.
    for key in ('DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH', 'LD_LIBRARY_PATH'):
        env.pop(key, None)

    def run(*command):
        command = [str(x) for x in command]
        path = output / f'{len(report["commands"]):02d}.log'
        entry = {'command': command, 'log': str(path), 'exitCode': None}
        report['commands'].append(entry)
        print(' '.join(command), flush=True)
        start = time.monotonic()
        with path.open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, timeout=900)
        entry.update(exitCode=result.returncode, seconds=round(time.monotonic() - start, 3))
        if result.returncode:
            raise RuntimeError(f'Command failed ({result.returncode}): {path}\n{path.read_text()[-6000:]}')

    try:
        source = SDK
        if args.source_archive:
            extracted = output / 'source'
            with tarfile.open(args.source_archive) as archive:
                archive.extractall(extracted, filter='data')
            root = extracted / 'SuiteCutNative'
            manifest = json.loads((root / 'SOURCE-MANIFEST.json').read_text())
            for name, expected in manifest.items():
                path = root / name
                if not path.resolve().is_relative_to(root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                    raise ValueError(f'Source manifest mismatch: {name}')
            if (root / 'native/browser').exists():
                raise ValueError('SDK source archive unexpectedly contains CEF sources')
            source = root / 'native/client'
            report['sourceArchiveSha256'] = hashlib.sha256(args.source_archive.read_bytes()).hexdigest()
        run(shutil.which('python3'), source / 'tools/check-layout.py')
        extra = [f'-DOPENSSL_ROOT_DIR={args.openssl_root}'] if args.openssl_root else []
        for kind in ('static', 'shared'):
            build = args.static_build.resolve() if kind == 'static' and args.static_build else output / f'{kind}-build'
            if args.source_archive and kind == 'static' and args.static_build:
                raise ValueError('Cannot reuse a checkout build when verifying an exported source archive')
            shared = kind == 'shared'
            run('cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                f'-DBUILD_SHARED_LIBS={"ON" if shared else "OFF"}', '-DBUILD_TESTING=ON',
                '-DSUITECUT_CLIENT_BUILD_TESTS=ON', '-DSUITECUT_CLIENT_BUILD_EXAMPLES=ON', *extra)
            run('cmake', '--build', build, '--parallel', args.workers)
            run('ctest', '--test-dir', build, '-L', 'unit', '--parallel', 1, '--output-on-failure', '--no-tests=error')
            run('ctest', '--test-dir', build, '-L', 'architecture', '--parallel', 1, '--output-on-failure', '--no-tests=error')
            installed = output / f'{kind}-install'
            run('cmake', '--install', build, '--prefix', installed)
            relocated = output / f'{kind}-relocated'
            installed.rename(relocated)
            exports = list(relocated.rglob('*.cmake'))
            for path in exports:
                data = path.read_text()
                if str(source) in data or str(build) in data or 'suitecut_sdk_' in data:
                    raise ValueError(f'Private build path/target leaked into export: {path}')
            expected_headers = {p.name for p in (source / 'include/suitecut').iterdir() if p.is_file()}
            if {p.name for p in (relocated / 'include/suitecut').iterdir()} != expected_headers:
                raise ValueError('Installed public header inventory differs from authored inventory')
            for c_only in ((False, True) if shared else (False,)):
                consumer = output / f'{kind}-consumer-{ "c" if c_only else "cpp"}'
                run('cmake', '-S', source / 'tests/consumer', '-B', consumer, '-G', 'Ninja',
                    f'-DCMAKE_PREFIX_PATH={relocated}', f'-DSUITECUT_CONSUMER_C_ONLY={"ON" if c_only else "OFF"}', *extra)
                run('cmake', '--build', consumer, '--parallel', args.workers)
                run('ctest', '--test-dir', consumer, '--parallel', 1, '--output-on-failure', '--no-tests=error')
            run(relocated / 'bin/suitecut-native-control', '--help')
        # Verify an independent parent project's default options.
        embedded = output / 'embedded-consumer'
        run('cmake', '-S', source / 'tests/consumer', '-B', embedded, '-G', 'Ninja',
            f'-DSUITECUT_CONSUMER_SOURCE={source}', '-DCMAKE_BUILD_TYPE=Release', *extra)
        cache = (embedded / 'CMakeCache.txt').read_text()
        for key in ('SUITECUT_CLIENT_BUILD_TESTS', 'SUITECUT_CLIENT_BUILD_EXAMPLES'):
            if f'{key}:BOOL=OFF' not in cache:
                raise ValueError(f'Embedding unexpectedly enabled {key}')
        run('cmake', '--build', embedded, '--parallel', args.workers)
        run('ctest', '--test-dir', embedded, '--parallel', 1, '--output-on-failure', '--no-tests=error')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {output / "report.json"}', flush=True)


if __name__ == '__main__':
    main()
