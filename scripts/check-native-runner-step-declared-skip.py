# SPDX-License-Identifier: MIT
"""Actual public runner/step callbacks and independent JSON/XML/HTML decode."""
import argparse
import json
import os
import pathlib
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--executable', required=True, type=pathlib.Path)
p.add_argument('--sdk-directory', required=True, type=pathlib.Path)
p.add_argument('--directory', required=True, type=pathlib.Path)
p.add_argument('--node', required=True)
a = p.parse_args()
exe, sdk, out = a.executable.resolve(), a.sdk_directory.resolve(), a.directory.resolve()
out.mkdir(parents=True, exist_ok=True)
groups = []
pre = "import assert from 'node:assert/strict';import process from 'node:process';import {defineNativeSuite} from " + json.dumps((sdk / 'native-suite.js').as_uri()) + ";const wait=ms=>new Promise(r=>setTimeout(r,ms));const canceled=async signal=>{if(!signal.aborted)await new Promise(r=>signal.addEventListener('abort',r,{once:true}));await wait(25);throw signal.reason};\n"

def run(name, source, code=0, options=()):
    d = out / name
    d.mkdir(exist_ok=True)
    suite, report, xml, html = (d / leaf for leaf in ['suite.mjs', 'report.json', 'report.xml', 'report.html'])
    suite.write_text(pre + source)
    for path in [report, xml, html]:
        path.unlink(missing_ok=True)
    actual = subprocess.run([str(exe), '--node', a.node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(report), '--junit', str(xml), '--html', str(html), *options], capture_output=True, timeout=20)
    (d / 'stdout.bin').write_bytes(actual.stdout)
    (d / 'stderr.bin').write_bytes(actual.stderr)
    assert actual.returncode == code, (name, actual.returncode, actual.stdout, actual.stderr)
    assert not actual.stderr, (name, actual.stderr)
    value = json.loads(report.read_text())
    value['_xml'] = ET.parse(xml)
    value['_html'] = html.read_text()
    value['_output'] = ''.join(bytes.fromhex(worker['stdout']['data']).decode() for worker in value['workers'])
    ids = {step['id']: step for step in value['steps']}
    assert len(ids) == len(value['steps'])
    for worker in value['workers']:
        assert not bytes.fromhex(worker['stderr']['data'])
        try:
            os.kill(worker['pid'], 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(('actual native callback host remains alive', worker['pid']))
    for step in value['steps']:
        actual_attempts = [attempt for attempt in value['attempts'] if attempt['testId'] == step['testId'] and attempt['generation'] == step['generation'] and attempt['workerIndex'] == step['workerIndex'] and attempt['pid'] == step['pid']]
        assert len(actual_attempts) == 1, (name, step)
        assert step['startedMonotonicMs'] <= step['endedMonotonicMs'] <= step['receiptMonotonicMs']
        assert step['durationMs'] == step['endedMonotonicMs'] - step['startedMonotonicMs']
        if step['receipt']=='notInvoked':
            assert step['callbackInvoked'] is False and not step['receiptOK'] and 'physicalDurationMs' not in step and step['startedMonotonicMs']==step['endedMonotonicMs']==step['receiptMonotonicMs'] and int(step['declaredSkipRequestId'])>0 and 'stepSkip' not in step and 'testSkip' not in step
        else:
            assert step['physicalDurationMs'] == step['receiptMonotonicMs'] - step['startedMonotonicMs']
        assert step['receipt'] in ('settled', 'workerExited', 'notInvoked')
        if step['parentId'] != '0':
            parent = ids[step['parentId']]
            assert parent['generation'] == step['generation'] and parent['callbackToken'] == step['callbackToken'] and parent['scopeId'] == step['scopeId'] and parent['scopeGeneration'] == step['scopeGeneration'] and parent['pid'] == step['pid']
        assert value['_xml'].find(f".//property[@name='native.step{step['id']}.pid']").attrib['value'] == str(step['pid'])
        assert f'data-step-id="{step["id"]}"' in value['_html']
    groups.append({'name': name, 'report': str(report), 'actualExitCode': actual.returncode})
    return value



v=run('actual-parent-continues-and-unused-callback-is-never-invoked',"let invoked=0;export default defineNativeSuite({tests:[{title:'parent remains passed',run:async(_,i)=>{assert.equal(await i.step('parent',async()=>{assert.equal(await i.step.skip('omitted',()=>{invoked++;throw Error('must never execute')}),undefined);return i.step('sibling',()=>7)}),7);assert.equal(invoked,0);console.log('actual parent continued')}}]});")
assert v['summary']['expected']==1 and v['summary']['skipped']==0 and [s['status'] for s in v['steps']]==['passed','skipped','passed'] and v['steps'][1]['receipt']=='notInvoked' and v['_output']=='actual parent continued\n'
v=run('utf16-title-and-no-callback-timeout-invention',"export default defineNativeSuite({tests:[{title:'unicode declaration',run:async(_,i)=>{await i.step.skip('title\\ud800\\0<&',async()=>{throw Error('not invoked')},{timeout:1});await wait(20)}}]});")
assert v['summary']['expected']==1 and v['steps'][0]['title']=='title\ud800\0<&' and v['steps'][0]['timeoutMs']==1 and v['steps'][0]['status']=='skipped' and v['steps'][0]['receipt']=='notInvoked' and not v['globalErrors']
v=run('actual-all-hook-and-cleanup-step-declarations',"const hook={run:async(_,i)=>{await i.step.skip('omitted '+i.hook,()=>assert.fail('callback'));console.log(i.hook)}};export default defineNativeSuite({hooks:{beforeAll:[hook],beforeEach:[hook],afterEach:[hook],afterAll:[hook]},tests:[{title:'actual body',run:(_,i)=>i.step.skip('omitted body',()=>assert.fail('callback'))}]});")
assert v['summary']['expected']==1 and len(v['steps'])==5 and {s['callbackKind'] for s in v['steps']}=={'body','beforeAll','beforeEach','afterEach','afterAll'} and all(s['receipt']=='notInvoked' for s in v['steps'])
v=run('runtime-local-cause-does-not-rewrite-declared-not-invoked',"export default defineNativeSuite({tests:[{title:'separate causes',run:(_,i)=>i.step('actual source',async s=>{await i.step.skip('omitted inner',()=>assert.fail('callback'));s.skip('actual runtime local')})}]});")
assert v['summary']['expected']==1 and v['steps'][0]['stepSkip']['reason']=='actual runtime local' and v['steps'][0]['receipt']=='settled' and v['steps'][1]['receipt']=='notInvoked' and 'stepSkip' not in v['steps'][1]
v=run('runtime-test-cause-does-not-invent-callback-for-declared-step',"export default defineNativeSuite({tests:[{title:'test skip after declaration',run:async(_,i)=>{await i.step.skip('omitted',()=>assert.fail('callback'));i.skip('actual test')}}]});")
assert v['summary']['skipped']==1 and v['steps'][0]['receipt']=='notInvoked' and 'testSkip' not in v['steps'][0] and v['attempts'][0]['skipReason']=='actual test'
v=run('original-parent-step-deadline-prevents-late-declaration',"let called=false;export default defineNativeSuite({tests:[{title:'caught timeout',run:async(_,i)=>{try{await i.step('deadline parent',async()=>{await wait(80);await i.step.skip('too late',()=>{called=true})},{timeout:30})}catch{}}},{title:'actual next',run:()=>assert.equal(called,false)}]});")
assert v['summary']['expected']==2 and len(v['steps'])==1 and v['steps'][0]['status']=='timedOut' and v['steps'][0]['receipt']=='settled'
v=run('actual-retry-worker-isolation-and-declaration-records',"export default defineNativeSuite({tests:[{title:'retry',retries:1,run:async(_,i)=>{await i.step.skip('omitted attempt '+i.retry,()=>assert.fail('callback'));if(!i.retry)throw Error('actual first attempt error')}}]});")
assert [a['status'] for a in v['attempts']]==['failed','passed'] and len(v['workers'])==2 and v['summary']['flaky']==1 and len(v['steps'])==2 and all(s['receipt']=='notInvoked' for s in v['steps']) and v['steps'][0]['generation']!=v['steps'][1]['generation']
v=run('stale-declaration-function-cannot-amend-next-test',"let stale;export default defineNativeSuite({tests:[{title:'original callback',run:(_,i)=>{stale=i.step.skip}},{title:'actual next',run:()=>{assert.throws(()=>stale('stale',()=>assert.fail('callback')),/no longer active/)}}]});")
assert v['summary']['expected']==2 and v['steps']==[]
v=run('invalid-author-arguments-do-not-create-native-skipped-record',"export default defineNativeSuite({tests:[{title:'invalid arguments caught',run:(_,i)=>{assert.throws(()=>i.step.skip('bad',null),/callback must be a function/);assert.throws(()=>i.step.skip('bad',()=>1,{timeout:-1}));}}]});")
assert v['summary']['expected']==1 and v['steps']==[]
v=run('parallel-native-declaration-admissions-without-raw-callbacks',"let called=0;export default defineNativeSuite({tests:[{title:'parallel actual declarations',run:async(_,i)=>{await Promise.all(Array.from({length:100},(_,n)=>i.step.skip('omitted '+n,()=>{called++})));assert.equal(called,0)}}]});")
assert v['summary']['expected']==1 and len(v['steps'])==100 and len({s['declaredSkipRequestId'] for s in v['steps']})==100 and all(s['receipt']=='notInvoked' for s in v['steps'])
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native predeclared step admission; real author callback intentionally not invoked; no unchanged custom reporter claim','groups':groups},indent=2)+'\n');print('PASS',len(groups),'actual predeclared native step-skip process groups')
