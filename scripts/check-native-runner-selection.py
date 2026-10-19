# SPDX-License-Identifier: MIT
"""Launch actual native admission, callback host, hooks and retry workers."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--host-script', type=Path, required=True)
parser.add_argument('--suite-api', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default=shutil.which('node'))
args = parser.parse_args()
root = args.directory.resolve()
root.mkdir(parents=True, exist_ok=True)
groups = []


def execute(name, extra, selected, expected=0, frontend='native'):
    folder = root / name
    folder.mkdir(exist_ok=True)
    events = folder / 'events.jsonl'
    # CTest may repeat this proof, but never reuse a prior attempt's events.
    events.unlink(missing_ok=True)
    suite = folder / 'suite.mjs'
    suite.write_text(
        'import fs from "node:fs";import process from "node:process";'
        'import {defineNativeSuite} from ' + json.dumps(args.suite_api.resolve().as_uri()) + ';\n'
        'const mark=(kind,info)=>fs.appendFileSync(' + json.dumps(str(events)) +
        ',JSON.stringify({kind,id:info.testId??null,pid:process.pid})+"\\n");\n'
        'export default defineNativeSuite({fixtures:{value:{automatic:true,setup:async({},use,info)=>{'
        'mark("setup",info);await use(42);mark("teardown",info)}}},'
        'hooks:{beforeAll:[{run:async(_,info)=>mark("beforeAll",info)}],'
        'beforeEach:[{run:async(_,info)=>mark("beforeEach",info)}],'
        'afterEach:[{run:async(_,info)=>mark("afterEach",info)}],'
        'afterAll:[{run:async(_,info)=>mark("afterAll",info)}]},'
        'tests:Array.from({length:5},(_,index)=>({title:"actual declaration "+(index+1),fixtures:["value"],retries:1,'
        'run:async({value},info)=>{if(value!==42)throw Error("actual fixture missing");'
        'mark("body",info);if(info.testId==="3"&&info.retry===0)throw Error("actual first retry");}}))});\n')
    report = folder / 'result.json'
    if frontend == 'native':
        command = [str(args.executable), '--node', args.node, '--host-script', str(args.host_script)]
    elif frontend == 'cli':
        command = [args.node, str(args.host_script.parent / 'native-runner-cli.js'),
                   '--executable', str(args.executable)]
    else:
        launcher = folder / 'launch.mjs'
        launcher.write_text('import process from "node:process";import {runNativeSuite} from ' +
            json.dumps((args.host_script.parent / 'native-runner.js').resolve().as_uri()) +
            ';const actual=await runNativeSuite(' + json.dumps({
                'executablePath': str(args.executable), 'suitePath': str(suite),
                'reportPath': str(report), 'testIds': ['5','2'],
                'shard': {'current': 2, 'total': 2}}) +
            ');if(actual.signal!==null)throw Error(actual.signal);process.exitCode=actual.exitCode;')
        command = [args.node, str(launcher)]
    if frontend != 'api':
        command += ['--suite', str(suite), '--report', str(report)] + extra
    actual = subprocess.run(command, capture_output=True, timeout=15)
    (folder / 'stdout').write_bytes(actual.stdout)
    (folder / 'stderr').write_bytes(actual.stderr)
    assert actual.returncode == expected, (name, actual.returncode, actual.stderr)
    assert actual.stderr == b'', (name, actual.stderr)
    data = json.loads(report.read_bytes())
    assert [attempt['testId'] for attempt in data['attempts']] == selected, (name, data)
    assert data.get('unrun', []) == [], (name, data)
    observed = [json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
    assert [entry['id'] for entry in observed if entry['kind'] == 'body'] == selected, (name, observed)
    for phase in ['beforeEach', 'afterEach']:
        assert [entry['id'] for entry in observed if entry['kind'] == phase] == selected
    if not selected:
        assert observed == [], (name, observed)
    for worker in data['workers']:
        assert worker['exitStatusKnown'] is True, (name, worker)
        if expected == 0:
            assert worker['exitCode'] == 0 and worker['signal'] is None, (name, worker)
        else:
            assert worker['exitCode'] is None and worker['signal'] == 9, (name, worker)
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(('Actual worker survives report publication', worker))
    if '3' in selected:
        attempts = [attempt for attempt in data['attempts'] if attempt['testId'] == '3']
        assert [a['status'] for a in attempts] == ['failed', 'passed']
        assert attempts[0]['pid'] != attempts[1]['pid']
    if expected:
        assert any('selected test identity' in error for error in data['globalErrors'])
    else:
        assert data['globalErrors'] == []
    groups.append({'name': name, 'passed': True, 'report': str(report)})


execute('shard-first', ['--shard','1/3'], ['1','2'])
execute('shard-middle-real-retry', ['--shard','2/3'], ['3','3','4'])
execute('shard-last', ['--shard','3/3'], ['5'])
execute('empty-shard-no-fixtures-or-hooks', ['--shard','6/7'], [])
execute('exact-filter-original-order', ['--test-id','5','--test-id','2'], ['2','5'])
execute('filter-before-shard-cli', ['--test-id','5','--test-id','2','--shard','2/2'], ['5'], frontend='cli')
execute('filter-before-shard-api', [], ['5'], frontend='api')
execute('unknown-id-no-admission', ['--test-id','99'], [], expected=1)
execute('duplicate-id-no-admission', ['--test-id','2','--test-id','2'], [], expected=1)
(root / 'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups},indent=2)+'\n')
print(json.dumps({'passed':len(groups),'directory':str(root)}))
