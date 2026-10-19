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
        assert step['physicalDurationMs'] == step['receiptMonotonicMs'] - step['startedMonotonicMs']
        assert step['receipt'] in ('settled', 'workerExited')
        if step['parentId'] != '0':
            parent = ids[step['parentId']]
            assert parent['generation'] == step['generation'] and parent['callbackToken'] == step['callbackToken'] and parent['scopeId'] == step['scopeId'] and parent['scopeGeneration'] == step['scopeGeneration'] and parent['pid'] == step['pid']
        assert value['_xml'].find(f".//property[@name='native.step{step['id']}.pid']").attrib['value'] == str(step['pid'])
        assert f'data-step-id="{step["id"]}"' in value['_html']
    groups.append({'name': name, 'report': str(report), 'actualExitCode': actual.returncode})
    return value

v = run('native-admission-values-and-object-identity', "export default defineNativeSuite({tests:[{title:'returns',run:async(_,i)=>{const value={actual:7};assert.equal(await i.step('same object',()=>value),value);assert.equal(await i.step('sync',()=>10),10);assert.equal(await i.step('async',async()=>{await wait(5);return 20}),20)}}]});")
assert [step['status'] for step in v['steps']] == ['passed'] * 3 and v['summary']['expected'] == 1
v = run('parallel-nested-actual-native-ancestry', "export default defineNativeSuite({tests:[{title:'parallel',run:async(_,i)=>{const result=await i.step('grand',async()=>Promise.all([i.step('branch1',()=>i.step('child1',()=>7)),i.step('branch2',async()=>{await wait(3);return i.step('child2',()=>8)})]));assert.deepEqual(result,[7,8])}}]});")
rows = {step['title']: step for step in v['steps']}
assert rows['child1']['parentId'] == rows['branch1']['id'] and rows['child2']['parentId'] == rows['branch2']['id'] and len(rows) == 5
v = run('caught-hard-step-error-does-not-invent-caller-failure', "export default defineNativeSuite({tests:[{title:'caught',run:async(_,i)=>i.step('outer passed',async()=>{try{await i.step('inner failed',()=>{throw Error('actual caught step error')})}catch(error){assert.match(error.message,/actual caught step error/)}return 1})}]});")
assert v['attempts'][0]['status'] == 'passed' and [step['status'] for step in v['steps']] == ['passed', 'failed']
v = run('uncaught-step-error-and-real-fixture-cleanup', "export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{console.log('setup');await use(1);console.log('teardown')}}},hooks:{afterEach:[{run:()=>console.log('afterEach')}]},tests:[{title:'thrown',run:async(_,i)=>i.step('outer',()=>i.step('inner',()=>{throw Error('actual uncaught step error')}))}]});", 1)
assert v['attempts'][0]['status'] == 'failed' and all(step['status'] == 'failed' for step in v['steps']) and 'afterEach' in v['_output'] and 'teardown' in v['_output'] and v['globalErrors'] == []
v = run('own-step-timeout-retains-late-physical-receipt', "export default defineNativeSuite({tests:[{title:'short step',timeout:5000,run:async(_,i)=>i.step('deadline',s=>canceled(s.signal),{timeout:40})}]});", 1)
assert v['attempts'][0]['status'] == 'failed' and v['steps'][0]['status'] == 'timedOut' and v['steps'][0]['receipt'] == 'settled' and v['steps'][0]['physicalDurationMs'] > v['steps'][0]['durationMs'] + 10
v = run('never-step-raw-callback-requires-real-worker-kill', "export default defineNativeSuite({tests:[{title:'never',timeout:5000,run:async(_,i)=>i.step('never',()=>new Promise(()=>{}),{timeout:40})}]});", 1)
assert v['attempts'][0]['status'] == 'failed' and v['steps'][0]['status'] == 'timedOut' and v['steps'][0]['receipt'] == 'workerExited' and v['workers'][0]['signal'] == 9
v = run('original-test-deadline-wins-long-step-timeout', "export default defineNativeSuite({tests:[{title:'test cap',timeout:80,run:async(_,i)=>i.step('long',s=>canceled(s.signal),{timeout:5000})}]});", 1)
assert v['attempts'][0]['status'] == 'timedOut' and v['steps'][0]['status'] == 'timedOut' and v['steps'][0]['deadlineMonotonicMs'] - v['steps'][0]['startedMonotonicMs'] <= 80
v = run('native-global-cancel-retains-real-step-receipt', "export default defineNativeSuite({tests:[{title:'global cap',timeout:5000,run:async(_,i)=>i.step('actual interrupted',s=>canceled(s.signal))}]});", 1, ('--global-timeout', '500'))
assert v['attempts'][0]['status'] == 'interrupted' and v['steps'][0]['status'] == 'interrupted' and v['steps'][0]['receipt'] == 'settled'
v = run('late-native-step-work-drains-before-next-test', "let active=false;export default defineNativeSuite({tests:[{title:'catch timeout',timeout:5000,run:async(_,i)=>{try{await i.step('cooperative timeout',async s=>{active=true;try{await canceled(s.signal)}finally{active=false}},{timeout:40})}catch{}assert.equal(active,true)}},{title:'next',run:()=>{assert.equal(active,false);console.log('next admitted after raw settlement')}}]});")
assert [a['status'] for a in v['attempts']] == ['passed', 'passed'] and len(v['workers']) == 1 and v['steps'][0]['status'] == 'timedOut'
v = run('unawaited-child-gets-native-cancel-and-real-drain', "export default defineNativeSuite({tests:[{title:'floating',run:async(_,i)=>{await i.step('parent',()=>{void i.step('floating child',s=>canceled(s.signal)).catch(()=>{});return 1})}}]});")
assert [step['status'] for step in v['steps']] == ['passed', 'interrupted'] and all(step['receipt'] == 'settled' for step in v['steps'])
v = run('retry-separate-actual-worker-generation-step-ledgers', "export default defineNativeSuite({tests:[{title:'retry',retries:1,run:(_,i)=>i.step('attempt '+i.retry,()=>{if(!i.retry)throw Error('actual retry failure');return 1})}]});")
assert [a['status'] for a in v['attempts']] == ['failed', 'passed'] and len(v['workers']) == 2 and v['steps'][0]['generation'] != v['steps'][1]['generation'] and v['steps'][0]['pid'] != v['steps'][1]['pid'] and v['summary']['flaky'] == 1
v = run('all-hook-and-before-afterEach-native-callback-identity', "const hook={run:async(_,i)=>i.step('step '+i.hook,()=>{console.log(i.hook)})};export default defineNativeSuite({hooks:{beforeAll:[hook],beforeEach:[hook],afterEach:[hook],afterAll:[hook]},tests:[{title:'hook steps',run:async(_,i)=>i.step('step body',()=>1)}]});")
assert {step['callbackKind'] for step in v['steps']} == {'body','beforeAll','beforeEach','afterEach','afterAll'}
assert len(v['steps']) == 5 and len({step['callbackToken'] for step in v['steps']}) == 5 and all(step['callbackToken'] != '0' for step in v['steps'])
v = run('isolated-UTF16-native-step-metadata-reporters', "export default defineNativeSuite({tests:[{title:'UTF16',run:(_,i)=>i.step('title\\ud800\\u0000<&',()=>{throw Error('error\\udfff\\u0000<&')})}]});", 1)
assert v['steps'][0]['title'] == 'title\ud800\0<&' and 'error\udfff\0<&' in v['steps'][0]['error']
assert v['_xml'].find(".//property[@name='native.step1.titleXmlSanitizedUnits']").attrib['value'] == '2' and 'original-json' in v['_html']
v = run('stale-info-cannot-admit-steps-from-next-callback', "let old;export default defineNativeSuite({tests:[{title:'save',run:(_,i)=>{old=i.step}},{title:'reject stale',run:async(_,i)=>{assert.throws(()=>old('stale',()=>{throw Error('must never run')}),/no longer active/);await i.step('current',()=>1)}}]});")
assert len(v['steps']) == 1 and v['steps'][0]['testId'] == '2'
v = run('raw-64-capacity-remains-after-logical-timeouts', "export default defineNativeSuite({tests:[{title:'capacity',timeout:5000,run:async(_,i)=>{const results=await Promise.allSettled(Array.from({length:64},(_,n)=>i.step('held'+n,()=>new Promise(()=>{}),{timeout:40})));assert(results.every(r=>r.status==='rejected'));await assert.rejects(i.step('no admission',()=>{throw Error('must not run')}),/admission rejected/)}}]});", 1)
assert len(v['steps']) == 64 and all(step['status'] == 'timedOut' and step['receipt'] == 'workerExited' for step in v['steps']) and v['workers'][0]['signal'] == 9
v = run('cleanup-error-overrides-static-and-dynamic-expected-failure', "export default defineNativeSuite({hooks:{afterEach:[{run:()=>{throw Error('actual cleanup failure')}}]},tests:[{title:'expected failure',expectedFailure:true,run:async(_,i)=>{await i.fail('dynamic');await i.step('actual failure',()=>{throw Error('actual step failure')})}}]});", 1)
assert v['attempts'][0]['status'] == 'failed' and not v['attempts'][0]['expected'] and v['steps'][0]['status'] == 'failed'
v = run('actual-worker-exit-with-no-step-receipt', "export default defineNativeSuite({tests:[{title:'exit',run:(_,i)=>i.step('exit17',()=>process.exit(17))}]});", 1)
assert v['workers'][0]['exitCode'] == 17 and v['steps'][0]['status'] == 'interrupted' and v['steps'][0]['receipt'] == 'workerExited'
v = run('native-live-test-timeout-amendment-remains-step-cap', "export default defineNativeSuite({tests:[{title:'amend',timeout:5000,run:async(_,i)=>{await i.step('live amended',async s=>{await i.setTimeout(80);await canceled(s.signal)},{timeout:5000})}}]});", 1)
assert v['attempts'][0]['status'] == 'timedOut' and v['steps'][0]['status'] == 'timedOut'
v = run('expired-live-hook-timeout-native-cap-not-interruption', "export default defineNativeSuite({hooks:{beforeEach:[{run:()=>{}}]},tests:[{title:'expired amended hook',timeout:5000,run:async(_,i)=>i.step('expired by actual mutation',async s=>{await wait(25);await i.setTimeout(1);await canceled(s.signal)},{timeout:5000})}]});", 1)
assert v['attempts'][0]['status'] == 'timedOut' and v['steps'][0]['status'] == 'timedOut'
v = run('runtime-skip-with-live-step-native-accepted-cause', "export default defineNativeSuite({tests:[{title:'skip interaction',run:(_,i)=>i.step('actual native skip',()=>i.skip('actual accepted reason'))}]});")
assert v['attempts'][0]['status'] == 'skipped' and v['summary']['skipped'] == 1 and v['steps'][0]['status'] == 'skipped' and v['steps'][0]['testSkip']['requestId'] == v['steps'][0]['receiptTestSkipRequestId']
v = run('step-physical-cleanup-failure-cannot-be-expected', "export default defineNativeSuite({tests:[{title:'expected cannot excuse physical failure',timeout:5000,expectedFailure:true,run:async(_,i)=>i.step('real raw hung',()=>new Promise(()=>{}),{timeout:40})}]});", 1)
assert v['attempts'][0]['status'] == 'failed' and not v['attempts'][0]['expected'] and v['workers'][0]['signal'] == 9
v = run('maximum-UTF16-step-error-and-propagated-root-error-fit-wire', "export default defineNativeSuite({tests:[{title:'maximum error',run:(_,i)=>i.step('maximum title'+'\\ud800'.repeat(4000),()=>{throw '\\udfff'.repeat(8192)})}]});", 1)
assert len(v['steps'][0]['title']) == 4013 and v['steps'][0]['error'].count('\udfff') >= 8000 and v['attempts'][0]['status'] == 'failed' and v['steps'][0]['status'] == 'failed' and v['steps'][0]['receipt'] == 'settled'
(out / 'report.json').write_text(json.dumps({'passed': len(groups), 'scope': 'Actual native public info.step first slice; native scheduling/status/receipt/cleanup, author callbacks and async ancestry only', 'groups': groups}, indent=2) + '\n')
print('PASS', len(groups), 'actual native public step process/report groups')
