# SPDX-License-Identifier: MIT
"""Actual native project selector/test-ID/shard composition and physical receipts.

Authored assertions use the pinned public project behavior as an oracle. No
external runner/JS scheduler or modified upstream driver is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--cli-script', type=Path, required=True)
parser.add_argument('--suite-api', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--source-root', type=Path)
parser.add_argument('--node', default=shutil.which('node'))
args = parser.parse_args()
assert args.node
requested = args.directory.resolve()
requested.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='run-', dir=requested))
inputs = [args.executable.resolve(), args.suite_api.resolve(), Path(__file__).resolve(), *sorted(path for path in args.cli_script.resolve().parent.rglob('*') if path.is_file() and not path.is_symlink())]
if args.source_root:
    inputs.extend(args.source_root/name for name in ['native/runner/project_selection.h','native/runner/project_pattern.h','native/runner/native_runner.cc','native/runner/project_plan.h','native/runner/project_metadata.h','native/runner/project_report.h','native/runner/runner_report.h','native/runner/retry_plan.h','native/runner/CMakeLists.txt','src/native-suite.ts','src/native-runner-host.ts','src/native-runner-cli.ts','src/native-runner.ts','src/native.ts'])
inputs = sorted(set(inputs))
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(path): digest(path) for path in inputs}
(root/'inputs-before.json').write_text(json.dumps(before, indent=2)+'\n')
groups = []


def execute(name, source, code=0, options=()):
    folder = root/name
    folder.mkdir()
    suite = folder/'suite.mjs'
    suite.write_text("import assert from 'node:assert/strict';\nimport process from 'node:process';\nimport {defineNativeSuite} from "+json.dumps(args.suite_api.resolve().as_uri())+";\nconst log=(event,info)=>process.stdout.write(JSON.stringify({event,pid:process.pid,project:info?.project?.name,retry:info?.retry})+'\\n');\n"+source)
    suite_hash = digest(suite)
    command = [args.node, str(args.cli_script.resolve()), '--executable', str(args.executable.resolve()), '--suite', str(suite), '--report', str(folder/'result.json'), *options]
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGTERM)
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate(timeout=5)
    (folder/'native.stdout').write_bytes(stdout)
    (folder/'native.stderr').write_bytes(stderr)
    receipt = {'suiteSha256':suite_hash, 'command': command, 'returncode': process.returncode, 'timedOut': timed_out, 'elapsedMs': (time.monotonic()-start)*1000}
    (folder/'actual-process.json').write_text(json.dumps(receipt, indent=2)+'\n')
    assert digest(suite) == suite_hash, 'Authored module changed during actual execution'
    assert not timed_out and process.returncode == code, (name, receipt, stderr)
    assert stderr == b'', (name, stderr)
    value = json.loads((folder/'result.json').read_bytes())
    workers = {worker['workerIndex']: worker for worker in value['workers']}
    for worker in workers.values():
        assert worker['exitStatusKnown'] is True
        assert worker['stderr']['byteLength'] == 0
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError('Actual project worker still alive at report publication')
    for project in value.get('projects', []):
        assert all(worker in workers for worker in project['workerIndices'])
    for attempt in value['attempts']:
        owner = [project for project in value.get('projects', []) if attempt['testId'] in project['testIds']]
        assert len(owner) == 1
        assert attempt['testId'] in owner[0]['selectedTestIds']
        assert attempt['workerIndex'] in owner[0]['workerIndices']
        assert workers[attempt['workerIndex']]['pid'] == attempt['pid']
    assert all(project['selectionRole'] in ['primary','dependency','teardown','notSelected'] for project in value.get('projects', []))
    assert len({attempt['generation'] for attempt in value['attempts']}) == len(value['attempts'])
    events = [json.loads(line) for worker in workers.values() for line in bytes.fromhex(worker['stdout']['data']).splitlines()]
    groups.append({'name': name, 'processAndOwnershipPassed': True, 'report': str(folder/'result.json'), 'actualProcess': receipt})
    return value, events


try:
    value, events = execute('case-insensitive-star-union-with-literal-punctuation', """
const test=name=>({title:name,run:({},info)=>log(name,info)});
export default defineNativeSuite({projects:[{name:'first',tests:[test('first')]},{name:'fooBar',tests:[test('fooBar')]},{name:'prefix',default:false,tests:[test('prefix')]},{name:'[literal].+$',tests:[test('literal')]}]});
""", options=['--project','FIRST','--project','*bar','--project','pref*x','--project','first','--project','[LITERAL].+$','--project','no-match*'])
    assert [event['event'] for event in events] == ['first','fooBar','prefix','literal']
    assert all(project['selectionRole'] == 'primary' for project in value['projects'])

    for selector,fragment in [('missing*','No native projects matched'),('missing','Native project selector not found')]:
        value, events = execute('unmatched-'+('star' if '*' in selector else 'literal'), """
export default defineNativeSuite({projects:[{name:'available',tests:[{title:'not admitted',run:()=>assert.fail('unmatched selector admitted work')}]}]});
""", code=1, options=['--project',selector])
        assert events == [] and value['attempts'] == []
        assert any(fragment in error and 'available' in error for error in value['globalErrors'])

    source = """
const tests=(project,count)=>Array.from({length:count},(_,index)=>({title:project+(index+1),run:({},info)=>{assert.equal(info.project.name,project);log(project+(index+1),info)}}));
export default defineNativeSuite({projects:[{name:'setup',teardown:'cleanup',tests:tests('setup',2)},{name:'body',dependencies:['setup'],tests:tests('body',4)},{name:'cleanup',tests:tests('cleanup',2)},{name:'manual',default:false,tests:tests('manual',1)}]});
"""
    for shard in [1,2]:
        value, events = execute('primary-shard-'+str(shard)+'-full-support', source, options=['--shard',str(shard)+'/2'])
        expected = ['1','2'] + (['3','4'] if shard == 1 else ['5','6']) + ['7','8']
        assert [attempt['testId'] for attempt in value['attempts']] == expected
        assert [project['selectedTestIds'] for project in value['projects']] == [expected[:2],expected[2:4],expected[4:],[]]
        assert [project['selectionRole'] for project in value['projects']] == ['dependency','primary','teardown','notSelected']
        assert len({attempt['pid'] for attempt in value['attempts']}) == 3
        value, events = execute('test-id-before-primary-shard-'+str(shard), source, options=['--project','BODY','--test-id','3','--test-id','6','--shard',str(shard)+'/2'])
        expected = ['1','2',('3' if shard == 1 else '6'),'7','8']
        assert [attempt['testId'] for attempt in value['attempts']] == expected
        assert value['projects'][1]['selectedTestIds'] == [expected[2]]
        assert len({attempt['pid'] for attempt in value['attempts']}) == 3

    for name,options in [('empty-primary-shard',['--shard','5/5']),('test-id-outside-selected-project',['--project','body','--test-id','9','--pass-with-no-tests'])]:
        value, events = execute(name, source, options=options)
        assert events == [] and value['attempts'] == []
        assert all(project['status'] == 'notSelected' and project['workerIndices'] == [] and project['selectedTestIds'] == [] for project in value['projects'])

    value, events = execute('filtered-away-dependent-does-not-expand', source, options=['--test-id','2'])
    assert [attempt['testId'] for attempt in value['attempts']] == ['2','7','8']
    assert [project['selectionRole'] for project in value['projects']] == ['primary','notSelected','teardown','notSelected']

    value, events = execute('no-deps-excludes-setup-teardown', source, options=['--project','b*','--no-deps','--test-id','4'])
    assert [attempt['testId'] for attempt in value['attempts']] == ['4']
    assert all(project['dependenciesIgnored'] is True for project in value['projects'])
    assert [project['status'] for project in value['projects']] == ['notSelected','passed','notSelected','notSelected']
    assert value['projects'][1]['dependencies'] == ['1'] and value['projects'][0]['teardown'] == '3'

    value, events = execute('no-deps-defaults-exclude-implicit-teardown', source, options=['--no-deps'])
    assert [attempt['testId'] for attempt in value['attempts']] == ['1','2','3','4','5','6']
    assert [project['selectionRole'] for project in value['projects']] == ['primary','primary','notSelected','notSelected']
    assert value['projects'][2]['workerIndices'] == []

    value, events = execute('no-deps-explicit-teardown-name-is-primary', source, options=['--project','CLEANUP','--no-deps'])
    assert [attempt['testId'] for attempt in value['attempts']] == ['7','8']
    assert [project['selectionRole'] for project in value['projects']] == ['notSelected','notSelected','primary','notSelected']

    value, events = execute('no-deps-removes-order-and-failure-gate', """
export default defineNativeSuite({projects:[{name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]},{name:'setup',teardown:'cleanup',tests:[{title:'setup',run:({},info)=>{log('setup',info);throw new Error('actual ignored dependency failure')}}]},{name:'cleanup',tests:[{title:'not admitted',run:()=>assert.fail('implicit teardown admitted with no-deps')}]}]});
""", code=1, options=['--project','body','--project','setup','--no-deps'])
    assert [event['event'] for event in events] == ['body','setup']
    assert [project['status'] for project in value['projects']] == ['passed','failed','notSelected']
    assert value['unrun'] == []

    value, events = execute('no-deps-failed-setup-does-not-block-later-project', """
export default defineNativeSuite({projects:[{name:'setup',tests:[{title:'setup',run:({},info)=>{log('setup',info);throw new Error('actual ignored setup failure')}}]},{name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]}]});
""", code=1, options=['--no-deps'])
    assert [event['event'] for event in events] == ['setup','body']
    assert [project['status'] for project in value['projects']] == ['failed','passed']
    assert value['unrun'] == []

    value, events = execute('required-dependency-focus-is-not-primary-filter', """
export default defineNativeSuite({projects:[{name:'setup',default:false,tests:[{title:'setup1',only:true,run:({},info)=>log('setup1',info)},{title:'setup2',run:({},info)=>log('setup2',info)}]},{name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]}]});
""", options=['--project','body'])
    assert [event['event'] for event in events] == ['setup1','setup2','body']

    value, events = execute('primary-focus-full-support', """
export default defineNativeSuite({projects:[{name:'setup',tests:[{title:'setup1',run:({},info)=>log('setup1',info)},{title:'setup2',run:({},info)=>log('setup2',info)}]},{name:'body',dependencies:['setup'],tests:[{title:'filtered',run:()=>assert.fail('unfocused primary admitted')},{title:'focused',only:true,run:({},info)=>log('focused',info)}]}]});
""")
    assert [event['event'] for event in events] == ['setup1','setup2','focused']

    value, events = execute('excluded-empty-suite-focus-does-not-filter-primary', """
export default defineNativeSuite({projects:[{name:'setup',default:false,entries:[{title:'empty focused',only:true,entries:[]},{title:'setup',run:({},info)=>log('setup',info)}]},{name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]}]});
""", options=['--project','body'])
    assert [event['event'] for event in events] == ['setup','body']

    value, events = execute('empty-selected-focused-suite-runs-no-orphan-support', """
export default defineNativeSuite({projects:[{name:'setup',tests:[{title:'not admitted',run:()=>assert.fail('orphan setup admitted')}]},{name:'body',dependencies:['setup'],entries:[{title:'empty focused',only:true,entries:[]}]}]});
""", options=['--pass-with-no-tests'])
    assert events == [] and value['attempts'] == []
    assert all(project['status'] == 'notSelected' for project in value['projects'])
except BaseException:
    (root/'first-failure.json').write_text(json.dumps({'completedGroups':groups,'scope':'First actual selector/composition failure preserved; no assertion retries'},indent=2)+'\n')
    raise
finally:
    after = {str(path): digest(path) for path in inputs}
    (root/'inputs-after.json').write_text(json.dumps(after, indent=2)+'\n')
    assert before == after, 'Actual native executable/SDK/checker inputs changed'
(root/'index.json').write_text(json.dumps({'passed':True,'groups':groups,'inputsUnchanged':True,'scope':'Authored actual native CLI project selectors, primary test-ID/shard composition, full dependency/teardown support and physical worker receipts; not unchanged upstream/full projects parity'},indent=2)+'\n')
print('Actual native project selectors:', len(groups), 'groups passed;', root)
