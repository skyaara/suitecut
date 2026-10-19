# SPDX-License-Identifier: MIT
"""Signals a real native runner only after a real host start marker exists."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
BASE = ROOT
parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, default=ROOT / 'suitecut-runner')
parser.add_argument('--directory', type=Path, default=ROOT / 'evidence')
parser.add_argument('--host-script',type=Path,default=BASE/'dist/native-runner-host.js')
parser.add_argument('--suite-api',type=Path,default=BASE/'dist/native-suite.js')
parser.add_argument('--node',default=shutil.which('node'))
args = parser.parse_args()
results = []


def run(name, mode, signum, expected_status, cooperative=False):
    folder = (args.directory / name).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    marker, forbidden = folder / 'actual-start', folder / 'forbidden-next'
    marker.unlink(missing_ok=True)
    forbidden.unlink(missing_ok=True)
    suite = folder / 'suite.mjs'
    preface = "import fs from 'node:fs';import process from 'node:process';import {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ';\n'
    mark = 'fs.writeFileSync(' + json.dumps(str(marker)+'.tmp') + ",String(process.pid));fs.renameSync("+json.dumps(str(marker)+'.tmp')+','+json.dumps(str(marker))+');'
    next_body = 'fs.writeFileSync(' + json.dumps(str(forbidden)) + ",'INVALID_NEXT');"
    if mode == 'module':
        source = preface + mark + 'await new Promise(()=>{});'
    elif mode == 'setup':
        source = preface + "export default defineNativeSuite({fixtures:{held:{setup:async()=>{" + mark + "await new Promise(()=>{})}}},tests:[{title:'real fixture interrupted',timeout:0,fixtures:['held'],run:async()=>{" + next_body + "}},{title:'not admitted',run:async()=>{" + next_body + "}}]});"
    else:
        body = ("await new Promise(resolve=>info.signal.addEventListener('abort',()=>{process.stdout.write('ACTUAL_INTERRUPT_ABORT');resolve()}));throw info.signal.reason;" if cooperative else 'await new Promise(()=>{});')
        source = preface + "export default defineNativeSuite({tests:[{title:'real body interrupted',timeout:0,retries:2,run:async({},info)=>{" + mark + body + "}},{title:'not admitted',run:async()=>{" + next_body + "}}]});"
    suite.write_text(source)
    report = folder / 'result.json'
    junit = folder / 'result.xml'
    report.unlink(missing_ok=True)
    junit.unlink(missing_ok=True)
    process = subprocess.Popen([str(args.executable.resolve()), '--node', args.node, '--host-script', str(args.host_script.resolve()), '--suite', str(suite), '--report', str(report), '--junit', str(junit)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    until = time.monotonic() + 5
    while not marker.exists():
        assert time.monotonic() < until and process.poll() is None, 'Genuine host start marker was not observed'
        time.sleep(0.005)
    host_pid = int(marker.read_text())
    started = time.monotonic()
    os.kill(process.pid, signum)
    stdout, stderr = process.communicate(timeout=5)
    elapsed = (time.monotonic() - started) * 1000
    (folder / 'stdout').write_bytes(stdout)
    (folder / 'stderr').write_bytes(stderr)
    assert process.returncode == 128 + signum, (name, process.returncode, stderr)
    value = json.loads(report.read_bytes())
    ET.parse(junit)
    assert not forbidden.exists(), 'A callback was admitted after real interruption'
    assert len(value['workers']) == 1 and value['workers'][0]['pid'] == host_pid
    assert value['globalErrors'] and any(signal.Signals(signum).name in error for error in value['globalErrors'])
    assert len(value['attempts']) == (0 if mode == 'module' else 1)
    if mode != 'module':
        assert value['attempts'][0]['status'] == expected_status
        assert value['attempts'][0]['retry'] == 0
        if cooperative:
            assert b'ACTUAL_INTERRUPT_ABORT' in bytes.fromhex(value['workers'][0]['stdout']['data'])
            assert value['workers'][0]['signal'] is None
        else:
            assert elapsed >= 950, elapsed
            assert value['workers'][0]['signal'] == 9
    try:
        os.kill(host_pid, 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError('Actual host still exists after interruption report publication')
    results.append({'name': name, 'passed': True, 'signal': signum, 'elapsedAfterActualSignalMs': elapsed, 'report': str(report)})


run('cooperative-sigint', 'body', signal.SIGINT, 'interrupted', True)
run('unsettled-body-sigterm', 'body', signal.SIGTERM, 'interrupted')
run('unsettled-setup-sigint', 'setup', signal.SIGINT, 'interrupted')
run('unsettled-module-sigterm', 'module', signal.SIGTERM, None)
args.directory.mkdir(parents=True, exist_ok=True)
(args.directory / 'report.json').write_text(json.dumps({'passed': len(results), 'groups': results}, indent=2) + '\n')
print(json.dumps({'passed': len(results), 'report': str((args.directory / 'report.json').resolve())}))
