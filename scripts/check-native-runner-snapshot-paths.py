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
const emit=(event,info)=>process.stdout.write(JSON.stringify({event,testId:info.testId,
 project:info.project.name,snapshotDir:info.snapshotDir})+'\n');
const initial=new Map();
export default defineNativeSuite({testDir:'.',snapshotDir:'native-baselines',projects:[{
 name:'proj 2',snapshotPathTemplate:'{snapshotDir}/{testFilePath}/{testName}{-projectName}{-snapshotSuffix}/{arg}{ext}',
 expect:{toHaveScreenshot:{pathTemplate:'{snapshotDir}/pixels/{arg}{ext}'},
 toMatchAriaSnapshot:{pathTemplate:'{snapshotDir}/semantics/{arg}{ext}'}},
 hooks:{beforeEach:[{run:(_,info)=>{initial.set(info.testId,info.snapshotPath('same.txt'));emit('before',info)}}],
 afterEach:[{run:(_,info)=>{assert.equal(info.snapshotPath('same.txt'),initial.get(info.testId));emit('after',info)}}]},
 entries:[{title:'nested suite',entries:[{title:'path contract',run:(_,info)=>{
  const base=path.join(root,'native-baselines','suite.mjs','nested-suite-path-contract-proj-2-'+process.platform);
  assert.equal(info.snapshotDir,path.join(root,'native-baselines','suite.mjs-snapshots'));
  assert.equal(info.snapshotPath('image.png'),path.join(base,'image.png'));
  assert.equal(info.snapshotPath('a','b','image.jpeg'),path.join(base,'a/b/image.jpeg'));
  assert.equal(info.snapshotPath('header.png',{kind:'screenshot'}),path.join(root,'native-baselines/pixels/header.png'));
  assert.equal(info.snapshotPath('tree.aria.yml',{kind:'aria'}),path.join(root,'native-baselines/semantics/tree.aria.yml'));
  assert.notEqual(info.snapshotDir,info.outputDir);assert(!fs.existsSync(path.join(root,'native-baselines')));
  emit('paths',info);
 }}]}]
},{name:'argument',snapshotPathTemplate:'{ext}{arg}',tests:[{title:'extension first',run:(_,info)=>{
 assert.equal(info.snapshotPath('foo','bar','baz.jpeg'),path.join(root,'.jpegfoo/bar/baz'));
 assert.throws(()=>info.snapshotPath('x',{kind:'unknown'}),/unknown kind "unknown"/);
 for(const name of ['../foreign','/absolute','\0','\ud800'])assert.throws(()=>info.snapshotPath(name));
 emit('argument',info);
}}]},{name:'default',tests:[{title:'unpaired \ud800 untouched',run:(_,info)=>{
 assert(info.title.includes('\ud800'));
 assert.equal(info.snapshotPath('safe.txt'),path.join(root,'native-baselines/suite.mjs-snapshots/safe-default-'+process.platform+'.txt'));
 assert.equal(info.snapshotPath('safe.aria.yml',{kind:'aria'}),path.join(root,'native-baselines/suite.mjs-snapshots/safe.aria.yml'));
 emit('default',info);
}}]},{name:'title-token',snapshotPathTemplate:'{testName}/{arg}{ext}',tests:[{title:'unpaired \ud800 unsupported filename',run:(_,info)=>{
 assert(info.title.includes('\ud800'));
 assert.throws(()=>info.snapshotPath('safe.txt'),/filesystem encoding/);
 assert.equal(info.snapshotDir,path.join(root,'native-baselines/suite.mjs-snapshots'));
 emit('unsupported-title',info);
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
    assert len(data['attempts']) == 4 and not data['globalErrors'], data
    assert all(row['status'] == 'passed' and row['expected'] for row in data['attempts']), data
    assert not data['unrun'] and not data['skipped'], data
    assert all(worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None for worker in data['workers']), data['workers']
    events = [json.loads(line) for worker in data['workers']
              for line in decoded(worker['stdout']).decode().splitlines()]
    assert sorted(row['event'] for row in events) == ['after', 'argument', 'before', 'default', 'paths', 'unsupported-title'], events
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
    (out / 'index.json').write_text(json.dumps({'scope': 'Actual native Runner callback snapshot path plans; no browser or baseline IO',
        'firstFailure': failure, 'inputsUnchanged': before == after, 'report': str(report)}, indent=2) + '\n')
    assert before == after, 'Exact source/SDK/executable/fixture input changed during proof'
print('Actual native snapshot paths4 PASS ' + str(out / 'index.json'))
