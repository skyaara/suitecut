"""Stage the pinned upstream tests with an explicit SuiteCut CEF fixture override."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('archive', type=pathlib.Path)
parser.add_argument('destination', type=pathlib.Path)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
expected = 'f6b38805b0b021423bb908cd7682a455283c22742bf7d95c190927f3fb11172c'
if hashlib.sha256(args.archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('Upstream archive does not match the tested v1.62.1 snapshot')
destination = args.destination.resolve()
destination.mkdir(parents=True, exist_ok=False)
with tarfile.open(args.archive) as archive:
    archive.extractall(destination, filter='data')
upstream = destination / 'playwright-1.62.1'
deps = destination / 'runtime-deps'
subprocess.run(['npm', 'install', '--prefix', str(deps), '--ignore-scripts', '--no-audit', '--no-fund',
                'mime@4.1.0', 'pngjs@7.0.0', 'jpeg-js@0.4.4', 'dotenv@17.4.2', 'colors@1.4.0',
                'diff@9.0.0', 'formidable@2.1.5', 'get-stream@9.0.1', 'debug@4.4.3'], check=True)
modules = upstream / 'node_modules'
modules.mkdir()
for parent in [root / 'node_modules', deps / 'node_modules']:
    for item in parent.iterdir():
        if item.name.startswith('.'):
            continue
        if item.name.startswith('@'):
            scope = modules / item.name
            scope.mkdir(exist_ok=True)
            for package in item.iterdir():
                target = scope / package.name
                if not target.exists():
                    target.symlink_to(package.resolve())
        elif not (modules / item.name).exists():
            (modules / item.name).symlink_to(item.resolve())
paths = json.loads(subprocess.check_output(['node', '-e', """
const path = require('node:path');
const playwright = require.resolve('playwright/package.json');
const core = require.resolve('playwright-core/package.json', { paths: [playwright] });
if (require(playwright).version !== '1.62.1' || require(core).version !== '1.62.1')
  throw new Error('Use the tested Playwright 1.62.1 packages');
console.log(JSON.stringify({playwright:path.dirname(playwright),core:path.dirname(core)}));
"""], cwd=root, text=True))
if not (modules / 'playwright-core').exists():
    (modules / 'playwright-core').symlink_to(paths['core'])
(upstream / 'packages/playwright-core/lib').symlink_to(pathlib.Path(paths['core']) / 'lib')
(upstream / 'packages/playwright/lib').symlink_to(pathlib.Path(paths['playwright']) / 'lib')
evidence = root / 'benchmarks/playwright-compatibility/2026-10-11'
subprocess.run(['patch', '-p1', '-i', str(evidence / 'upstream-fixture.patch')], cwd=upstream, check=True)
fixture = upstream / 'tests/config/browserTest.ts'
fixture.write_text(fixture.read_text().replace('/workspace/suitecut/src/engine', str(root / 'src/engine')))
(upstream / 'suitecut.config.ts').write_text((evidence / 'tested-sources/upstream.config.ts.txt').read_text())
core = evidence / 'tested-sources/upstream-core.config.ts.txt'
if core.exists():
    (upstream / 'suitecut-core.config.ts').write_text(core.read_text())
print(upstream / 'suitecut.config.ts')
