#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual failed-beforeAll duration boundary pinned by stock no-browser probe."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--sdk-directory', type=Path, required=True)
p.add_argument('--node', required=True)
p.add_argument('--directory', type=Path, required=True)
a = p.parse_args()
a.directory.mkdir(parents=True, exist_ok=True)
folder = Path(tempfile.mkdtemp(prefix='beforeall-duration-', dir=a.directory))
suite = folder / 'suite.mjs'
suite.write_text("import {defineNativeSuite} from " + json.dumps((a.sdk_directory.resolve() / 'native-suite.js').as_uri()) + ";\n" + """
export default defineNativeSuite({timeout:2000,hooks:{beforeAll:[{run:async()=>{console.log('BEFORE_ARMED');await new Promise(resolve=>setTimeout(resolve,200));throw Error('ACTUAL_BEFORE_FAILURE')}}]},tests:[{title:'unadmitted body duration',run:()=>console.log('INVALID_BODY')}]});
""")
report_path = folder / 'actual.json'
command = [str(a.executable.resolve()), '--node', a.node, '--host-script', str(a.sdk_directory.resolve() / 'native-runner-host.js'), '--suite', str(suite), '--report', str(report_path)]
actual = subprocess.run(command, capture_output=True, timeout=15)
(folder / 'stdout.log').write_bytes(actual.stdout)
(folder / 'stderr.log').write_bytes(actual.stderr)
r = json.loads(report_path.read_text())
assert actual.returncode == 1 and actual.stderr == b'', (actual.returncode, actual.stderr)
assert len(r['attempts']) == 1 and len(r['workers']) == 1 and r['globalErrors'] == [], r
attempt = r['attempts'][0]
assert attempt['status'] == 'failed' and not attempt['expected'] and 'ACTUAL_BEFORE_FAILURE' in attempt['error'], attempt
assert attempt['durationMs'] == 0 and attempt['wallDurationMs'] >= 200, attempt
worker = r['workers'][0]
assert worker['exitCode'] == 0 and worker['signal'] is None, worker
stdout = bytes.fromhex(worker['stdout']['data']).decode()
assert 'BEFORE_ARMED' in stdout and 'INVALID_BODY' not in stdout, stdout
assert not Path('/proc', str(worker['pid'])).exists(), worker
(folder / 'proof.json').write_text(json.dumps({'scope': 'Genuine failed beforeAll: no Test phase duration; actual wall remains; exact native worker exit0', 'command': command, 'actualExit': actual.returncode, 'attempt': attempt, 'worker': worker}, indent=2) + '\n')
print('PASS actual beforeAll-only failure duration0/wall>=200, native Error retained and worker waitpid0')
