# SPDX-License-Identifier: MIT
"""Actual native-issued snapshot path plans and author callbacks; no browser/IO matcher."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--sdk-directory', type=Path, required=True)
p.add_argument('--supervisor', type=Path, required=True)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--node', default=shutil.which('node'))
a = p.parse_args()
assert a.node
a.directory.mkdir(parents=True, exist_ok=True)
out = Path(tempfile.mkdtemp(prefix='run-', dir=a.directory.resolve()))
sdk, exe, supervisor = a.sdk_directory.resolve(), a.executable.resolve(), a.supervisor.resolve()
suite = out / 'suite.mjs'
suite.write_text(r'''
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineNativeSuite} from API;
const root=path.dirname(fileURLToPath(import.meta.url));
const events=(event,info)=>console.log(JSON.stringify({event,id:info.testId,project:info.project.name,suffix:info.snapshotSuffix}));
const hooks={beforeEach:[{run:(_,info)=>{
 assert.equal(info.snapshotSuffix,process.platform);info.snapshotSuffix='';assert.equal(info.snapshotSuffix,'');events('before',info);
}}],afterEach:[{run:(_,info)=>{assert.equal(info.snapshotSuffix,'custom');events('after',info)}}]};
export default defineNativeSuite({projects:[{name:'named project',hooks,tests:[{title:'default suffix cell',run:(_,info)=>{
 const directory=path.join(root,'suite.mjs-snapshots');assert.equal(info.snapshotDir,directory);
 assert.equal(info.snapshotPath('tree.png'),path.join(directory,'tree-named-project.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'screenshot'}),info.snapshotPath('tree.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'aria'}),path.join(directory,'tree.png'));
 info.snapshotSuffix='custom';assert.equal(info.snapshotPath('tree.png'),path.join(directory,'tree-named-project-custom.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'screenshot'}),info.snapshotPath('tree.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'aria'}),path.join(directory,'tree.png'));
 for(const invalid of ['.','..','a/b','a\\b','\0','\ud800','x'.repeat(1025),1,null]){assert.throws(()=>Reflect.set(info,'snapshotSuffix',invalid));assert.equal(info.snapshotSuffix,'custom');}
 assert(!fs.existsSync(directory));events('body',info);
}}]},{name:'explicit',hooks,snapshotPathTemplate:'{snapshotDir}/explicit/{platform}/{-snapshotSuffix}/{arg}{ext}',
expect:{toMatchAriaSnapshot:{pathTemplate:'{snapshotDir}/aria/{arg}{ext}'}},tests:[{title:'explicit suffix opcode',run:(_,info)=>{
 const base=path.join(root,'explicit',process.platform);
 assert.equal(info.snapshotPath('tree.png'),path.join(base,'tree.png'));
 info.snapshotSuffix='custom';assert.equal(info.snapshotPath('tree.png'),path.join(base,'-custom','tree.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'screenshot'}),info.snapshotPath('tree.png'));
 assert.equal(info.snapshotPath('tree.png',{kind:'aria'}),path.join(root,'aria/tree.png'));
 events('body',info);
}}]}]});
'''.replace('API', json.dumps((sdk / 'native-suite.js').as_uri())))
inputs = [exe, supervisor, Path(a.node).resolve(), Path(__file__).resolve(), suite,
          *sorted(file for file in sdk.rglob('*') if file.is_file())]
digest = lambda file: hashlib.sha256(file.read_bytes()).hexdigest()
before = {str(file): digest(file) for file in inputs}
(out / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
report = out / 'report.json'
command = [str(supervisor), '--report', str(out / 'supervisor.json'), '--timeout-ms', '60000', '--',
           str(exe), '--node', a.node, '--host-script', str(sdk / 'native-runner-host.js'),
           '--suite', str(suite), '--report', str(report), '--global-timeout', '30000']
(out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
def decoded(value):
    assert isinstance(value, dict) and value['encoding'] == 'hex', value
    data = bytes.fromhex(value['data'])
    assert len(data) == value['byteLength'], value
    return data

failure = None
try:
    syntax = subprocess.run([a.node, '--check', str(suite)], capture_output=True, timeout=10)
    (out / 'syntax.stderr').write_bytes(syntax.stderr)
    assert syntax.returncode == 0, syntax.stderr
    result = subprocess.run(command, capture_output=True, timeout=70)
    (out / 'stdout').write_bytes(result.stdout)
    (out / 'stderr').write_bytes(result.stderr)
    (out / 'exit.json').write_text(json.dumps({'exit': result.returncode}) + '\n')
    assert result.returncode == 0 and result.stderr == b'', (result.returncode, result.stderr)
    data = json.loads(report.read_text())
    assert len(data['attempts']) == 2 and not data['globalErrors'], data
    assert all(row['status'] == 'passed' and row['expected'] for row in data['attempts']), data
    assert not data['unrun'] and not data['skipped'], data
    assert all(worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None for worker in data['workers']), data['workers']
    events = [json.loads(line) for worker in data['workers']
              for line in decoded(worker['stdout']).decode().splitlines()]
    assert sorted(row['event'] for row in events) == ['after', 'after', 'before', 'before', 'body', 'body'], events
    assert {row['id'] for row in events} == {row['testId'] for row in data['attempts']}
    for attempt in data['attempts']:
        actual = [row for row in events if row['id'] == attempt['testId']]
        assert [row['event'] for row in actual] == ['before', 'body', 'after'], actual
        assert [row['suffix'] for row in actual] == ['', 'custom', 'custom'], actual
    assert all(not decoded(worker['stderr']) for worker in data['workers']), data['workers']
    physical = json.loads((out / 'supervisor.json').read_text())
    assert physical['childExit']['code'] == 0 and not physical['childExit']['signaled']
    assert physical['descendantsRetired'] and not physical['cleanupFailed'] and not physical['timeout'] and not physical['error']
    for identity in [*physical['observedIdentities'], {'pid': physical['childPID'], 'start': str(physical['childStart'])},
                     *({'pid': row['pid']} for row in data['workers'])]:
        proc = Path('/proc') / str(identity['pid']) / 'stat'
        if proc.exists():
            fields = proc.read_text().split(')', 1)[1].split()
            assert 'start' in identity and fields[19] != str(identity['start']), ('remaining exact native process including Z', identity)
    assert not (out / 'native-baselines').exists()
except BaseException as error:
    failure = repr(error)
    (out / 'first-failure.txt').write_text(failure + '\n')
    raise
finally:
    after = {file: digest(Path(file)) for file in before}
    (out / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (out / 'index.json').write_text(json.dumps({'scope': 'Actual native Runner callback mutable snapshot suffix; no browser or baseline IO',
        'firstFailure': failure, 'inputsUnchanged': before == after, 'report': str(report)}, indent=2) + '\n')
    assert before == after, 'Exact source/SDK/executable/fixture input changed during proof'
print('Actual native snapshot suffix2 PASS ' + str(out / 'index.json'))
