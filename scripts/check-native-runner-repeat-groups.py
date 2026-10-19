# SPDX-License-Identifier: MIT
"""Actual native repeat-group concurrency, lifetime, retry and global bounds."""
import argparse
import json
from pathlib import Path

from native_repeat_process_proof import NativeRepeatProof, bodies, reports


def fixture():
    return 'fixtures:{w:{scope:"worker",automatic:true,setup:async(_,use,info)=>{mark("worker-setup",info);await use(process.pid);mark("worker-teardown",info);}}},'


def tests(held=False, retry=False):
    wait = 'await waitFor(()=>existsSync(control+"/release"));' if held else ''
    return '[' + ','.join('{fixtures:["w"],title:' + json.dumps(label) + ',retries:' + str(int(retry)) + ',run:async(f,info)=>{assert.equal(f.w,process.pid);mark("body",info,{label:' + json.dumps(label) + ',held:' + str(held).lower() + ',output:info.outputDir,snapshot:info.snapshotPath("same.txt")});writeFileSync(info.outputPath("actual.txt"),"actual group output");const identity={native:true};assert.strictEqual(await info.step("owned group step",()=>identity),identity);' + wait + ('if(info.repeatEachIndex===0&&info.retry===0)throw Error("genuine serial B failure");' if retry and label == 'B' else '') + '}}' for label in ('A', 'B')) + ']'


def lifetimes(events, expected):
    starts = [event for event in events if event['phase'] == 'worker-setup']
    ends = [event for event in events if event['phase'] == 'worker-teardown']
    body_pids = {event['pid'] for event in bodies(events)}
    assert len(starts) == len(ends) == len(body_pids) == expected
    assert {event['pid'] for event in starts} == {event['pid'] for event in ends} == body_pids
    for pid in body_pids:
        positions = [index for index, event in enumerate(events) if event['pid'] == pid]
        assert events[positions[0]]['phase'] == 'import'
        setup = next(index for index, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'worker-setup')
        teardown = next(index for index, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'worker-teardown')
        assert all(setup < index < teardown for index, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'body')


def main():
    parser = argparse.ArgumentParser()
    for field in ('executable', 'sdk-directory', 'node', 'supervisor', 'directory'):
        parser.add_argument('--' + field, type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true', help='Write fixtures and pins only; launch no processes.')
    args = parser.parse_args()
    proof = NativeRepeatProof(args, __file__)
    try:
        # Only A0/B0 are selected; two held bodies prove SAME-repeat overlap.
        parallel = 'export default defineNativeSuite({repeatEach:2,fullyParallel:true,' + fixture() + 'tests:' + tests(held=True) + '});'
        doc, events, _, row = proof.run('same-repeat-sharded-parallel-overlap', {'one.spec.mjs': parallel}, options=['--shard', '1/2'], release_after=2)
        if not args.prepare_only:
            assert sorted((event['label'], event['repeatEachIndex']) for event in bodies(events)) == [('A', 0), ('B', 0)]
            assert len(doc['directory']['repeatGroups']) == len(doc['sources']) == 2
            assert all(group['mode'] == 'parallel' for group in doc['directory']['repeatGroups'])
            assert row['peakObservedHosts'] == 2 and len(row['observedHeldBodyIdentities']) == 2
            lifetimes(events, 2)

        doc, events, _, row = proof.run('all-parallel-repeat-group-lifetimes', {'one.spec.mjs': parallel}, options=['--preserve-output', 'failures-only'], release_after=2)
        if not args.prepare_only:
            assert sorted((event['label'], event['repeatEachIndex']) for event in bodies(events)) == [('A', 0), ('A', 1), ('B', 0), ('B', 1)]
            assert len(doc['directory']['repeatGroups']) == len(doc['sources']) == 4
            assert all(group['mode'] == 'parallel' and len(group['selectedLocalIds']) == 1 for group in doc['directory']['repeatGroups'])
            assert row['peakObservedHosts'] == 2
            lifetimes(events, 4)
            assert len({event['output'] for event in bodies(events)}) == 4 and len({event['snapshot'] for event in bodies(events)}) == 1
            retention = [item for source in reports(doc) for item in source['outputRetention']]
            assert len(retention) == 4 and all(item['status'] == 'removed' and not Path(item['originalPath']).exists() for item in retention)

        default = 'export default defineNativeSuite({repeatEach:2,' + fixture() + 'tests:' + tests() + '});'
        doc, events, _, _ = proof.run('default-group-shares-worker-fixture-per-repeat', {'one.spec.mjs': default})
        if not args.prepare_only:
            assert len(doc['directory']['repeatGroups']) == len(doc['sources']) == 2
            assert all(group['mode'] == 'default' and len(group['selectedLocalIds']) == 2 for group in doc['directory']['repeatGroups'])
            for repeat in (0, 1):
                observed = [event for event in bodies(events) if event['repeatEachIndex'] == repeat]
                assert [event['label'] for event in observed] == ['A', 'B'] and len({event['pid'] for event in observed}) == 1
            lifetimes(events, 2)

        serial = 'export default defineNativeSuite({repeatEach:2,fullyParallel:true,' + fixture() + 'entries:[{title:"serial",mode:"serial",entries:' + tests(retry=True) + '}]});'
        doc, events, _, _ = proof.run('serial-whole-group-retry-fresh-native-worker', {'one.spec.mjs': serial})
        if not args.prepare_only:
            assert len(doc['directory']['repeatGroups']) == len(doc['sources']) == 2
            assert all(group['mode'] == 'serial' for group in doc['directory']['repeatGroups'])
            for repeat, retries in ((0, (0, 1)), (1, (0,))):
                observed = [event for event in bodies(events) if event['repeatEachIndex'] == repeat]
                assert [(event['label'], event['retry']) for event in observed] == [(label, retry) for retry in retries for label in ('A', 'B')]
                for retry in retries:
                    wave = [event for event in observed if event['retry'] == retry]
                    assert len({event['pid'] for event in wave}) == 1
                if repeat == 0:
                    assert observed[0]['pid'] != observed[2]['pid']
                    assert observed[0]['testId'] == observed[2]['testId'] and observed[1]['testId'] == observed[3]['testId']
            assert len({event['testId'] for event in bodies(events)}) == 4
            lifetimes(events, 3)
            failed = [attempt for source in reports(doc) for attempt in source['attempts'] if attempt['status'] == 'failed']
            assert len(failed) == 1 and 'genuine serial B failure' in failed[0]['error']

        body = 'export default defineNativeSuite({repeatEach:2,projects:[{name:"setup",repeatEach:1,teardown:"cleanup",tests:[{title:"setup",run:(_,info)=>mark("body",info)}]},{name:"body",dependencies:["setup"],fullyParallel:true,tests:[{title:"actual",run:async(_,info)=>{mark("body",info,{held:true});await waitFor(()=>existsSync(control+"/release"));}}]},{name:"cleanup",repeatEach:1,tests:[{title:"cleanup",run:(_,info)=>mark("body",info)}]}]});'
        doc, events, _, row = proof.run('cross-file-global-worker-pool-and-project-barriers', {'a.spec.mjs': body, 'b.spec.mjs': body}, options=['--project', 'body'], release_after=2)
        if not args.prepare_only:
            observed = bodies(events)
            assert len([event for event in observed if event['project'] == 'setup']) == len([event for event in observed if event['project'] == 'cleanup']) == 2
            assert sorted(event['repeatEachIndex'] for event in observed if event['project'] == 'body') == [0, 0, 1, 1]
            setup_positions = [index for index, event in enumerate(observed) if event['project'] == 'setup']
            cleanup_positions = [index for index, event in enumerate(observed) if event['project'] == 'cleanup']
            assert all(max(setup_positions) < index < min(cleanup_positions) for index, event in enumerate(observed) if event['project'] == 'body')
            assert row['peakObservedHosts'] == 2
            pids = {role: {event['pid'] for event in observed if event['project'] == role} for role in ('setup', 'body', 'cleanup')}
            hosts = {role: [host for segment in doc['directory']['segments'] if segment['sourceId'] for host in segment['timing']['hosts'] if host['pid'] in pids[role]] for role in pids}
            assert len(hosts['setup']) == len(hosts['cleanup']) == 2 and len(hosts['body']) == 4
            setup_exit = max(host['waitpidCapturedMonotonicMs'] for host in hosts['setup'])
            cleanup_start = min(host['admittedMonotonicMs'] for host in hosts['cleanup'])
            assert all(setup_exit <= host['admittedMonotonicMs'] <= host['waitpidCapturedMonotonicMs'] <= cleanup_start for host in hosts['body'])

        # One failing group and a real held peer; later groups stay selected-only.
        fail = 'export default defineNativeSuite({repeatEach:2,fullyParallel:true,tests:[{title:"failure",run:async(_,info)=>{mark("body",info);await waitFor(()=>existsSync(control+"/peer-held"));throw Error("genuine group failure cap");}}]});'
        held = 'export default defineNativeSuite({repeatEach:2,fullyParallel:true,tests:[{title:"held",run:async(_,info)=>{mark("body",info);writeFileSync(control+"/peer-held","");await aborted(info);mark("aborted",info);}}]});'
        doc, events, _, _ = proof.run('one-global-failure-cap-across-repeated-files', {'a.spec.mjs': fail, 'b.spec.mjs': held}, options=['--max-failures', '1'], code=1)
        if not args.prepare_only:
            observed = bodies(events)
            assert len(observed) == 2 and all(event['repeatEachIndex'] == 0 for event in observed)
            assert any(event['phase'] == 'aborted' for event in events)
            assert sorted(attempt['status'] for source in reports(doc) for attempt in source['attempts']) == ['failed', 'interrupted']
            unstarted = [row for row in doc['tests'] if row['repeat']['repeatEachIndex'] == 1]
            assert len(unstarted) == 2 and all(row['nativeSelectionOnly'] and row['sourceId'] is None for row in unstarted)

        held_deadline = 'export default defineNativeSuite({repeatEach:3,fullyParallel:true,tests:[{title:"held",run:async(_,info)=>{await info.setTimeout(0);mark("body",info);await aborted(info);mark("aborted",info);}}]});'
        doc, events, _, _ = proof.run('one-original-global-deadline-across-repeat-groups', {'a.spec.mjs': held_deadline, 'b.spec.mjs': held_deadline}, options=['--global-timeout', '2000'], code=1)
        if not args.prepare_only:
            assert len(bodies(events)) == 2 and all(event['repeatEachIndex'] == 0 for event in bodies(events))
            assert len([event for event in events if event['phase'] == 'aborted']) == 2
            assert all(attempt['status'] == 'interrupted' for source in reports(doc) for attempt in source['attempts'])
            assert any('original global timeout of 2000ms exceeded' in error for source in reports(doc) for error in source['globalErrors'])
            unstarted = [row for row in doc['tests'] if row['repeat']['repeatEachIndex'] > 0]
            assert len(unstarted) == 4 and all(row['nativeSelectionOnly'] and row['sourceId'] is None for row in unstarted)

        declared = 'export default defineNativeSuite({projects:[' + ','.join('{name:"declared-' + str(count) + '",repeatEach:' + str(count) + ',tests:[{title:"actual",run:(_,info)=>{assert.equal(info.project.repeatEach,' + str(count) + ');mark("body",info);}}]}' for count in (0, 1, 3)) + ']});'
        doc, events, _, _ = proof.run('declared-project-counts-remain-distinct-from-primary-cli', {'one.spec.mjs': declared}, options=['--repeat-each', '2'])
        if not args.prepare_only:
            assert sorted((event['project'], event['declaredRepeatEach'], event['repeatEachIndex']) for event in bodies(events)) == [(f'declared-{count}', count, repeat) for count in (0, 1, 3) for repeat in (0, 1)]
            units = doc['directory']['repeatSelection']['units']
            assert sorted((unit['declaredRepeatEach'], unit['effectiveRepeatEach'], unit['executions']) for unit in units) == [(0, 2, 2), (1, 2, 2), (3, 2, 2)]
        direct = 'export default defineNativeSuite({repeatEach:0,projects:[' + ','.join('{name:"once-' + str(count) + '",repeatEach:' + str(count) + ',tests:[{title:"direct",run:(_,info)=>{assert.equal(info.project.repeatEach,' + str(count) + ');assert.equal(info.repeatEachIndex,0);assert.equal(info.testId,"' + str(count + 1) + '");mark("body",info);}}]}' for count in (0, 1)) + ']});'
        doc, events, _, _ = proof.run('direct-once-project-zero-and-one-native-sidecar', {'one.spec.mjs': direct}, direct=True, workers=1)
        if not args.prepare_only:
            assert 'directory' not in doc and len(doc['workers']) == len(doc['attempts']) == 2
            assert len([event for event in events if event['phase'] == 'import']) == 2
            assert sorted((event['project'], event['declaredRepeatEach'], event['repeatEachIndex'], event['testId']) for event in bodies(events)) == [('once-0', 0, 0, '1'), ('once-1', 1, 0, '2')]
            assert {event['pid'] for event in bodies(events)} == {worker['pid'] for worker in doc['workers']}
            assert len({event['pid'] for event in bodies(events)}) == 2
            assert all(attempt['status'] == 'passed' and attempt['expected'] for attempt in doc['attempts'])
            assert doc['summary']['expected'] == 2 and doc['globalErrors'] == []
            assert doc['repeatConfiguration'] == {'primaryOverride':None, 'root':0, 'projectDeclarations':[{'project':1,'repeatEach':0},{'project':2,'repeatEach':1}], 'resolved':[{'project':index+1,'role':'primary','declaredRepeatEach':index,'effectiveRepeatEach':index,'executions':1} for index in (0,1)]}
        proof.save()
        print(json.dumps({'preparedOnly': args.prepare_only, 'groups': len(proof.groups), 'index': str(proof.root / 'index.json')}))
    except BaseException:
        proof.save()
        print('Preserved actual first negative:', proof.root / 'index.json', flush=True)
        raise


if __name__ == '__main__':
    main()
