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


v=run('runtime-local-skip-parent-and-sibling-continue',"export default defineNativeSuite({tests:[{title:'parent remains passed',run:async(_,i)=>{assert.equal(await i.step('parent',async()=>{assert.equal(await i.step('local',s=>s.skip('real local reason')),undefined);return await i.step('actual sibling',()=>7)}),7);console.log('body continued')}}]});")
assert v['summary']['expected']==1 and not v['summary']['skipped'] and not v['globalErrors'] and v['_output']=='body continued\n'
parent,local,sibling=v['steps'];assert parent['status']=='passed' and local['status']=='skipped' and sibling['status']=='passed' and local['stepSkip']=={'requestId':'1','sourceStepId':local['id'],'reason':'real local reason','descriptionPresent':True} and local['receiptStepSkipRequestId']=='1' and local['receiptOK'] and 'testSkip' not in local and 'skipReason' not in v['attempts'][0]
assert v['_xml'].find(f".//property[@name='native.step{local['id']}.stepSkipSourceStepId']").attrib['value']==local['id'] and 'real local reason' in v['_html']
v=run('false-absent-empty-and-exact-utf16-reasons',"export default defineNativeSuite({tests:[{title:'reasons',run:async(_,i)=>{await i.step('false',s=>{s.skip(false,'unused');return 7});await i.step('absent',s=>s.skip());await i.step('empty',s=>s.skip(''));await i.step('utf16',s=>s.skip('\\ud800'.repeat(8192)));}}]});")
assert v['summary']['expected']==1 and v['steps'][0]['status']=='passed' and 'stepSkip' not in v['steps'][0];assert [(s['stepSkip']['descriptionPresent'],s['stepSkip']['reason']) for s in v['steps'][1:]]==[(False,''),(True,''),(True,'\ud800'*8192)]
v=run('caught-control-still-authoritatively-skips-local-only',"export default defineNativeSuite({tests:[{title:'caught',run:async(_,i)=>{await i.step('local',s=>{try{s.skip('caught')}catch{console.log('actual catch')}return 42});console.log('continued')}}]});")
assert v['summary']['expected']==1 and v['_output']=='actual catch\ncontinued\n' and v['steps'][0]['status']=='skipped' and v['steps'][0]['receiptOK'] and 'receiptStepSkipRequestId' not in v['steps'][0]
v=run('nested-subtree-real-cancel-receipts-and-sibling',"export default defineNativeSuite({tests:[{title:'subtree',run:async(_,i)=>{await i.step('outside',async()=>{await i.step('source',async s=>{const child=i.step('held actual child',async c=>canceled(c.signal));await wait(20);s.skip('only subtree');await child});assert.equal(await i.step('sibling',()=>8),8)});}}]});")
assert v['summary']['expected']==1 and [s['status'] for s in v['steps']]==['passed','skipped','skipped','passed'] and all(s['receiptOK'] for s in v['steps']) and v['steps'][2]['stepSkip']['sourceStepId']==v['steps'][1]['id'] and v['steps'][2]['receiptStepSkipRequestId']==v['steps'][1]['stepSkip']['requestId'] and v['steps'][2]['physicalDurationMs']>=40
v=run('beforeEach-local-skip-does-not-skip-body',"export default defineNativeSuite({hooks:{beforeEach:[{run:async(_,i)=>{await i.step('before',s=>s.skip('one step'));console.log('before continued')}}],afterEach:[{run:(_,i)=>{assert.equal(i.status,'passed');console.log('after')}}]},tests:[{title:'actual body',run:()=>console.log('body')}]});")
assert v['summary']['expected']==1 and v['_output']=='before continued\nbody\nafter\n' and v['steps'][0]['callbackKind']=='beforeEach'
v=run('all-hook-and-cleanup-local-skip-do-not-change-test-expectation',"export default defineNativeSuite({hooks:{beforeAll:[{run:(_,i)=>i.step('all before',s=>s.skip())}],afterEach:[{run:(_,i)=>i.step('each after',s=>s.skip())}],afterAll:[{run:(_,i)=>i.step('all after',s=>s.skip())}]},tests:[{title:'actual body',run:()=>console.log('body')}]});")
assert v['summary']['expected']==1 and v['_output']=='body\n' and len(v['steps'])==3 and all(s['status']=='skipped' and s['receiptOK'] for s in v['steps'])
v=run('same-error-message-is-real-failure-not-local-control',"export default defineNativeSuite({tests:[{title:'actual distinct error identity',run:async(_,i)=>{await i.step('local',s=>{try{s.skip('accepted')}catch{}throw Error('Native local step skip requested')})}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and v['steps'][0]['status']=='skipped' and not v['steps'][0]['receiptOK'] and 'Native local step skip requested' in v['steps'][0]['receiptError'] and 'receiptStepSkipRequestId' not in v['steps'][0]
v=run('real-child-error-overrides-local-skip-and-expected-failure',"export default defineNativeSuite({tests:[{title:'raw failure not excused',expectedFailure:true,run:async(_,i)=>{await i.step('source',async s=>{void i.step('failed child',async c=>{if(!c.signal.aborted)await new Promise(r=>c.signal.addEventListener('abort',r,{once:true}));throw Error('actual local cleanup failure')});await wait(20);s.skip('accepted')})}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and any(not s['receiptOK'] and 'actual local cleanup failure' in s['receiptError'] for s in v['steps'])
v=run('original-step-deadline-survives-late-local-request',"export default defineNativeSuite({tests:[{title:'late',run:async(_,i)=>i.step('actual earlier deadline',async s=>{await wait(80);s.skip('too late')},{timeout:30})}]});",1)
assert v['steps'][0]['status']=='timedOut' and 'stepSkip' not in v['steps'][0] and not v['steps'][0]['receiptOK'] and 'no longer active' in v['steps'][0]['receiptError']
v=run('test-skip-remains-distinct-from-already-skipped-local-step',"export default defineNativeSuite({tests:[{title:'two real causes',run:async(_,i)=>{await i.step('local',s=>s.skip('only local'));await i.step('whole test',()=>i.skip('actual test reason'))}}]});")
assert v['summary']['skipped']==1 and 'stepSkip' in v['steps'][0] and 'testSkip' not in v['steps'][0] and 'testSkip' in v['steps'][1] and 'stepSkip' not in v['steps'][1]
v=run('retry-new-worker-does-not-inherit-local-cause',"export default defineNativeSuite({tests:[{title:'retry',retries:1,run:async(_,i)=>{if(!i.retry){await i.step('first skip',s=>s.skip('first'));throw Error('actual first failure')}assert.equal(await i.step('new callback',()=>7),7)}}]});")
assert [a['status'] for a in v['attempts']]==['failed','passed'] and len(v['workers'])==2 and v['summary']['flaky']==1 and v['steps'][1]['status']=='passed' and 'stepSkip' not in v['steps'][1]
v=run('never-settled-child-forces-real-kill-with-local-cause',"export default defineNativeSuite({tests:[{title:'physical pending',run:async(_,i)=>{await i.step('source',async s=>{void i.step('actual never child',()=>new Promise(()=>{}));await wait(20);s.skip('not a fake receipt')})}}]});",1)
assert v['attempts'][0]['status']=='failed' and not v['attempts'][0]['expected'] and v['workers'][0]['signal']==9 and any(s['status']=='skipped' and s['receipt']=='workerExited' and not s['receiptOK'] and 'stepSkip' in s for s in v['steps'])
v=run('actual-zero-exit-cannot-acknowledge-local-skipped-raw-child',"export default defineNativeSuite({tests:[{title:'zero exit',expectedFailure:true,run:async(_,i)=>{await i.step('source',async s=>{void i.step('actual zero exit',async c=>{if(!c.signal.aborted)await new Promise(r=>c.signal.addEventListener('abort',r,{once:true}));process.exit(0)});await wait(20);s.skip('accepted')})}}]});",1)
assert v['attempts'][0]['status'] in ['failed','interrupted'] and not v['attempts'][0]['expected'] and v['workers'][0]['exitCode']==0 and not v['workers'][0]['signal'] and any(s['status']=='skipped' and s['receipt']=='workerExited' and not s['receiptOK'] and 'stepSkip' in s for s in v['steps'])

v=run('dynamic-failure-mark-cannot-excuse-local-cleanup-fault',"export default defineNativeSuite({tests:[{title:'dynamic expected failure',run:async(_,i)=>{i.fail(true,'ordinary author assertion expected');await i.step('source',s=>{try{s.skip('accepted local')}catch{}throw Error('actual raw cleanup remains failure')})}}]});",1)
assert v['attempts'][0]['status']=='failed' and v['attempts'][0]['expectedStatus']=='failed' and not v['attempts'][0]['expected'] and 'actual raw cleanup remains failure' in v['attempts'][0]['error']
v=run('local-and-test-cause-coexist-on-the-same-real-step',"export default defineNativeSuite({tests:[{title:'distinct actual controls',run:async(_,i)=>{await i.step('local first then test',s=>{try{s.skip('first actual local')}catch{}i.skip('second actual test')})}}]});")
assert v['summary']['skipped']==1 and v['steps'][0]['stepSkip']['reason']=='first actual local' and v['steps'][0]['testSkip']['reason']=='second actual test' and v['steps'][0]['receiptTestSkipRequestId']==v['steps'][0]['testSkip']['requestId'] and 'receiptStepSkipRequestId' not in v['steps'][0]
v=run('earlier-child-cause-survives-parent-local-skip',"export default defineNativeSuite({tests:[{title:'two local causes',run:async(_,i)=>{await i.step('parent',async s=>{void i.step('child',async c=>{try{c.skip('child cause')}catch{}await wait(50)});await wait(20);s.skip('parent cause')})}}]});")
assert v['summary']['expected']==1 and [s['stepSkip']['reason'] for s in v['steps']]==['parent cause','child cause'] and all(s['receiptOK'] for s in v['steps'])
v=run('sequential-local-causes-retire-after-real-receipts',"export default defineNativeSuite({tests:[{title:'bounded physical controls reused',run:async(_,i)=>{for(let n=0;n<160;n++)await i.step('actual '+n,s=>s.skip('reason '+n));console.log('all actual controls retired')}}]});")
assert v['summary']['expected']==1 and len(v['steps'])==160 and all(s['status']=='skipped' and s['receiptOK'] for s in v['steps']) and v['_output']=='all actual controls retired\n'
v=run('stale-step-info-cannot-change-next-test',"let stale;export default defineNativeSuite({tests:[{title:'original callback',run:(_,i)=>i.step('original',s=>{stale=s.skip})},{title:'next callback',run:()=>{assert.throws(()=>stale('stale'),/no longer active/);console.log('next stays passed')}}]});")
assert v['summary']['expected']==2 and len(v['steps'])==1 and v['steps'][0]['status']=='passed' and v['_output']=='next stays passed\n'


v=run('actual-fixture-cleanup-waits-for-local-subtree-raw-receipt',"let active=false;export default defineNativeSuite({fixtures:{resource:{automatic:true,setup:async(_,use)=>{console.log('setup');await use(1);assert.equal(active,false);console.log('actual fixture cleanup')}}},hooks:{afterEach:[{run:()=>{assert.equal(active,false);console.log('afterEach')}}]},tests:[{title:'local skip with cleanup',run:async(_,i)=>{await i.step('source',async s=>{void i.step('held child',async c=>{active=true;try{await canceled(c.signal)}finally{active=false;console.log('actual raw child settled')}});await wait(20);s.skip('local source')});console.log('body continued')}}]});")
assert v['summary']['expected']==1 and v['_output'].startswith('setup\n') and v['_output'].index('actual raw child settled')<v['_output'].index('afterEach')<v['_output'].index('actual fixture cleanup') and all(s['receipt']=='settled' and s['receiptOK'] for s in v['steps'])
v=run('native-global-cap-does-not-invent-local-physical-receipt',"export default defineNativeSuite({tests:[{title:'global stops raw local cleanup',run:async(_,i)=>{await i.step('source',async s=>{void i.step('cooperative held child',async c=>{if(!i.signal.aborted)await new Promise(r=>i.signal.addEventListener('abort',r,{once:true}));await wait(25);throw c.signal.reason});await wait(20);s.skip('native accepted local')});if(!i.signal.aborted)await new Promise(r=>i.signal.addEventListener('abort',r,{once:true}));throw i.signal.reason}}]});",1,('--global-timeout','3000'))
assert v['globalErrors'] and len(v['attempts'])==1 and v['attempts'][0]['status']=='interrupted' and len(v['steps'])==2 and all(s['status']=='skipped' and s['receipt']=='settled' and s['receiptOK'] for s in v['steps']) and v['steps'][1]['physicalDurationMs']>v['steps'][1]['durationMs']+20

(out/'report.json').write_text(json.dumps({'passed':len(groups),'scope':'Runtime local step skip, native subtree cause/status, actual raw receipt/error/kill ownership; predeclared step.skip not implemented','groups':groups},indent=2)+'\n')
print('PASS',len(groups),'actual native runtime local-step-skip process groups')
