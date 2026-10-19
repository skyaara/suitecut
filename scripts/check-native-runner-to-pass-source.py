# SPDX-License-Identifier: MIT
"""Genuine retained-browser retry and soft-failure fences; no synthetic receipts."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

FIXTURE = r'''import assert from 'node:assert/strict';
import fs from 'node:fs';
import {channel} from 'node:diagnostics_channel';
import {defineNativeSuite,createNativeBrowserFixtures,expectNative,NativeAssertionError} from API;
const log=(phase,extra={})=>fs.appendFileSync(EVENTS,JSON.stringify({phase,workerPid:process.pid,...extra})+'\n');
const spawnChannel=channel('child_process');
spawnChannel.subscribe(({process:child})=>child.once('spawn',()=>{
 if(fs.realpathSync(child.spawnfile)!==fs.realpathSync(BROWSER))return;
 const raw=fs.readFileSync('/proc/'+child.pid+'/stat','utf8'),f=raw.slice(raw.lastIndexOf(')')+2).trim().split(/\s+/);
 assert.equal(Number(f[1]),process.pid);
 log('helper-spawn',{pid:child.pid,start:f[19],parent:process.pid});
 child.once('exit',(code,signal)=>log('helper-exit',{pid:child.pid,code,signal}));
 child.once('close',(code,signal)=>log('helper-close',{pid:child.pid,code,signal}));
}));
const fixtures=createNativeBrowserFixtures({browser:{executablePath:BROWSER,headless:true,width:80,height:60,framesPerSecond:30,onDiagnostic:text=>fs.appendFileSync(DIAGNOSTICS,text)}});
export default defineNativeSuite({fixtures,tests:[{title:'genuine retained Source iteration fence',timeout:10000,fixtures:['page'],run:async({page},info)=>{
 await page.setContent('<p id=status>before</p>');
 await expectNative(page.locator('#status')).toHaveText('before',{timeout:1000});
 let originalFailure;
 await expectNative(async()=>{
  log('iteration-enter',{testId:info.testId,retry:info.retry});
  try{await expectNative(page.locator('#status')).toHaveText('after',{timeout:200});}
  catch(error){
   assert(error instanceof NativeAssertionError);
   assert.match(error.message,/Received: "before"/);
   assert.equal(originalFailure,undefined,'only one genuine failed observation');
   originalFailure=error;
   await page.setContent('<p id=status>after</p>');
   log('genuine-mismatch-before-throw',{error:error.stack});
   throw error;
  }
  log('native-matcher-passed');
 }).toPass({timeout:3000,intervals:[0]});
 assert(originalFailure instanceof NativeAssertionError);
 assert.equal(await page.locator('#status').textContent(),'after');
 log('body-continued',{testId:info.testId,retry:info.retry});
}},{title:'accepted soft mismatch survives passing callback assertion',timeout:10000,expectedFailure:true,fixtures:['page'],run:async({page},info)=>{
 await page.setContent('<p id=status>before</p>');
 await expectNative(page.locator('#status')).toHaveText('before',{timeout:1000});
 let calls=0,originalSoft;
 await expectNative(async()=>{
  ++calls;
  await expectNative.soft(page.locator('#status')).toHaveText('after',{timeout:200});
  assert.equal(info.errors.length,1);
  originalSoft=info.errors[0];
  assert(originalSoft instanceof NativeAssertionError);
  assert.match(originalSoft.message,/Received: "before"/);
  log('soft-mismatch-accepted',{testId:info.testId,retry:info.retry,error:originalSoft.stack});
 }).toPass({timeout:3000,intervals:[0]});
 assert.equal(calls,1,'soft failure must not schedule another invocation');
 assert.equal(info.errors.length,1);
 assert.equal(info.errors[0],originalSoft);
 log('soft-body-continued',{testId:info.testId,retry:info.retry});
}}]});
'''

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def uint(value, positive=True):
    assert isinstance(value, str) and value.isascii() and value.isdecimal()
    assert value == str(int(value)) and (int(value) > 0 if positive else int(value) >= 0)
    return int(value)


def main():
    parser = argparse.ArgumentParser()
    for name in ('executable', 'browser-executable', 'sdk-directory', 'node', 'supervisor', 'directory'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='to-pass-source-', dir=args.directory.resolve()))
    suite, events_path = directory / 'suite.mjs', directory / 'events.jsonl'
    suite.write_text(FIXTURE.replace('API', json.dumps((args.sdk_directory.resolve() / 'native.js').as_uri())).replace('BROWSER', json.dumps(str(args.browser_executable.resolve()))).replace('EVENTS', json.dumps(str(events_path))).replace('DIAGNOSTICS', json.dumps(str(directory / 'native-stderr'))))
    inputs = [Path(__file__).resolve(), suite, args.executable.resolve(), args.browser_executable.resolve(), args.node.resolve(), args.supervisor.resolve(), *sorted(args.sdk_directory.resolve().rglob('*'))]
    pins = {str(path): digest(path) for path in inputs if path.is_file()}
    index = {'scope': 'One real native Runner invocation with retained CEF Source: recovered ordinary failure plus accepted soft failure; original native receipts only, no replay or installed bridge.', 'inputs': pins, 'preparedOnly': args.prepare_only}
    def save():
        index['changed'] = [path for path, value in pins.items() if not Path(path).is_file() or digest(path) != value]
        (directory / 'index.json').write_text(json.dumps(index, indent=2) + '\n')
        assert not index['changed']
    try:
        if args.prepare_only:
            save()
            print(json.dumps({'prepared': str(directory), 'runtime': 'not launched'}))
            return
        command = [str(args.supervisor), '--report', str(directory / 'supervisor.json'), '--timeout-ms', '45000', '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(args.executable), '--node', str(args.node), '--host-script', str(args.sdk_directory / 'native-runner-host.js'), '--suite', str(suite), '--report', str(directory / 'actual.json'), '--junit', str(directory / 'actual.xml'), '--workers', '1', '--reporter', 'none']
        index['command'] = command
        with (directory / 'stdout').open('wb') as out, (directory / 'stderr').open('wb') as err:
            result = subprocess.run(command, stdout=out, stderr=err)
        receipt = json.loads((directory / 'supervisor.json').read_text())
        index.update(actualExit=result.returncode, physicalSupervisor=receipt)
        save()
        assert result.returncode == 0 and receipt['childExit'] == {'signaled': False, 'code': 0, 'rawWaitpidStatus': 0}
        assert receipt['descendantsRetired'] and not receipt['cleanupFailed'] and not receipt['timeout']
        assert not (directory / 'stderr').read_bytes()
        assert not (directory / 'native-stderr').exists() or not (directory / 'native-stderr').stat().st_size
        doc = json.loads((directory / 'actual.json').read_text())
        assert doc['globalErrors'] == [] and doc['summary']['expected'] == 2 and doc['summary']['unexpected'] == doc['summary']['didNotRun'] == 0
        assert len(doc['attempts']) == 2
        attempt, soft_attempt = doc['attempts']
        workers = {row['workerIndex']: row for row in doc['workers']}
        assert workers and all(row['exitStatusKnown'] and row['exitCode'] == 0 and row['signal'] is None and bytes.fromhex(row['stderr']['data']) == b'' for row in workers.values())
        worker = workers[attempt['workerIndex']]
        assert attempt['status'] == 'passed' and attempt['expected'] and attempt['pid'] == worker['pid']
        assert soft_attempt['status'] == 'failed' and soft_attempt['expected'] and soft_attempt['pid'] == workers[soft_attempt['workerIndex']]['pid']
        rows = doc['callbackAssertions']
        assert len(rows) == 2
        by_attempt = {(row['testId'], row['generation']): row for row in rows}
        row, soft_row = (by_attempt[(actual['testId'], actual['generation'])] for actual in (attempt, soft_attempt))
        for actual, assertion, count in ((attempt, row, 2), (soft_attempt, soft_row, 1)):
            assert assertion['status'] == 'passed' and assertion['attempts'] == count and assertion['receipt'] == 'settled'
            assert all(assertion[key] == actual[key] for key in ('testId', 'generation', 'workerIndex', 'pid'))
        steps = sorted([step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == row['id']], key=lambda step: uint(step['callbackAssertion']['invocation']))
        soft_steps = [step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == soft_row['id']]
        assert len(steps) == 2 and len(soft_steps) == 1 and steps[0]['id'] == row['id'] and steps[-1]['callbackAssertion']['invocation'] == row['lastInvocation']
        assert steps[0]['callbackReceiptOK'] is False and steps[1]['callbackReceiptOK'] is True
        soft_step = soft_steps[0]
        assert soft_step['id'] == soft_row['id'] and soft_step['callbackAssertion']['invocation'] == soft_row['lastInvocation']
        assert soft_step['callbackReceiptOK'] is True and soft_step['receiptOK'] is True and soft_step['status'] == 'failed'
        accepted = doc['softAssertions']
        assert len(accepted) == 1
        mismatch = accepted[0]
        assert mismatch['acceptedMismatch'] is True and mismatch['acceptedDeadline'] is False and mismatch['receipt'] == 'mismatch' and mismatch['rawOutcome'] == 'recognizedMismatch'
        assert mismatch['stepId'] == soft_step['id'] and soft_step['softErrorId'] == mismatch['id'] and soft_attempt['softErrorId'] == mismatch['id']
        assert soft_attempt['error'] == mismatch['error'] and soft_attempt['expectedStatus'] == 'failed'
        assert all(mismatch[key] == soft_step[key] for key in ('testId', 'generation', 'callbackToken', 'scopeId', 'scopeGeneration', 'workerIndex', 'pid', 'callbackKind'))
        assert soft_step['startedMonotonicMs'] <= mismatch['admittedMonotonicMs'] <= mismatch['receiptMonotonicMs'] <= soft_step['callbackReceiptMonotonicMs']
        sources = doc['retainedSources']
        assert sources and len({source['ticketId'] for source in sources}) == len(sources)
        by_source = {source['ticketId']: source for source in sources}
        for source in sources:
            actual_worker = workers[source['workerIndex']]
            assert source['hostPID'] == actual_worker['pid'] and source['startup']['receipt'] == 'credentialedKernelStartup' and source['startup']['parentPID'] == actual_worker['pid']
            assert source['channelDrained'] and source['pidfdExitObserved'] and source['physicalRetirement']['receipt'] == 'hostWaitpidAndDescendantBatchRetired'
        last_by_source = {}
        all_steps = sorted([*steps, *soft_steps], key=lambda actual: actual['startedMonotonicMs'])
        for step in all_steps:
            assertion = row if step['callbackAssertion']['id'] == row['id'] else soft_row
            assert all(step[key] == assertion[key] for key in ('testId', 'generation', 'callbackToken', 'scopeId', 'scopeGeneration', 'workerIndex', 'pid', 'callbackKind')) and step['parentId'] == assertion['parentStepId']
            assert step['receipt'] == 'settled' and step['startedMonotonicMs'] <= step['callbackReceiptMonotonicMs'] <= step['receiptMonotonicMs'] <= assertion['receiptMonotonicMs']
            fence = step['callbackAssertion']['sourceFence']
            source = by_source[fence['ticketId']]
            assert source['hostPID'] == step['pid'] and source['workerIndex'] == step['workerIndex']
            last_nonce, last_epoch = last_by_source.get(source['ticketId'], (0, 0))
            assert uint(fence['nonce']) > last_nonce and uint(fence['epoch']) > last_epoch
            assert uint(fence['resumeEpoch']) > uint(fence['epoch'])
            last_by_source[source['ticketId']] = uint(fence['nonce']), uint(fence['resumeEpoch'])
            assert all(fence[key] is True for key in ('decoderObserved', 'acknowledged', 'resumed', 'hostReady', 'hostResumed'))
            for cut in fence['cuts'].values():
                uint(cut['instance']); uint(cut['sequence'], False); uint(cut['nonce'])
            times = [fence[key] for key in ('observedMonotonicMs', 'acknowledgedMonotonicMs', 'hostReadyMonotonicMs', 'resumedMonotonicMs', 'hostResumedMonotonicMs')]
            assert all(isinstance(value, (float, int)) and math.isfinite(value) and value > 0 for value in times) and times == sorted(times)
            assert step['callbackReceiptMonotonicMs'] <= times[0] and times[-1] < fence['cleanupDeadlineMonotonicMs'] and times[-1] <= step['receiptMonotonicMs']
        assert set(last_by_source) == set(by_source)
        assert steps[1]['startedMonotonicMs'] >= steps[0]['receiptMonotonicMs']
        events = [json.loads(line) for line in events_path.read_text().splitlines()]
        assert events_path.read_bytes().endswith(b'\n')
        phases = [event['phase'] for event in events]
        assert phases.count('iteration-enter') == 2 and phases.count('genuine-mismatch-before-throw') == phases.count('native-matcher-passed') == phases.count('body-continued') == 1
        assert phases.index('genuine-mismatch-before-throw') < phases.index('native-matcher-passed') < phases.index('body-continued')
        assert phases.count('soft-mismatch-accepted') == phases.count('soft-body-continued') == 1
        assert phases.index('soft-mismatch-accepted') < phases.index('soft-body-continued')
        soft_event = next(event for event in events if event['phase'] == 'soft-mismatch-accepted')
        assert soft_event['testId'] == soft_attempt['testId'] and soft_event['retry'] == 0 and mismatch['error'] == soft_event['error']
        spawned = [event for event in events if event['phase'] == 'helper-spawn']
        assert len(spawned) == len(sources)
        for source in sources:
            matches = [event for event in spawned if event['pid'] == source['startup']['pid'] and event['start'] == source['startup']['start'] and event['parent'] == source['hostPID']]
            assert len(matches) == 1
            for phase in ('helper-exit', 'helper-close'):
                actual = [event for event in events if event['phase'] == phase and event['pid'] == matches[0]['pid']]
                assert len(actual) == 1 and actual[0]['code'] == 0 and actual[0]['signal'] is None
        junit = ET.parse(directory / 'actual.xml').getroot()
        assert len(junit.findall('.//testcase')) == 2 and not junit.findall('.//failure') and not junit.findall('.//error')
        pids = {receipt['supervisorPID'], receipt['childPID'], *[actual['pid'] for actual in workers.values()], *[source['startup']['pid'] for source in sources], *[child['pid'] for child in receipt.get('descendants', [])]}
        assert not [pid for pid in pids if Path('/proc', str(pid)).exists()]
        index.update(passed=True, reportSHA256=digest(directory / 'actual.json'), eventSHA256=digest(events_path), sourceFenceReceipts=[step['callbackAssertion']['sourceFence'] for step in all_steps], knownPIDs=sorted(pids), allKnownPIDsAbsentIncludingZ=True)
    finally:
        save()
    print(json.dumps({'status': 'PASS', 'index': str(directory / 'index.json')}))


if __name__ == '__main__':
    main()
