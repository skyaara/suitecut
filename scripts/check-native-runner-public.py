# SPDX-License-Identifier: MIT
"""Actual native CLI/host callbacks; Python only launches and checks artifacts."""
import json
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, default=Path(os.environ.get('SUITECUT_RUNNER_PROOF_EXECUTABLE', ROOT / 'suitecut-runner')))
parser.add_argument('--host-script', type=Path, default=ROOT / 'dist/native-runner-host.js')
parser.add_argument('--suite-api', type=Path, default=ROOT / 'dist/native-suite.js')
parser.add_argument('--directory', type=Path, default=Path(os.environ.get('SUITECUT_RUNNER_PROOF_DIRECTORY', ROOT / 'evidence')))
parser.add_argument('--node', default=shutil.which('node'))
options = parser.parse_args()
RUN = options.directory.resolve()
RUN.mkdir(parents=True, exist_ok=True)
NODE = options.node
assert NODE
HOST = options.host_script.resolve()
CLI = options.executable.resolve()
API = options.suite_api.resolve().as_uri()
results = []


def execute(name, source, expected_code=0, via_cli=False, orchestration_timeout=20):
    folder = RUN / name
    folder.mkdir(exist_ok=True)
    suite = folder / 'suite.mjs'
    suite.write_text("import assert from 'node:assert/strict';\nimport fs from 'node:fs';\nimport process from 'node:process';\nimport {Buffer} from 'node:buffer';\nimport {defineNativeSuite} from " + json.dumps(API) + ';\n' + source)
    report = folder / 'result.json'
    junit = folder / 'result.xml'
    start = time.monotonic()
    command = ([NODE, str(HOST.parent / 'native-runner-cli.js'), '--executable', str(CLI)] if via_cli else [str(CLI), '--node', NODE, '--host-script', str(HOST)])
    process = subprocess.run(command + ['--suite', str(suite), '--report', str(report), '--junit', str(junit)], capture_output=True, timeout=orchestration_timeout)
    elapsed = (time.monotonic() - start) * 1000
    (folder / 'cli.stdout').write_bytes(process.stdout)
    (folder / 'cli.stderr').write_bytes(process.stderr)
    assert process.returncode == expected_code, (name, process.returncode, process.stderr)
    actual = json.loads(report.read_bytes())
    tree = ET.parse(junit)
    assert tree.getroot().tag == 'testsuites'
    for worker in actual['workers']:
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('Actual worker still exists after CLI report publication')
    results.append({'name': name, 'passed': True, 'elapsedMs': elapsed, 'report': str(report), 'junit': str(junit)})
    return actual, elapsed


actual, elapsed = execute('real-fixtures-retry-streams', '''
const mark=(name)=>process.stdout.write(name+'\\n');
export default defineNativeSuite({fixtures:{
 worker:{scope:'worker',automatic:true,setup:async({},use,info)=>{assert.equal(info.scope,'worker');assert.equal('testId' in info,false);mark('worker+');await use({pid:process.pid});mark('worker-')}},
 item:{dependencies:['worker'],setup:async({worker},use,info)=>{assert.equal(info.scope,'test');assert.equal(typeof info.testId,'string');mark('item+');await use(worker);mark('item-')}}
},tests:[
 {title:'actual fixture and binary streams',fixtures:['item'],run:async({item},info)=>{assert.equal(item.pid,process.pid);assert.equal(info.retry,0);process.stdout.write(Buffer.from([0,255,82,69,65,68,89,32,57,57,57,10]));process.stderr.write(Buffer.from([0,254]));}},
 {title:'real first retry failure',fixtures:['item'],retries:1,run:async({item},info)=>{assert.equal(item.pid,process.pid);if(info.retry===0)throw new Error('actual first-attempt error');assert.equal(info.retry,1)}},
 {title:'expected native failure',expectedFailure:true,run:async()=>{throw new Error('actual expected error')}}]});
''')
assert [a['status'] for a in actual['attempts']] == ['passed', 'failed', 'passed', 'failed'], actual
assert len(actual['workers']) == 2
assert actual['attempts'][0]['pid'] == actual['attempts'][1]['pid']
assert actual['attempts'][1]['pid'] != actual['attempts'][2]['pid']
assert 'actual first-attempt error' in actual['attempts'][1]['error']
assert actual['attempts'][-1]['expected'] is True
assert '00ff5245414459203939390a' in actual['workers'][0]['stdout']['data']
assert actual['workers'][0]['stderr']['data'] == '00fe'

actual, elapsed = execute('never-body-real-exit', '''
export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async({},use)=>{process.stdout.write('resource+\\n');await use(1);process.stdout.write('resource-\\n')}}},tests:[
 {title:'real pending callback',timeout:40,run:async()=>{await new Promise(()=>{})}},
 {title:'next real worker',run:async()=>{assert.equal(process.pid>0,true)}}]});
''', 1)
assert elapsed >= 1000, elapsed
assert [a['status'] for a in actual['attempts']] == ['timedOut', 'passed'], actual
assert actual['attempts'][0]['pid'] != actual['attempts'][1]['pid']
assert actual['workers'][0]['signal'] == 9
assert b'resource-' in bytes.fromhex(actual['workers'][0]['stdout']['data'])

# Establish body readiness before shortening the existing native original-origin budget.
# A past deadline expires immediately; no preparation time is removed or clock reset.
actual, elapsed = execute('deadline-abort-signal', '''
export default defineNativeSuite({tests:[{title:'actual AbortSignal',timeout:30000,run:async({},info)=>{
 const aborted=new Promise(resolve=>info.signal.addEventListener('abort',()=>{process.stdout.write('ACTUAL_ABORT\\n');resolve()},{once:true}));
 process.stdout.write('ACTUAL_BODY_READY\\n');
 const budget=25;
 await info.setTimeout(budget);
 assert.equal(info.timeout,budget);
 process.stdout.write('ACTUAL_TIMEOUT_ACK '+JSON.stringify({budget,admittedElapsed:info.duration})+'\\n');
 await aborted;throw info.signal.reason;
}}]});
''', 1)
assert actual['attempts'][0]['status'] == 'timedOut'
abort_stdout = bytes.fromhex(actual['workers'][0]['stdout']['data'])
assert b'ACTUAL_ABORT' in abort_stdout
assert abort_stdout.index(b'ACTUAL_BODY_READY') < abort_stdout.index(b'ACTUAL_ABORT'), abort_stdout
amended = [json.loads(line.removeprefix('ACTUAL_TIMEOUT_ACK ')) for line in abort_stdout.decode().splitlines() if line.startswith('ACTUAL_TIMEOUT_ACK ')]
assert len(amended) == 1 and amended[0]['budget'] == 25, amended
assert actual['attempts'][0]['durationMs'] >= amended[0]['budget'], (actual, amended)
assert actual['workers'][0]['signal'] is None
assert actual['workers'][0]['exitCode'] == 0

actual, elapsed = execute('late-fixture-readiness', '''
export default defineNativeSuite({fixtures:{late:{setup:async({},use)=>{await new Promise(resolve=>setTimeout(resolve,80));await use(1)}}},tests:[{title:'must not execute after deadline',timeout:20,fixtures:['late'],run:async()=>{process.stdout.write('FORBIDDEN_BODY')}}]});
''', 1)
assert actual['attempts'][0]['status'] == 'timedOut'
assert b'FORBIDDEN_BODY' not in bytes.fromhex(actual['workers'][0]['stdout']['data'])

actual, elapsed = execute('worker-teardown-physical', '''
export default defineNativeSuite({fixtures:{held:{scope:'worker',automatic:true,setup:async({},use)=>{await use(1);process.stdout.write('ACTUAL_TEARDOWN');await new Promise(()=>{})}}},tests:[{title:'body passes, teardown cannot settle',run:async()=>{}}]});
''', 1, orchestration_timeout=45)
assert actual['attempts'][0]['status'] == 'passed'
assert actual['globalErrors']
# The ordinary worker fixture inherits the real default30000 slot. Its raw
# callback deliberately cannot settle, so require actual OS retirement after
# that slot rather than shortening the author budget to fit an old wrapper.
assert elapsed >= 30000
assert len(actual['workers']) == 1 and actual['workers'][0]['exitStatusKnown']
assert actual['workers'][0]['exitCode'] is None and actual['workers'][0]['signal'] == 9
assert b'ACTUAL_TEARDOWN' in bytes.fromhex(actual['workers'][0]['stdout']['data'])

actual, elapsed = execute('native-dag-precondition', '''
export default defineNativeSuite({fixtures:{outer:{scope:'worker',dependencies:['inner'],setup:()=>{process.stdout.write('FORBIDDEN_FACTORY')}},inner:{setup:async({},use)=>use(1)}},tests:[{title:'invalid native dependency',fixtures:['outer'],run:()=>{}}]});
''', 1)
assert not actual['attempts']
assert actual['globalErrors']
assert b'FORBIDDEN_FACTORY' not in bytes.fromhex(actual['workers'][0]['stdout']['data'])

actual, elapsed = execute('exact-utf16-and-timeout-zero', '''
export default defineNativeSuite({tests:[{title:'exact \\ud800 and \\u0000 \\udfff',timeout:0,run:async()=>{await new Promise(resolve=>setTimeout(resolve,35))}}]});
''')
assert actual['attempts'][0]['title'] == 'exact \ud800 and \x00 \udfff'

actual, elapsed = execute('fixture-actual-error', '''
export default defineNativeSuite({fixtures:{broken:{setup:async()=>{throw new Error('actual fixture setup error')}}},tests:[{title:'fixture genuinely rejects',fixtures:['broken'],run:async()=>{process.stdout.write('FORBIDDEN_BODY')}}]});
''', 1)
assert actual['attempts'][0]['status'] == 'failed'
assert b'FORBIDDEN_BODY' not in bytes.fromhex(actual['workers'][0]['stdout']['data'])

actual, elapsed = execute('native-stream-cap-physical-close', '''
export default defineNativeSuite({tests:[{title:'actual oversized stdout',run:async()=>{process.stdout.write(Buffer.alloc(1100000,65));await new Promise(()=>{})}}]});
''', 1)
assert actual['globalErrors']
assert actual['attempts'][0]['status'] == 'interrupted'
assert actual['workers'][0]['signal'] == 9
assert actual['workers'][0]['stdout']['byteLength'] <= 1048576

actual, elapsed = execute('retry-metadata-native-fence', '''
const counter=new URL('./count.txt',import.meta.url);const n=fs.existsSync(counter)?Number(fs.readFileSync(counter,'utf8')):0;fs.writeFileSync(counter,String(n+1));
export default defineNativeSuite({tests:[{title:'actual metadata '+n,retries:1,run:async()=>{throw new Error('genuine first callback fails')}}]});
''', 1)
assert len(actual['attempts']) == 1
assert len(actual['workers']) == 2
assert any('metadata changed' in error for error in actual['globalErrors'])

actual, elapsed = execute('actual-control-eof', '''
export default defineNativeSuite({tests:[{title:'real control socket disappears',timeout:0,run:async()=>{setInterval(()=>{},1000);fs.closeSync(3);await new Promise(()=>{})}}]});
''', 1)
assert elapsed < 3000
assert actual['globalErrors']
assert actual['attempts'][0]['status'] == 'interrupted'

actual, elapsed = execute('actual-public-node-cli', '''
export default defineNativeSuite({tests:[{title:'actual thin public launcher',run:async({},info)=>{assert.equal(info.workerIndex,1);assert.equal(info.retry,0);process.stdout.write('PUBLIC_NATIVE_CALLBACK')}}]});
''', via_cli=True)
assert actual['attempts'][0]['status'] == 'passed'
assert b'PUBLIC_NATIVE_CALLBACK' in bytes.fromhex(actual['workers'][0]['stdout']['data'])

(RUN / 'report.json').write_text(json.dumps({'groups': results, 'passed': len(results), 'claims': 'Authored actual native CLI/process/host contract only; no upstream runner parity'}, indent=2) + '\n')
print(json.dumps({'passed': len(results), 'report': str(RUN / 'report.json')}))
