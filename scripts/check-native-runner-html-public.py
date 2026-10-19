# SPDX-License-Identifier: MIT
"""Exercise native HTML with actual callback failures, retries and process exit."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--node', type=Path, required=True)
parser.add_argument('--host-script', type=Path, required=True)
parser.add_argument('--suite-api', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
args = parser.parse_args()
directory = args.directory.resolve()
directory.mkdir(parents=True, exist_ok=True)
suite = directory / 'suite.mjs'
suite.write_text("import assert from 'node:assert/strict';\nimport process from 'node:process';\nimport {Buffer} from 'node:buffer';\nimport {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ";\n" + r'''
export default defineNativeSuite({
  fixtures: { worker: { scope: 'worker', setup: async ({}, use) => { await use({pid: process.pid}); } } },
  tests: [
    { title: 'native retry </summary><script>throw 1</script>', fixtures: ['worker'], retries: 1,
      run: async ({worker}, info) => {
        assert.equal(worker.pid, process.pid);
        process.stdout.write(Buffer.from([0, 255, 60, 62]));
        if (!info.retry) throw new Error('actual first attempt <img src=x onerror=throw>');
      } },
    { title: 'native UTF16 '+String.fromCharCode(0xd800), expectedFailure: true,
      run: async () => { process.stderr.write(Buffer.from([254, 0, 10])); throw new Error('actual expected failure '+String.fromCharCode(0)); } }
  ]
});
''')
command = [str(args.executable.resolve()), '--node', str(args.node.resolve()),
           '--host-script', str(args.host_script.resolve()), '--suite', str(suite),
           '--report', str(directory / 'report.json'), '--html', str(directory / 'report.html')]
actual = subprocess.run(command, capture_output=True, timeout=20)
(directory / 'stdout.log').write_bytes(actual.stdout)
(directory / 'stderr.log').write_bytes(actual.stderr)
assert actual.returncode == 0, (actual.returncode, actual.stderr)
assert actual.stderr == b''
report = json.loads((directory / 'report.json').read_bytes())
assert report['summary']['tests'] == 2 and report['summary']['attempts'] == 3
assert report['summary']['flaky'] == 1 and report['summary']['unexpected'] == 0
assert report['summary']['globalErrors'] == 0
assert len(report['workers']) >= 2
for worker in report['workers']:
    assert worker['exitStatusKnown']
    try:
        os.kill(worker['pid'], 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError(('actual callback worker still exists', worker))
assert any(bytes.fromhex(row['stdout']['data']).startswith(bytes([0, 255, 60, 62])) for row in report['workers'])
assert any(bytes.fromhex(row['stderr']['data']) == bytes([254, 0, 10]) for row in report['workers'])
decoder = Path(__file__).with_name('check-native-runner-html.py')
spec = importlib.util.spec_from_file_location('native_html_decoder', decoder)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
decoded = module.check(directory)

# Each public front door must forward the real HTML destination to C++. A small
# actual test suffices here; retry and byte-stream semantics were checked above.
simple = directory / 'frontdoor-suite.mjs'
simple.write_text("import {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ";\nexport default defineNativeSuite({tests:[{title:'actual public front door',run:async()=>{}}]});\n")
frontdoors = []
for mode in ('cli', 'api'):
    folder = directory / mode
    folder.mkdir(exist_ok=True)
    options = dict(executablePath=str(args.executable.resolve()), suitePath=str(simple),
                   reportPath=str(folder / 'report.json'), htmlPath=str(folder / 'report.html'))
    if mode == 'cli':
        entry = args.host_script.resolve().with_name('native-runner-cli.js')
        launch = [str(args.node.resolve()), str(entry), '--executable', options['executablePath'],
                  '--suite', options['suitePath'], '--report', options['reportPath'], '--html', options['htmlPath']]
    else:
        api = args.host_script.resolve().with_name('native.js')
        entry = folder / 'launch.mjs'
        entry.write_text("import {runNativeSuite} from " + json.dumps(api.as_uri()) + ";\nconst actual=await runNativeSuite(" + json.dumps(options) + ");if(actual.exitCode!==0||actual.signal!==null)process.exitCode=1;\n")
        launch = [str(args.node.resolve()), str(entry)]
    invoked = subprocess.run(launch, capture_output=True, timeout=10)
    (folder / 'stdout.log').write_bytes(invoked.stdout)
    (folder / 'stderr.log').write_bytes(invoked.stderr)
    assert invoked.returncode == 0 and not invoked.stderr, (mode, invoked.returncode, invoked.stderr)
    public_report = json.loads((folder / 'report.json').read_bytes())
    assert public_report['summary']['tests'] == 1 and public_report['summary']['attempts'] == 1
    assert public_report['summary']['unexpected'] == 0 and not public_report['globalErrors']
    assert all(worker['exitStatusKnown'] for worker in public_report['workers'])
    for worker in public_report['workers']:
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(('public callback worker still exists', mode, worker))
    frontdoors.append({'mode': mode, 'actualProcess': 'PASS', 'decoded': module.check(folder)})

collision = directory / 'collision.json'
collision.write_bytes(b'preserve existing report')
bad = command.copy()
bad[bad.index('--report') + 1] = str(collision)
bad[bad.index('--html') + 1] = str(directory / 'nested' / '..' / 'collision.json')
rejected = subprocess.run(bad, capture_output=True, timeout=5)
assert rejected.returncode == 2 and b'must be distinct' in rejected.stderr
assert collision.read_bytes() == b'preserve existing report'
result = {'nativeCLI': 'PASS', 'actualCallbackAttempts': 3, 'workersExited': len(report['workers']),
          'independentDecode': decoded, 'distinctOutputAdmission': 'PASS', 'publicFrontdoors': frontdoors}
(directory / 'proof-report.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
