#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual global-stop cleanup callbacks; soft mismatch uses a controlled producer."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default='node')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
args.directory = Path(tempfile.mkdtemp(prefix="run-", dir=args.directory))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node
inputs = [args.executable.resolve(), Path(node).resolve(), Path(__file__).resolve()] + [p for p in sdk.rglob('*') if p.is_file()]
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in inputs}
(args.directory / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
prelude = """import assert from 'node:assert/strict';import process from 'node:process';
import {defineNativeSuite} from SUITE;
import {runNativeSoftAssertion} from SOFT;
import {nativeMatcherFailure} from PRODUCER;
const emit=event=>process.stdout.write(event+'\\n');
""".replace('SUITE', json.dumps((sdk / 'native-suite.js').as_uri())).replace('SOFT', json.dumps((sdk / 'native-soft-assertions.js').as_uri())).replace('PRODUCER', json.dumps((sdk / 'native-matcher-failure.js').as_uri()))
results = []
try:
    for soft in [False, True]:
        name = 'actual-afterEach-soft-after-global' if soft else 'actual-afterEach-after-global'
        folder = args.directory / name
        folder.mkdir()
        cleanup = "assert.equal(i.status,'interrupted');emit('ACTUAL_AFTER_EACH');"
        if soft:
            cleanup += "const original=new Error('ACTUAL_CLEANUP_MISMATCH');await runNativeSoftAssertion(async()=>{throw nativeMatcherFailure(original)});assert.strictEqual(i.errors[0],original);emit('ACTUAL_SOFT_ACK_CONTINUED');"
        source = prelude + "export default defineNativeSuite({hooks:{afterEach:[{run:async(_,i)=>{" + cleanup + "}}]},tests:[{title:'actual original global stop',timeout:0,expectedFailure:true,run:async(_,i)=>{const aborted=new Promise((_,reject)=>i.signal.addEventListener('abort',()=>{emit('ACTUAL_BODY_ABORT');reject(i.signal.reason)},{once:true}));emit('ACTUAL_BODY_READY');await aborted;}}]});\n"
        suite = folder / 'suite.mjs'
        suite.write_text(source)
        command = [str(args.executable.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(folder / 'report.json'), '--global-timeout', '1000']
        (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        actual = subprocess.run(command, capture_output=True, timeout=12)
        (folder / 'stdout.bin').write_bytes(actual.stdout)
        (folder / 'stderr.bin').write_bytes(actual.stderr)
        assert actual.returncode == 1 and actual.stderr == b'', (name, actual.returncode, actual.stderr)
        report = json.loads((folder / 'report.json').read_bytes())
        assert len(report['attempts']) == 1 and report['attempts'][0]['status'] == 'interrupted' and not report['attempts'][0]['expected'], report
        assert report['globalErrors'] == ['Native suite original global timeout of 1000ms exceeded'], report
        workers = report['workers']
        assert len(workers) == 1 and workers[0]['exitStatusKnown'] and workers[0]['exitCode'] == 0 and workers[0]['signal'] is None and workers[0]['stderr']['byteLength'] == 0, workers
        assert not Path('/proc', str(workers[0]['pid'])).exists(), workers
        stdout = bytes.fromhex(workers[0]['stdout']['data']).decode()
        assert stdout.index('ACTUAL_BODY_READY') < stdout.index('ACTUAL_BODY_ABORT') < stdout.index('ACTUAL_AFTER_EACH'), stdout
        rows = report.get('softAssertions', [])
        if soft:
            assert stdout.index('ACTUAL_AFTER_EACH') < stdout.index('ACTUAL_SOFT_ACK_CONTINUED'), stdout
            assert len(rows) == 1 and rows[0]['callbackKind'] == 'afterEach' and rows[0]['cleanup'] and rows[0]['acceptedMismatch'] and rows[0]['receipt'] == 'mismatch' and rows[0]['pid'] == workers[0]['pid'], rows
        else:
            assert not rows, rows
        results.append(dict(name=name, exitCode=actual.returncode, workerPID=workers[0]['pid'], globalTimeoutMs=1000, actualSoftRows=len(rows)))
finally:
    after = {str(p): digest(p) for p in inputs}
    (args.directory / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (args.directory / 'summary.json').write_text(json.dumps(dict(scopes=results, inputsUnchanged=before == after), indent=2) + '\n')
    assert before == after, 'Actual cleanup proof inputs changed'
print('PASS 2 actual original global-stop cleanup scopes (controlled soft producer, no DOM matcher claim)')
