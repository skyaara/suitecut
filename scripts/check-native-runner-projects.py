# SPDX-License-Identifier: MIT
"""Actual public CLI project graph, callback metadata and OS isolation receipts.

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
    inputs.extend(args.source_root/name for name in ['native/runner/native_runner.cc','native/runner/project_plan.h','native/runner/project_metadata.h','native/runner/project_report.h','native/runner/runner_report.h','native/runner/retry_plan.h','native/runner/CMakeLists.txt','src/native-suite.ts','src/native-runner-host.ts','src/native-runner-cli.ts','src/native-runner.ts','src/native.ts'])
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
        assert attempt['workerIndex'] in owner[0]['workerIndices']
        assert workers[attempt['workerIndex']]['pid'] == attempt['pid']
    assert len({attempt['generation'] for attempt in value['attempts']}) == len(value['attempts'])
    events = [json.loads(line) for worker in workers.values() for line in bytes.fromhex(worker['stdout']['data']).splitlines()]
    groups.append({'name': name, 'processAndOwnershipPassed': True, 'report': str(folder/'result.json'), 'actualProcess': receipt})
    return value, events


try:
    value, events = execute('dependency-default-teardown-isolation-and-metadata', r'''
const check=(info)=>{assert(info.project);assert(Object.isFrozen(info.project));assert.equal(info.project.metadata.kind,info.project.name);assert.equal(info.project.metadata.nested[1],'\ud800')};
const test=(name)=>({title:name,fixtures:['w'],run:async({w},info)=>{check(info);assert.equal(w.pid,process.pid);assert.equal(w.project,info.project.name);await info.step('project step',()=>{});await info.attach('project artifact',{body:Buffer.from(name),contentType:'text/plain'});log('body:'+name,info)}});
export default defineNativeSuite({fixtures:{w:{scope:'worker',setup:async({},use,info)=>{check(info);log('setup',info);await use({pid:process.pid,project:info.project.name});log('cleanup',info)}}},projects:[
 {name:'body',dependencies:['setup'],metadata:{kind:'body',nested:['π','\ud800']},tests:[test('body')]},
 {name:'setup',default:false,teardown:'teardown',metadata:{kind:'setup',nested:['π','\ud800']},tests:[test('setup')]},
 {name:'teardown',default:false,metadata:{kind:'teardown',nested:['π','\ud800']},tests:[test('teardown')]},
 {name:'manual',default:false,tests:[{title:'must not run',run:()=>assert.fail('disabled project admitted')}]}
]});
''')
    assert [event['event'] for event in events] == ['setup','body:setup','cleanup','setup','body:body','cleanup','setup','body:teardown','cleanup']
    assert [project['status'] for project in value['projects']] == ['passed','passed','passed','notSelected']
    assert [attempt['testId'] for attempt in value['attempts']] == ['2','1','3']
    assert len({attempt['pid'] for attempt in value['attempts']}) == 3
    assert value['summary']['expected'] == 3 and value['summary']['didNotRun'] == 0
    assert value['projects'][0]['metadata']['nested'] == ['π', '\ud800']
    assert len(value['steps']) == 3 and all(step['receiptOK'] is True and step['status'] == 'passed' for step in value['steps'])
    assert len(value['attachments']) == 3
    for artifact in value['attachments']:
        assert artifact['writerExitConfirmed'] is True and artifact['writerExitCode'] == 0
        project = next(project for project in value['projects'] if artifact['testId'] in project['testIds'])
        assert Path(artifact['path']).read_bytes() == project['name'].encode()

    value, events = execute('project-hook-snapshots-and-real-title-ancestry', '''
const hook=(event,path)=>({fixtures:['w'],run:({w},info)=>{assert.equal(info.project.name,w.project);assert.equal(w.pid,process.pid);assert.deepEqual(info.titlePath,['nested','body']);assert.deepEqual(info.suiteTitlePath,path);log(event,info)}});
const project=name=>({name,metadata:{name},hooks:{beforeAll:[hook('project+',[])],afterAll:[hook('project-',[])]},entries:[
 {title:'nested',hooks:{beforeAll:[hook('nested+',['nested'])],beforeEach:[hook('each+',['nested'])],afterEach:[hook('each-',['nested'])],afterAll:[hook('nested-',['nested'])]},entries:[
 {title:'body',fixtures:['w'],run:({w},info)=>{assert.equal(info.project.name,w.project);assert.deepEqual(info.titlePath,['nested','body']);log('body',info)}}]}
]});
export default defineNativeSuite({fixtures:{w:{scope:'worker',setup:async({},use,info)=>{assert(info.project);log('fixture+',info);await use({pid:process.pid,project:info.project.name});log('fixture-',info)}}},hooks:{beforeAll:[hook('root+',[])],afterAll:[hook('root-',[])]},projects:[project('first'),project('second')]});
''')
    expected = ['fixture+','root+','project+','nested+','each+','body','each-','nested-','project-','root-','fixture-']
    assert [event['event'] for event in events] == expected + expected
    assert [event['project'] for event in events] == ['first']*len(expected) + ['second']*len(expected)
    assert [project['status'] for project in value['projects']] == ['passed','passed']
    assert len({attempt['pid'] for attempt in value['attempts']}) == 2

    value, events = execute('explicit-nondefault-project', '''
export default defineNativeSuite({projects:[
 {name:'usual',tests:[{title:'excluded',run:()=>assert.fail('default ran despite explicit selection')}]},
 {name:'manual project',default:false,metadata:{selected:true},tests:[{title:'selected',run:({},info)=>{assert.equal(info.project.name,'manual project');assert.equal(info.project.metadata.selected,true);log('manual',info)}}]}
]});
''', options=['--project','manual project'])
    assert [event['event'] for event in events] == ['manual']
    assert [project['status'] for project in value['projects']] == ['notSelected','passed']

    value, events = execute('selection-order-remains-native-declaration-order', '''
const test=name=>({title:name,run:({},info)=>log(name,info)});
export default defineNativeSuite({projects:[{name:'first',tests:[test('first')]},{name:'second',tests:[test('second')]}]});
''', options=['--project','second','--project','first'])
    assert [event['event'] for event in events] == ['first','second']

    value, events = execute('failed-dependency-blocks-without-attempt-and-keeps-teardown', '''
const test=(name,fail=false)=>({title:name,run:({},info)=>{log(name,info);if(fail)throw new Error('actual setup failure')}});
export default defineNativeSuite({projects:[
 {name:'setup',teardown:'cleanup',tests:[test('setup',true)]},
 {name:'dependent',dependencies:['setup'],tests:[{title:'blocked',run:()=>assert.fail('failed dependency admitted author callback')}]},
 {name:'independent',tests:[test('independent')]},{name:'cleanup',tests:[test('cleanup')]}
]});
''', code=1)
    assert [event['event'] for event in events] == ['setup','independent','cleanup']
    assert [project['status'] for project in value['projects']] == ['failed','blocked','passed','passed']
    assert value['projects'][1]['failedDependency'] == '1'
    assert len(value['unrun']) == 1 and value['unrun'][0]['testId'] == '2'
    assert 'pid' not in value['unrun'][0] and 'generation' not in value['unrun'][0]
    assert value['projects'][1]['workerIndices'] == []

    value, events = execute('expected-failure-dependency-is-successful', '''
export default defineNativeSuite({projects:[
 {name:'setup',tests:[{title:'expected',expectedFailure:true,run:({},info)=>{log('setup',info);throw new Error('actual expected failure')}}]},
 {name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]}
]});
''')
    assert [event['event'] for event in events] == ['setup','body']
    assert [project['status'] for project in value['projects']] == ['passed','passed']
    assert value['attempts'][0]['status'] == 'failed' and value['attempts'][0]['expected'] is True

    value, events = execute('retry-worker-exit-before-dependency-release', '''
export default defineNativeSuite({projects:[
 {name:'setup',tests:[{title:'retry',retries:1,run:({},info)=>{log('setup',info);if(info.retry===0)throw new Error('actual first attempt')}}]},
 {name:'body',dependencies:['setup'],tests:[{title:'body',run:({},info)=>log('body',info)}]}
]});
''')
    assert [attempt['status'] for attempt in value['attempts']] == ['failed','passed','passed']
    assert len({attempt['pid'] for attempt in value['attempts']}) == 3
    assert [attempt['generation'] for attempt in value['attempts']] == ['1','2','3']
    assert value['summary']['flaky'] == 1

    value, events = execute('global-failure-limit-is-not-reset-between-projects', '''
const fail=name=>({title:name,run:({},info)=>{log(name,info);throw new Error('actual '+name)}});
export default defineNativeSuite({projects:[
 {name:'first',teardown:'cleanup',tests:[fail('first')]},{name:'second',tests:[fail('second')]},
 {name:'third',tests:[{title:'not admitted',run:()=>assert.fail('global failure limit reset')}]},
 {name:'cleanup',tests:[{title:'not admitted cleanup',run:()=>assert.fail('cleanup author work admitted beyond global failure policy')}]}
]});
''', code=1, options=['--max-failures','2'])
    assert [event['event'] for event in events] == ['first','second']
    assert [project['status'] for project in value['projects']] == ['failed','failed','notRun','notRun']
    assert len(value['attempts']) == 2 and len(value['unrun']) == 2

    value, events = execute('original-global-deadline-stops-project-and-teardown-admission', '''
export default defineNativeSuite({projects:[
 {name:'first',tests:[{title:'first',run:({},info)=>log('first',info)}]},
 {name:'held',teardown:'cleanup',tests:[{title:'held',run:async({},info)=>{log('held',info);await new Promise(resolve=>info.signal.addEventListener('abort',()=>{log('actual-abort',info);resolve()},{once:true}))}}]},
 {name:'next',tests:[{title:'not admitted',run:()=>assert.fail('author work after global deadline')}]},
 {name:'cleanup',tests:[{title:'not admitted cleanup',run:()=>assert.fail('cleanup author work after global deadline')}]}
]});
''', code=1, options=['--global-timeout','900'])
    assert [event['event'] for event in events] == ['first','held','actual-abort']
    assert [project['status'] for project in value['projects']] == ['passed','interrupted','notRun','notRun']
    assert any('original global timeout of 900ms exceeded' in error for error in value['globalErrors'])

    value, events = execute('cycle-validation-before-author-callback', '''
const bad={title:'not admitted',run:()=>assert.fail('cycle admitted author work')};
export default defineNativeSuite({projects:[{name:'a',dependencies:['b'],tests:[bad]},{name:'b',dependencies:['a'],tests:[bad]}]});
''', code=1)
    assert events == [] and value['attempts'] == []
    assert any('dependency cycle' in error for error in value['globalErrors'])

    value, events = execute('actual-project-metadata-is-pinned-between-workers', '''
const metadata={actualWorkerIndex:process.argv[3]};
export default defineNativeSuite({projects:[
 {name:'first',metadata,tests:[{title:'first',run:({},info)=>log('first',info)}]},
 {name:'second',metadata,tests:[{title:'not admitted',run:()=>assert.fail('mutated actual project metadata was admitted')}]}
]});
''', code=1)
    assert [event['event'] for event in events] == ['first']
    assert len(value['attempts']) == 1 and any('suite metadata changed' in error for error in value['globalErrors'])
    assert len(value['unrun']) == 1 and value['unrun'][0]['testId'] == '2'
    assert 'pid' not in value['unrun'][0] and 'generation' not in value['unrun'][0]
except BaseException:
    (root/'first-failure.json').write_text(json.dumps({'completedGroups':groups,'scope':'First actual failure preserved; no assertion retries'},indent=2)+'\n')
    raise
finally:
    after = {str(path): digest(path) for path in inputs}
    (root/'inputs-after.json').write_text(json.dumps(after, indent=2)+'\n')
    assert before == after, 'Actual native executable/SDK/checker inputs changed'
(root/'index.json').write_text(json.dumps({'passed':True,'groups':groups,'inputsUnchanged':True,'scope':'Authored actual public CLI serial project graph assertions, real OS worker exit and native JSON metadata; not unchanged upstream/full project parity'},indent=2)+'\n')
print('Actual native projects:', len(groups), 'groups passed;', root)
