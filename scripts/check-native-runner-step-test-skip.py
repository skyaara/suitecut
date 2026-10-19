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

v=run('nested-test-skip-native-cause',"export default defineNativeSuite({tests:[{title:'skip',run:(_,i)=>i.step('outer',()=>i.step('inner',()=>i.skip('real reason')))}]});")
assert v['summary']['skipped']==1 and v['summary']['unexpected']==0 and all(s['status']=='skipped' and s['testSkip']['requestId']==s['receiptTestSkipRequestId'] and s['testSkip']['reason']=='real reason' for s in v['steps']) and not v['globalErrors']
v=run('beforeEach-real-skip-skips-body-and-retains-cleanup',"export default defineNativeSuite({hooks:{beforeEach:[{run:(_,i)=>i.step('before step',()=>i.skip('before'))}],afterEach:[{run:(_,i)=>{assert.equal(i.status,'skipped');console.log('after')}}]},tests:[{title:'associated',run:()=>assert.fail('body')}]});")
assert v['summary']['skipped']==1 and v['_output']=='after\n' and v['steps'][0]['callbackKind']=='beforeEach' and v['steps'][0]['status']=='skipped'
v=run('beforeAll-real-skip-subtree-and-afterAll',"export default defineNativeSuite({hooks:{beforeAll:[{run:(_,i)=>i.step('before all',()=>i.skip('all'))}],afterAll:[{run:()=>console.log('actual after all')}]},tests:[{title:'associated',run:()=>assert.fail('body')},{title:'omitted',run:()=>assert.fail('body')}]});")
assert v['summary']['skipped']==2 and len(v['attempts'])==1 and len(v['runtimeSkipped'])==1 and v['_output']=='actual after all\n' and v['steps'][0]['callbackKind']=='beforeAll'
v=run('parallel-skip-retains-actual-raw-settlement',"export default defineNativeSuite({tests:[{title:'skip',run:async(_,i)=>{await Promise.all([i.step('held raw',async s=>{try{await canceled(s.signal)}finally{console.log('actual settled')}}),i.step('request',()=>i.skip('shared cause'))])}}]});")
assert v['summary']['skipped']==1 and v['_output']=='actual settled\n' and all(s['receiptOK'] for s in v['steps']) and max(s['physicalDurationMs'] for s in v['steps'])>=20
v=run('actual-raw-error-overrides-accepted-skip',"export default defineNativeSuite({tests:[{title:'skip but fails',expectedFailure:true,run:async(_,i)=>{await Promise.all([i.step('real raw error',async s=>{if(!s.signal.aborted)await new Promise(r=>s.signal.addEventListener('abort',r,{once:true}));throw Error('real error after native skip')}),i.step('request',()=>i.skip('accepted'))])}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real error after native skip' in v['attempts'][0]['error'] and any(not s['receiptOK'] and 'real error after native skip' in s['receiptError'] for s in v['steps'])
v=run('false-skip-no-cause',"export default defineNativeSuite({tests:[{title:'normal',run:(_,i)=>i.step('normal',()=>{i.skip(false,'unused');return 7})}]});")
assert v['summary']['expected']==1 and v['steps'][0]['status']=='passed' and 'testSkip' not in v['steps'][0]
v=run('raw-never-settlement-kill-retains-skip-cause',"export default defineNativeSuite({tests:[{title:'not cleanly skipped',run:async(_,i)=>{await Promise.all([i.step('never raw',()=>new Promise(()=>{})),i.step('request',()=>i.skip('accepted before kill'))])}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and v['workers'][0]['signal']==9 and any(s['receipt']=='workerExited' and s['status']=='skipped' and not s['receiptOK'] for s in v['steps'])
v=run('utf16-reason-retained-native-report-and-real-receipt',"export default defineNativeSuite({tests:[{title:'skip',run:(_,i)=>i.step('step',()=>i.skip('\\ud800'.repeat(8192)))}]});")
assert v['summary']['skipped']==1 and v['steps'][0]['testSkip']['reason']=='\ud800'*8192 and v['steps'][0]['receiptOK']
v=run('beforeAll-real-raw-step-failure-does-not-propagate-descendants',"export default defineNativeSuite({hooks:{beforeAll:[{run:async(_,i)=>Promise.all([i.step('real failed raw',async s=>{if(!s.signal.aborted)await new Promise(r=>s.signal.addEventListener('abort',r,{once:true}));throw Error('real beforeAll raw step error')}),i.step('request',()=>i.skip('accepted all'))])}],afterAll:[{run:()=>console.log('after actual cleanup')}]},tests:[{title:'associated',expectedFailure:true,run:()=>assert.fail('body')},{title:'unrun descendant',run:()=>assert.fail('body')}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real beforeAll raw step error' in v['attempts'][0]['error'] and not v['runtimeSkipped'] and len(v['unrun'])==1 and v['_output']=='after actual cleanup\n'
v=run('afterEach-real-failure-still-overrides-skip',"export default defineNativeSuite({hooks:{afterEach:[{run:()=>{throw Error('real afterEach cleanup')}}]},tests:[{title:'associated',expectedFailure:true,run:(_,i)=>i.step('request',()=>i.skip('accepted'))}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and 'real afterEach cleanup' in v['attempts'][0]['error']
v=run('retry-new-worker-does-not-inherit-test-skip',"export default defineNativeSuite({tests:[{title:'retry',retries:1,run:async(_,i)=>{if(i.retry===0)await Promise.all([i.step('failure',async s=>{if(!s.signal.aborted)await new Promise(r=>s.signal.addEventListener('abort',r,{once:true}));throw Error('actual first cleanup')}),i.step('skip',()=>i.skip('first attempt'))]);else assert.equal(await i.step('new actual callback',()=>7),7)}}]});")
assert [x['status'] for x in v['attempts']]==['failed','passed'] and len(v['workers'])==2 and v['steps'][-1]['status']=='passed' and 'testSkip' not in v['steps'][-1] and v['summary']['flaky']==1
v=run('same-error-text-is-not-native-test-skip-control',"export default defineNativeSuite({tests:[{title:'actual distinct error identity',run:async(_,i)=>Promise.all([i.step('ordinary matching message',async s=>{if(!s.signal.aborted)await new Promise(r=>s.signal.addEventListener('abort',r,{once:true}));throw Error('Native runtime skip requested')}),i.step('real accepted control',()=>i.skip('real reason'))])}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and any(s['title']=='ordinary matching message' and not s['receiptOK'] and 'Native runtime skip requested' in s['receiptError'] for s in v['steps'])
v=run('actual-zero-exit-does-not-acknowledge-test-skip-raw-callback',"export default defineNativeSuite({tests:[{title:'real zero exit physical boundary',expectedFailure:true,run:async(_,i)=>Promise.all([i.step('actual process exit',async s=>{if(!s.signal.aborted)await new Promise(r=>s.signal.addEventListener('abort',r,{once:true}));process.exit(0)}),i.step('accepted skip',()=>i.skip('retained cause'))])}]});",1)
assert v['attempts'][0]['status'] in ['failed','interrupted'] and not v['attempts'][0]['expected'] and v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'] and any(s['status']=='skipped' and s['receipt']=='workerExited' and not s['receiptOK'] for s in v['steps'])
(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Actual native accepted test-skip with pending steps, genuine raw receipt/error/kill ownership','groups':groups},indent=2)+'\n')
print('PASS',len(groups),'actual native accepted test-skip with pending step process groups')
