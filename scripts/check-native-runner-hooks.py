# SPDX-License-Identifier: MIT
"""Actual native CLI hook behavior; Python launches and checks only."""
from native_error_attribution_proof import assert_attempt_secondary
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--host-script', type=Path, required=True)
parser.add_argument('--suite-api', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default=shutil.which('node'))
args = parser.parse_args()
assert args.node
root = args.directory.resolve()
root.mkdir(parents=True, exist_ok=True)
groups = []

def execute(name, source, code=0, options=()):
    folder = root / name
    folder.mkdir(exist_ok=True)
    suite = folder / 'suite.mjs'
    suite.write_text("import assert from 'node:assert/strict';\nimport process from 'node:process';\nimport {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ';\nconst log=(value)=>process.stdout.write(value+"\\n");\n' + source)
    start = time.monotonic()
    actual = subprocess.run([str(args.executable.resolve()), '--node', args.node,
        '--host-script', str(args.host_script.resolve()), '--suite', str(suite),
        '--report', str(folder/'result.json'), '--junit', str(folder/'result.xml'),
        '--html', str(folder/'result.html'), *options], capture_output=True, timeout=15)
    elapsed = (time.monotonic()-start)*1000
    (folder/'native.stdout').write_bytes(actual.stdout)
    (folder/'native.stderr').write_bytes(actual.stderr)
    assert actual.returncode == code, (name, actual.returncode, actual.stderr)
    value = json.loads((folder/'result.json').read_bytes())
    ET.parse(folder/'result.xml')
    assert (folder/'result.html').stat().st_size > 0
    for worker in value['workers']:
        try: os.kill(worker['pid'], 0)
        except ProcessLookupError: pass
        else: raise AssertionError('Worker still physically alive at report publication')
    output = b''.join(bytes.fromhex(worker['stdout']['data']) for worker in value['workers'])
    groups.append({'name': name, 'passed': True, 'elapsedMs': elapsed, 'report': str(folder/'result.json')})
    return value, output, elapsed

value, output, elapsed = execute('fresh-all-hook-and-shared-test-fixtures', '''
const same=(item,n)=>{assert.equal(item.n,n);assert.equal(item.w.pid,process.pid)};
export default defineNativeSuite({fixtures:{
 w:{scope:'worker',setup:async({},use)=>{log('+w');await use({pid:process.pid,n:0});log('-w')}},
 t:{dependencies:['w'],setup:async({w},use,info)=>{const n=++w.n;log('+t'+n);await use({n,w});log('-t'+n)}}
},hooks:{
 beforeAll:[{fixtures:['t'],run:async({t},i)=>{same(t,1);assert.equal(i.hook,'beforeAll');log('all1')}},{fixtures:['t'],run:async({t})=>{same(t,2);log('all2')}}],
 beforeEach:[{fixtures:['t'],run:async({t},i)=>{same(t,i.testId==='1'?3:4);log('each'+i.testId)}}],
 afterEach:[{fixtures:['t'],run:async({t},i)=>{same(t,i.testId==='1'?3:4);log('after'+i.testId)}}],
 afterAll:[{fixtures:['t'],run:async({t})=>{same(t,5);log('end1')}},{fixtures:['t'],run:async({t})=>{same(t,6);log('end2')}}]
},tests:[{title:'one',fixtures:['t'],run:async({t})=>{same(t,3);log('body1')}},{title:'two',fixtures:['t'],run:async({t})=>{same(t,4);log('body2')}}]});
''')
assert value['summary']['expected'] == 2 and len(value['workers']) == 1, value
assert output.decode().splitlines() == ['+w','+t1','all1','-t1','+t2','all2','-t2','+t3','each1','body1','after1','-t3','+t4','each2','body2','after2','-t4','+t5','end1','-t5','+t6','end2','-t6','-w'], output

value, output, elapsed = execute('beforeEach-failure-runs-all-cleanup', '''
export default defineNativeSuite({fixtures:{t:{setup:async({},use)=>{log('fixture+');await use(42);log('fixture-')}}},hooks:{
 beforeEach:[{fixtures:['t'],run:async({t})=>{assert.equal(t,42);log('before');throw new Error('actual beforeEach error')}}],
 afterEach:[{fixtures:['t'],run:async({t})=>{assert.equal(t,42);log('after1');throw new Error('actual cleanup error')}},{fixtures:['t'],run:async({t})=>{assert.equal(t,42);log('after2')}}],
 afterAll:[{run:async()=>{log('end')}}]
},tests:[{title:'not admitted body',run:async()=>{throw new Error('BODY MUST NOT RUN')}}]});
''', 1)
assert output.decode().splitlines() == ['fixture+','before','after1','after2','fixture-','end'], output
assert value['attempts'][0]['status'] == 'failed' and 'actual beforeEach error' in value['attempts'][0]['error']
assert value['globalErrors'] == []
assert_attempt_secondary(value, 'Error: actual cleanup error', first_line=True)

value, output, elapsed = execute('beforeAll-failure-keeps-real-unrun-declarations', '''
export default defineNativeSuite({hooks:{beforeAll:[{run:async()=>{log('before');throw new Error('actual beforeAll error')}}],afterAll:[{run:async()=>{log('after')}}]},tests:[
 {title:'assigned actual failure',run:async()=>{log('INVALID BODY1')}},
 {title:'not admitted two',run:async()=>{log('INVALID BODY2')}},
 {title:'not admitted three',run:async()=>{log('INVALID BODY3')}}]});
''', 1)
assert output.decode().splitlines() == ['before','after'], output
assert len(value['attempts']) == 1 and value['summary']['tests'] == 3
assert value['summary']['didNotRun'] == 2 and len(value['unrun']) == 2
assert all('pid' not in unrun and 'generation' not in unrun for unrun in value['unrun'])

value, output, elapsed = execute('real-retry-restarts-hook-worker', '''
export default defineNativeSuite({hooks:{beforeAll:[{run:async()=>log('start:'+process.pid)}],afterAll:[{run:async()=>log('end:'+process.pid)}]},tests:[
 {title:'actual retry',retries:1,run:async({},info)=>{log('body:'+info.retry);if(info.retry===0)throw new Error('real first attempt')}}]});
''')
assert [a['status'] for a in value['attempts']] == ['failed','passed']
assert value['attempts'][0]['pid'] != value['attempts'][1]['pid'] and value['summary']['flaky'] == 1
assert len(output.decode().splitlines()) == 6

value, output, elapsed = execute('actual-native-body-deadline-and-abort', '''
export default defineNativeSuite({hooks:{afterEach:[{run:async()=>log('cleanup')}],afterAll:[{run:async()=>log('end')}]},tests:[
 {title:'actual timeout',timeout:80,run:async({},info)=>{log('body');await new Promise((resolve,reject)=>{info.signal.addEventListener('abort',()=>{log('aborted');reject(info.signal.reason)},{once:true})})}}]});
''', 1)
assert value['attempts'][0]['status'] == 'timedOut'
assert output.decode().splitlines() == ['body','aborted','cleanup','end'], output
assert value['workers'][0]['signal'] is None

value, output, elapsed = execute('never-afterAll-retained-then-confirmed-kill', '''
export default defineNativeSuite({hooks:{afterAll:[
 {timeout:80,run:async()=>{log('never');await new Promise(()=>{})}},
 {run:async()=>log('later cleanup')}]},tests:[{title:'actual afterAll timeout',run:async()=>log('body')}]});
''', 1)
assert output.decode().splitlines() == ['body','never','later cleanup'], output
assert value['attempts'][0]['status'] == 'timedOut' and value['workers'][0]['signal'] == 9
assert elapsed >= 1000 and elapsed < 4000, elapsed

value, output, elapsed = execute('actual-hook-error-preserves-utf16-and-long-unicode', '''
export default defineNativeSuite({hooks:{beforeEach:[{run:async()=>{throw new Error('actual \\ud800 \\u0000 '+ '界'.repeat(6000))}}]},
 tests:[{title:'exact original scalar',run:async()=>{throw new Error('BODY MUST NOT RUN')}}]});
''', 1)
assert 'actual \ud800 \x00 ' in value['attempts'][0]['error'] and value['attempts'][0]['error'].count('界') == 6000

value, output, elapsed = execute('native-physical-hook-capacity-before-next-admission', '''
export default defineNativeSuite({hooks:{beforeAll:Array.from({length:65},(_,index)=>({timeout:20,run:async()=>{log('actual callback'+index);await new Promise(()=>{})}}))},
 tests:[{title:'actual capacity failure',run:async()=>{throw new Error('BODY MUST NOT RUN')}}]});
''', 1)
assert output.decode().splitlines() == ['actual callback'+str(index) for index in range(64)], output
assert value['workers'][0]['signal'] == 9 and value['attempts'][0]['status'] == 'timedOut'
assert value['globalErrors'] == []
assert_attempt_secondary(value, 'Physical hook callback capacity64 exceeded', scope_id='65',
                         scopes={(str(scope), '1') for scope in range(1, 66)})

value, output, elapsed = execute('independent-all-hook-original-budgets', '''
const delay=()=>new Promise(resolve=>setTimeout(resolve,45));
export default defineNativeSuite({hooks:{beforeAll:[
 {timeout:80,run:async()=>{await delay();log('before1')}},{timeout:80,run:async()=>{await delay();log('before2')}}],
 afterAll:[{timeout:80,run:async()=>{await delay();log('after1')}},{timeout:80,run:async()=>{await delay();log('after2')}}]},
 tests:[{title:'own body deadline',timeout:80,run:async()=>{await delay();log('body')}}]});
''')
assert output.decode().splitlines() == ['before1','before2','body','after1','after2']
assert value['attempts'][0]['status'] == 'passed' and elapsed >= 200

value, output, elapsed = execute('normal-fixture-teardown-uses-declared-budget', '''
export default defineNativeSuite({fixtures:{t:{setup:async({},use)=>{await use(42);await new Promise(resolve=>setTimeout(resolve,1100));log('real fixture teardown')}}},
 hooks:{afterAll:[{run:async()=>log('afterAll')}]},tests:[{title:'legitimate teardown',fixtures:['t'],timeout:2000,run:async({t})=>assert.equal(t,42)}]});
''')
assert output.decode().splitlines() == ['real fixture teardown','afterAll']
assert value['attempts'][0]['status'] == 'passed' and elapsed >= 1000

value, output, elapsed = execute('beforeAll-real-retry-then-success', '''
export default defineNativeSuite({hooks:{beforeAll:[{run:async({},info)=>{log('before'+info.workerIndex);if(info.workerIndex===1)throw new Error('real beforeAll first worker')}}],
 afterAll:[{run:async()=>log('after')}]},tests:[{title:'beforeAll retry',retries:1,run:async()=>log('body')}]});
''')
assert [a['status'] for a in value['attempts']] == ['failed','passed'] and value['summary']['flaky'] == 1
assert output.decode().splitlines() == ['before1','after','before2','body','after']

value, output, elapsed = execute('no-selected-tests-no-hooks-or-factories', '''
export default defineNativeSuite({fixtures:{t:{automatic:true,setup:async()=>{throw new Error('NO FACTORY')}}},
 hooks:{beforeAll:[{run:async()=>{throw new Error('NO BEFORE')}}],afterAll:[{run:async()=>{throw new Error('NO AFTER')}}]},tests:[]});
''', options=('--pass-with-no-tests',))
assert value['summary']['tests'] == 0 and not value['attempts'] and output == b''

value, output, elapsed = execute('beforeAll-is-not-an-expected-body-failure', '''
export default defineNativeSuite({hooks:{beforeAll:[{run:async()=>{throw new Error('actual suite setup failure')}}],afterAll:[{run:async()=>log('afterAll')}]},
 tests:[{title:'expected body failure was never admitted',expectedFailure:true,run:async()=>{throw new Error('BODY MUST NOT RUN')}}]});
''', 1)
assert value['attempts'][0]['expected'] is False and value['attempts'][0]['status'] == 'failed'
assert output == b'afterAll\n'

value, output, elapsed = execute('actual-fixture-teardown-error-still-runs-afterAll', '''
export default defineNativeSuite({fixtures:{t:{setup:async({},use)=>{await use(42);log('teardown');throw new Error('real fixture teardown error')}}},
 hooks:{afterAll:[{run:async()=>log('afterAll')}]},tests:[{title:'actual teardown error',fixtures:['t'],run:async({t})=>{assert.equal(t,42);log('body')}}]});
''', 1)
assert value['attempts'][0]['status'] == 'failed' and 'real fixture teardown error' in value['attempts'][0]['error']
assert output == b'body\nteardown\nafterAll\n'

value, output, elapsed = execute('late-fixture-use-after-real-cancel-settles-physically', '''
export default defineNativeSuite({fixtures:{late:{setup:async({},use)=>{await new Promise(resolve=>setTimeout(resolve,60));await use(42);log('actual late factory settled')}}},
 hooks:{beforeAll:[{fixtures:['late'],timeout:20,run:async()=>log('FORBIDDEN HOOK')}],afterAll:[{run:async()=>log('afterAll')}]},
 tests:[{title:'actual late setup',run:async()=>log('FORBIDDEN BODY')}]});
''', 1)
assert output == b'actual late factory settled\nafterAll\n',output
assert value['attempts'][0]['status'] == 'timedOut' and value['workers'][0]['signal'] is None
assert elapsed < 1000

folder = root/'actual-sigint-with-hook-cleanup'
folder.mkdir(exist_ok=True)
marker = folder/'started'
marker.unlink(missing_ok=True)
suite = folder/'suite.mjs'
suite.write_text("import fs from 'node:fs';\nimport process from 'node:process';\nimport {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ';\nexport default defineNativeSuite({hooks:{afterEach:[{run:async()=>process.stdout.write("cleanup\\n")}],afterAll:[{run:async()=>process.stdout.write("afterAll\\n")}]},tests:[{title:"actual interrupted body",timeout:0,run:async({},info)=>{fs.writeFileSync(' + json.dumps(str(marker)) + ',String(process.pid));await new Promise((resolve,reject)=>info.signal.addEventListener("abort",()=>{process.stdout.write("aborted\\n");reject(info.signal.reason)},{once:true}))}},{title:"not admitted",run:async()=>{throw new Error("MUST NOT RUN")}}]});\n')
actual = subprocess.Popen([str(args.executable.resolve()),'--node',args.node,'--host-script',str(args.host_script.resolve()),'--suite',str(suite),'--report',str(folder/'result.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
deadline = time.monotonic()+5
while not marker.exists():
    assert actual.poll() is None and time.monotonic()<deadline
    time.sleep(0.005)
os.kill(actual.pid, signal.SIGINT)
stdout, stderr = actual.communicate(timeout=5)
assert actual.returncode == 130, (actual.returncode,stderr)
value = json.loads((folder/'result.json').read_bytes())
assert value['workers'][0]['pid'] == int(marker.read_text()), 'Signal readiness must belong to this actual worker'
assert value['attempts'][0]['status'] == 'interrupted' and value['summary']['didNotRun'] == 1
assert b''.join(bytes.fromhex(worker['stdout']['data']) for worker in value['workers']) == b'aborted\ncleanup\nafterAll\n'
for worker in value['workers']:
    try: os.kill(worker['pid'],0)
    except ProcessLookupError: pass
    else: raise AssertionError('Interrupted hook worker survived native report publication')
groups.append({'name':'actual-sigint-with-hook-cleanup','passed':True,'report':str(folder/'result.json')})

for name, source, expected_status in [
    ('sigint-bounds-infinite-cleanup-with-retained-owner', '''
export default defineNativeSuite({hooks:{afterEach:[{run:async()=>{process.stdout.write('infinite cleanup\\n');await new Promise(()=>{})}}],afterAll:[{run:async()=>process.stdout.write('UNREACHABLE AFTERALL\\n')}]},tests:[{title:'interrupted',timeout:0,run:async({},info)=>{mark();await new Promise((resolve,reject)=>info.signal.addEventListener('abort',()=>{process.stdout.write('aborted\\n');reject(info.signal.reason)},{once:true}))}}]});
''', 'interrupted'),
    ('earlier-original-deadline-remains-timeout-after-sigint', '''
export default defineNativeSuite({hooks:{afterEach:[{run:async()=>{mark();process.stdout.write('cleanup after original timeout\\n');await new Promise(()=>{})}}]},tests:[{title:'actual earlier deadline',timeout:80,run:async()=>{await new Promise(()=>{})}}]});
''', 'timedOut'),
]:
    folder = root/name
    folder.mkdir(exist_ok=True)
    marker = folder/'actual-started'
    marker.unlink(missing_ok=True)
    suite = folder/'suite.mjs'
    suite.write_text("import fs from 'node:fs';\nimport process from 'node:process';\nimport {defineNativeSuite} from " + json.dumps(args.suite_api.resolve().as_uri()) + ';\nconst mark=()=>fs.writeFileSync(' + json.dumps(str(marker)) + ',String(process.pid));\n' + source)
    start = time.monotonic()
    actual = subprocess.Popen([str(args.executable.resolve()),'--node',args.node,'--host-script',str(args.host_script.resolve()),'--suite',str(suite),'--report',str(folder/'result.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    while not marker.exists():
        assert actual.poll() is None and time.monotonic()<start+5
        time.sleep(0.005)
    os.kill(actual.pid,signal.SIGINT)
    stdout,stderr = actual.communicate(timeout=5)
    elapsed = (time.monotonic()-start)*1000
    assert actual.returncode == 130,(name,actual.returncode,stderr)
    value = json.loads((folder/'result.json').read_bytes())
    assert value['workers'][0]['pid'] == int(marker.read_text()), 'Signal readiness must belong to this actual worker'
    assert value['attempts'][0]['status'] == expected_status,(name,value)
    assert value['workers'][0]['signal'] == 9 and elapsed >= 900 and elapsed < 3000
    output = b''.join(bytes.fromhex(worker['stdout']['data']) for worker in value['workers'])
    if expected_status == 'interrupted': assert output == b'aborted\ninfinite cleanup\n',output
    else: assert output == b'cleanup after original timeout\n',output
    for worker in value['workers']:
        try: os.kill(worker['pid'],0)
        except ProcessLookupError: pass
        else: raise AssertionError('Native hook worker still exists after signal report')
    groups.append({'name':name,'passed':True,'elapsedMs':elapsed,'report':str(folder/'result.json')})

(root/'report.json').write_text(json.dumps({'passed': len(groups), 'groups': groups,
    'scope': 'Authored actual public native CLI and hook declarations, not unchanged upstream runner parity'}, indent=2)+'\n')
print(json.dumps({'passed': len(groups), 'report': str(root/'report.json')}))
