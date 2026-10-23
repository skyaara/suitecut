#!/usr/bin/env python3
"""Build and install both SDK tarballs in an isolated consumer, then run Song Maker."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='New qualification directory; refuses an existing path')
    args = parser.parse_args()
    engine = Path(os.environ.get('SUITECUT_NATIVE_EXECUTABLE', ''))
    if not engine.is_absolute() or not engine.is_file() or not os.access(engine, os.X_OK):
        parser.error('Set SUITECUT_NATIVE_EXECUTABLE to the absolute built CEF executable')
    for command in ('node', 'npm', 'pnpm'):
        if not shutil.which(command):
            parser.error(f'{command} is required on PATH')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = (args.output or ROOT / '.suitecut/examples/song-maker' / stamp).resolve()
    output.mkdir(parents=True, exist_ok=False)
    consumer = Path(tempfile.mkdtemp(prefix='suitecut-song-maker-'))
    setup = {'passed': False, 'engine': str(engine), 'consumer': str(consumer),
             'output': str(output), 'commands': [], 'packages': []}

    def run(command, cwd=ROOT, env=None, capture=False):
        command = [str(value) for value in command]
        print(f'Running in {cwd}: ' + ' '.join(command), flush=True)
        entry = {'command': command, 'cwd': str(cwd), 'exitCode': None}
        setup['commands'].append(entry)
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE if capture else None, check=False)
        entry['exitCode'] = result.returncode
        result.check_returncode()
        return result.stdout

    try:
        run(['pnpm', 'build'])
        run(['pnpm', 'build:browser-control'])
        # npm pack runs with --ignore-scripts below, so run the prepack LFS guard here.
        run(['pnpm', 'check:lfs'])
        for project in (ROOT, ROOT / 'packages/browser-control'):
            packed = json.loads(run(['npm', 'pack', '--ignore-scripts', '--json',
                                     '--pack-destination', output], cwd=project, capture=True))[0]
            archive = output / packed['filename']
            setup['packages'].append({'name': packed['name'], 'version': packed['version'],
                                      'path': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
        for name in ('package.json', 'run.mjs', 'verify-media.mjs', '.env.example', 'README.md',
                     'eslint.config.mjs'):
            shutil.copy2(ROOT / 'examples/song-maker' / name, consumer / name)
        run(['npm', 'install', '--no-audit', '--no-fund', '--save-exact',
             *[item['path'] for item in setup['packages']]], cwd=consumer)
        setup['engineSha256'] = hashlib.sha256(engine.read_bytes()).hexdigest()
        env = {**os.environ, 'SUITECUT_EXAMPLE_OUTPUT': str(output / 'artifacts')}
        # A temporary consumer has no ancestor SuiteCut workspace. Do not permit
        # NODE_PATH to accidentally satisfy an omitted packaged dependency.
        env.pop('NODE_PATH', None)
        run(['npm', 'start'], cwd=consumer, env=env)
        report = json.loads((output / 'artifacts/report.json').read_text())
        if report.get('passed') is not True:
            raise RuntimeError('Example returned without a passing media/application report')
        setup['passed'] = True
    finally:
        (output / 'setup.json').write_text(json.dumps(setup, indent=2) + '\n')
        print(f'Preserved consumer: {consumer}\nQualification artifacts: {output}', flush=True)


if __name__ == '__main__':
    main()
