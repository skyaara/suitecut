# SPDX-License-Identifier: MIT
"""Actual native collection without callback admission, plus list/run ID parity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default=shutil.which('node'))
parser.add_argument('--source-root', type=Path)
args = parser.parse_args()
assert args.node
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='run-', dir=args.directory.resolve()))
(root/'control-script.py').write_bytes(Path(__file__).read_bytes())
sdk = args.sdk_directory.resolve()
executable = args.executable.resolve()
inputs = [executable, Path(args.node).resolve(), Path(__file__).resolve(), *sorted(p for p in sdk.rglob('*') if p.is_file())]
if args.source_root:
    inputs.extend(p for p in (args.source_root/'native/runner').rglob('*') if p.is_file())
    inputs.extend(args.source_root/name for name in ['src/native-runner.ts','src/native-runner-cli.ts','src/native-runner-host.ts'])
inputs = sorted(set(inputs))
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p): digest(p) for p in inputs}
(root/'inputs-before.json').write_text(json.dumps(before, indent=2)+'\n')
groups = []


def record(value):
    groups.append(value)
    partial = [{'name':row['name'],'baseChecksPassed':True,'output':row.get('output')} for row in groups]
    (root/'partial-report.json').write_text(json.dumps({'scope':'Base invocation checks only; subsequent metadata assertions and final input checks may remain pending','baseControls':partial},indent=2)+'\n')


def execute(name, declaration, selected, options=(), expected=0, frontend='native', error=None, parity=False, prelude=''):
    folder = root/name
    folder.mkdir()
    suite = folder/'suite.mjs'
    events = folder/'events.jsonl'
    suite.write_text('import fs from "node:fs";import process from "node:process";'
        'import {defineNativeSuite} from '+json.dumps((sdk/'native-suite.js').as_uri())+';\n'
        'const mark=(kind,info)=>fs.appendFileSync('+json.dumps(str(events))+
        ',JSON.stringify({kind,id:info?.testId??null,pid:process.pid})+"\\n");mark("import");\n'
        'const callback=(_,info)=>mark("callback",info);'
        'const test=(title,extra={})=>({title,run:callback,...extra});'
        'const hooks={beforeAll:[{run:callback}],beforeEach:[{run:callback}],afterEach:[{run:callback}],afterAll:[{run:callback}]};'
        'const fixtures={automatic:{automatic:true,setup:async({},use,info)=>{mark("setup",info);await use(42);mark("teardown",info)}}};\n'
        +prelude+'\nexport default defineNativeSuite('+declaration+');\n')
    suite_before = digest(suite)
    output = folder/'list.json'
    if frontend == 'native':
        command = [str(executable), '--node', args.node, '--host-script', str(sdk/'native-runner-host.js')]
    elif frontend == 'cli':
        command = [args.node, str(sdk/'native-runner-cli.js'), '--executable', str(executable)]
    else:
        launcher = folder/'launch.mjs'
        launcher.write_text('import process from "node:process";import {runNativeSuite} from '+
            json.dumps((sdk/'native-runner.js').as_uri())+';const result=await runNativeSuite('+
            json.dumps({'executablePath':str(executable), 'suitePath':str(suite), 'reportPath':str(output),
                        'list':True, 'testIds':['5','2'], 'shard':{'current':2,'total':2}})+
            ');if(result.signal)throw Error(result.signal);process.exitCode=result.exitCode;')
        command = [args.node, str(launcher)]
    if frontend != 'api':
        command += ['--suite', str(suite), '--report', str(output), '--list', *options]
    (folder/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    actual = subprocess.run(command, capture_output=True, timeout=25)
    (folder/'stdout').write_bytes(actual.stdout)
    (folder/'stderr').write_bytes(actual.stderr)
    assert actual.returncode == expected, (name, actual.returncode, actual.stderr)
    assert digest(suite) == suite_before
    observed = [json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
    assert all(row['kind'] == 'import' for row in observed), (name, observed)
    if expected == 2:
        assert not output.exists()
        assert error.encode() in actual.stderr, (name, actual.stderr)
        assert not observed
        record({'name':name, 'passed':True, 'output':None})
        return None, folder
    assert not actual.stderr, (name, actual.stderr)
    data = json.loads(output.read_bytes())
    assert data['kind'] == 'suitecut-native-test-list' and data['version'] == 1
    assert 'attempts' not in data and 'workers' not in data and 'summary' not in data
    assert data['selectedTestIds'] == selected, (name, data)
    assert data['selectionResolved'] is (expected == 0)
    assert len(data['collectionWorkers']) == 1
    worker = data['collectionWorkers'][0]
    assert worker['pid'] == observed[0]['pid'] and worker['exitStatusKnown'] is True
    if expected == 0:
        assert worker['exitCode'] == 0 and worker['signal'] is None and not data['errors']
    else:
        assert any(error in value for value in data['errors']), (name, data['errors'])
        assert all(row['selected'] is None for row in data['tests'])
    try:
        os.kill(worker['pid'], 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError(('actual collection worker survives publication', worker))
    assert bytes.fromhex(worker['stdoutUTF8Hex']) == b''
    assert bytes.fromhex(worker['stderrUTF8Hex']) == b''
    st = suite.stat()
    assert data['file'] == {'id':'1', 'path':str(suite), 'device':str(st.st_dev), 'inode':str(st.st_ino)}
    protocol = bytes.fromhex(data['collectionProtocolUTF8Hex']).decode()
    assert protocol.endswith('LOADED\n') if data['tests'] else True
    if expected == 0:
        assert [row['testId'] for row in data['tests'] if row['selected']] == selected
    if parity:
        run = folder/'run.json'
        actual = subprocess.run([str(executable), '--node', args.node, '--host-script', str(sdk/'native-runner-host.js'),
            '--suite', str(suite), '--report', str(run), *options], capture_output=True, timeout=25)
        (folder/'run-stdout').write_bytes(actual.stdout)
        (folder/'run-stderr').write_bytes(actual.stderr)
        assert actual.returncode == 0, (name, actual.returncode, actual.stderr)
        report = json.loads(run.read_bytes())
        actual_ids = {row['testId'] for row in report['attempts']} | {row['testId'] for row in report.get('skipped', [])}
        assert actual_ids == set(selected), (name, actual_ids, selected)
        titles = {row['testId']:row['title'] for row in data['tests']}
        assert all(row['title'] == titles[row['testId']] for row in report['attempts'])
    record({'name':name, 'passed':True, 'output':str(output)})
    return data, folder


flat = '{fixtures,hooks,tests:[test("one"),test("two"),test("three"),test("four"),test("five")]}'
execute('all-real-declarations', flat, ['1','2','3','4','5'], parity=True)
execute('filter-original-ids', flat, ['2','5'], ['--test-id','5','--test-id','2'], parity=True)
execute('filter-before-shard-cli', flat, ['5'], ['--test-id','5','--test-id','2','--shard','2/2'], frontend='cli', parity=True)
execute('api-list-transport', flat, ['5'], frontend='api')
execute('empty-shard-retains-all-declarations', flat, [], ['--shard','6/7'], parity=True)
execute('workers-do-not-admit-extra-collection-hosts', flat, ['1','2','3','4','5'], ['--workers','8'])
child_pid_file = root/'actual-import-child.pid'
child_listing, _ = execute('actual-top-level-descendant-retired', flat, None, expected=1, error='Actual native descendant batch', prelude=
    'import {spawn} from "node:child_process";const child=spawn(process.execPath,["-e","setInterval(()=>{},1000)"],{stdio:"ignore"});'
    'fs.writeFileSync('+json.dumps(str(child_pid_file))+',String(child.pid));child.unref();')
child_pid = int(child_pid_file.read_text())
assert any(f'PID {child_pid} signal 9 after native cleanup SIGKILL' in error for error in child_listing['errors'])
assert len(child_listing['tests']) == 5 and 'attempts' not in child_listing
try:
    os.kill(child_pid, 0)
except ProcessLookupError:
    pass
else:
    raise AssertionError(('actual top-level module child survives listing', child_pid))

execute('unknown-test-id', flat, None, ['--test-id','99'], expected=1, error='selected test identity')
execute('duplicate-test-id', flat, None, ['--test-id','2','--test-id','2'], expected=1, error='selected test identity')
execute('forbid-focus', '{fixtures,hooks,tests:[test("focused",{only:true})]}', None, ['--forbid-only'], expected=1, error='Focused')
execute('focused-subtree', '{fixtures,hooks,entries:[test("excluded"),{title:"focused suite",only:true,hooks,entries:[test("included"),test("included two")]}]}', ['2','3'], parity=True)
data, _ = execute('inherited-static-skip-and-utf16', '{fixtures,hooks,entries:[test("kept"),{title:"skip\\ud800",skip:"reason\\udfff",hooks,entries:[test("excluded?\\ud800",{retries:2,timeout:47,expectedFailure:true})]}]}', ['1','2'], parity=True)
assert data['tests'][1]['title'].endswith('excluded?\ud800') and data['tests'][1]['skipReason'] == 'reason\udfff'
assert data['tests'][1]['retries'] == 2 and data['tests'][1]['timeoutMs'] == 47 and data['tests'][1]['expectedFailure']
projects = '''{fixtures,hooks,projects:[
 {name:'body',dependencies:['setup'],metadata:{customUnknown:{deep:['π','\\ud800']},zero:0},tests:[test('body one'),test('body two')]},
 {name:'setup',default:false,teardown:'cleanup',metadata:{kind:'support'},tests:[test('setup')]},
 {name:'cleanup',metadata:{kind:'teardown'},tests:[test('cleanup')]},
 {name:'excluded',default:false,metadata:{unknownFuture:{preserve:true}},tests:[test('excluded')]},
 {name:'empty',default:false,metadata:{empty:true},tests:[]}] }'''
data, _ = execute('project-support-and-opaque-metadata', projects, ['1','2','3','4'], parity=True)
assert [p['selectionRole'] for p in data['projects']] == ['primary','dependency','teardown','notSelected','notSelected']
assert data['projects'][0]['metadata']['customUnknown']['deep'] == ['π','\ud800']
assert data['projects'][3]['metadata'] == {'unknownFuture':{'preserve':True}}
assert len(data['tests']) == 5 and data['tests'][4]['selected'] is False
execute('project-primary-shard-support-whole', projects, ['2','3','4'], ['--project','BODY','--shard','2/2'], parity=True)
execute('project-nodeps-original-id', projects, ['2'], ['--project','body','--test-id','2','--no-deps'], parity=True)
execute('wildcard-excluded-project-becomes-primary', projects, ['5'], ['--project','EXCLU*'], parity=True)
data, _ = execute('explicit-empty-project', projects, [], ['--project','empty','--pass-with-no-tests'])
assert data['projects'][4]['selectionRole'] == 'primary'
execute('unknown-project', projects, None, ['--project','missing'], expected=1, error='selector not found')
execute('selector-without-projects', flat, None, ['--project','missing'], expected=1, error='requires declared projects')
for option in ['--junit','--html','--output-directory']:
    execute('execution-option-rejected-'+option[2:], flat, None, [option,str(root/(option[2:]+'.out'))], expected=2, error='cannot use execution')
execute('failure-policy-rejected', flat, None, ['--max-failures','0'], expected=2, error='cannot use execution')
execute('duplicate-list-flag-rejected', flat, None, ['--list'], expected=2, error='Duplicate')

# Real import failure is a collection error; it creates no author work or attempt.
execute('actual-import-failure', flat, None, expected=1, error='suite module failed', prelude='throw Error("actual collection import failure");')

# A genuine control host emits an unsolicited execution packet only after CLOSE.
# Collection must fail explicitly without ever dispatching callback-owner code.
folder = root/'actual-unsolicited-close-packet'
folder.mkdir()
suite = folder/'suite.mjs'
suite.write_text('export default {tests:[]};\n')
host = folder/'control-host.mjs'
host.write_text('import fs from "node:fs";import {Buffer} from "node:buffer";'
    'fs.writeSync(3,"LOADED\\n");const buffer=Buffer.alloc(128);const size=fs.readSync(3,buffer,0,128,null);'
    'if(buffer.subarray(0,size).toString()!=="CLOSE\\n")throw Error("expected actual native CLOSE");'
    'fs.writeSync(3,"READY 1 1 1\\n");setInterval(()=>{},1000);\n')
actual = subprocess.run([str(executable),'--node',args.node,'--host-script',str(host),
    '--suite',str(suite),'--report',str(folder/'list.json'),'--list'],capture_output=True,timeout=20)
(folder/'stdout').write_bytes(actual.stdout)
(folder/'stderr').write_bytes(actual.stderr)
assert actual.returncode == 1 and not actual.stderr, actual.stderr
value = json.loads((folder/'list.json').read_bytes())
assert value['selectionResolved'] is False and value['selectedTestIds'] is None
assert any('Unexpected native collection packet after declarations' in error for error in value['errors'])
assert 'attempts' not in value and len(value['collectionWorkers']) == 1
worker = value['collectionWorkers'][0]
assert worker['exitStatusKnown'] is True and worker['exitCode'] is None and worker['signal'] == 9
try:
    os.kill(worker['pid'], 0)
except ProcessLookupError:
    pass
else:
    raise AssertionError('actual malformed collection host survives publication')
record({'name':'actual-unsolicited-close-packet','passed':True})

# Real module top-level writes to its input are fenced before output publication.
folder = root/'actual-input-version-change'
folder.mkdir()
suite = folder/'suite.mjs'
events = folder/'import-pid'
suite.write_text('import fs from "node:fs";import process from "node:process";'
    'fs.writeFileSync('+json.dumps(str(events))+',String(process.pid));'
    'fs.appendFileSync(new URL(import.meta.url),"\\n// actual import mutation\\n");export default {tests:[]};\n')
actual = subprocess.run([str(executable),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),
    '--suite',str(suite),'--report',str(folder/'list.json'),'--list'],capture_output=True,timeout=20)
(folder/'stdout').write_bytes(actual.stdout)
(folder/'stderr').write_bytes(actual.stderr)
assert actual.returncode == 2 and b'identity/version changed' in actual.stderr, actual.stderr
assert not (folder/'list.json').exists()
try:
    os.kill(int(events.read_text()), 0)
except ProcessLookupError:
    pass
else:
    raise AssertionError('actual input-changing collection host survives error')
record({'name':'actual-input-version-change','passed':True})

# Protect source files (including actual hardlink aliases) from atomic output rename.
folder = root/'source-alias-output'
folder.mkdir()
suite = folder/'suite.mjs'
suite.write_text('export default {tests:[]};\n')
original = suite.read_bytes()
for name, destination in [('same-file',suite),('actual-hardlink',folder/'hardlink.json')]:
    if destination != suite:
        os.link(suite,destination)
    actual = subprocess.run([str(executable),'--node',args.node,'--host-script',str(sdk/'native-runner-host.js'),
        '--suite',str(suite),'--report',str(destination),'--list'],capture_output=True,timeout=15)
    assert actual.returncode == 2 and b'aliases a collection input' in actual.stderr
    assert suite.read_bytes() == original and destination.read_bytes() == original
    record({'name':'output-'+name,'passed':True})

after = {str(p):digest(p) for p in inputs}
(root/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert before == after
(root/'report.json').write_text(json.dumps({'passed':len(groups),'groups':groups,'inputsUnchanged':True},indent=2)+'\n')
print(json.dumps({'passed':len(groups),'directory':str(root)}))
