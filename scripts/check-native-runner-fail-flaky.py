# SPDX-License-Identifier: MIT
"""Actual native final flaky-run policy; retries/scheduling remain native facts."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from native_repeat_process_proof import NativeRepeatProof, bodies, digest, physical_receipts, reports


def all_attempts(doc):
    return [attempt for source in reports(doc) for attempt in source['attempts']]


def assert_owners(doc):
    generations = []
    for source in reports(doc):
        workers = {worker['workerIndex']: worker for worker in source['workers']}
        assert len(workers) == len(source['workers'])
        for attempt in source['attempts']:
            assert workers[attempt['workerIndex']]['pid'] == attempt['pid']
            generations.append(attempt['generation'])
        for record in source['steps'] + source['attachments']:
            assert any(all(record[key] == attempt[key] for key in ('testId', 'generation', 'workerIndex', 'pid')) for attempt in source['attempts'])
        assert all(step['receiptOK'] and step['status'] == 'passed' for step in source['steps'])
        assert all(item['writerExitConfirmed'] and item['writerExitCode'] == 0 for item in source['attachments'])
    assert len(generations) == len(set(generations))


def assert_policy(doc, enabled, count):
    assert_owners(doc)
    assert doc['summary']['flaky'] == count
    if enabled:
        assert doc['runPolicy'] == {'failOnFlakyTests': True, 'flakyTests': count, 'failed': bool(count)}
    else:
        assert 'runPolicy' not in doc
    for source in reports(doc):
        assert source['globalErrors'] == []
        assert source['summary']['unexpected'] == source['summary']['didNotRun'] == 0
        if source is not doc:
            assert 'runPolicy' not in source
    if 'sources' in doc:
        assert doc['summary']['failedSources'] == 0
        assert doc['summary']['runFailed'] is bool(enabled and count)


def assert_junit(path, enabled, count, authored):
    root = ET.parse(path).getroot()
    assert len(root.findall('.//testcase')) == authored
    assert root.findall('.//failure') == root.findall('.//error') == []
    values = {}
    for item in root.findall('.//property'):
        if item.attrib.get('name') in ('native.failOnFlakyTests', 'native.flakyTests', 'native.runPolicyFailed'):
            values.setdefault(item.attrib['name'], set()).add(item.attrib['value'])
    assert values == ({'native.failOnFlakyTests': {'true'}, 'native.flakyTests': {str(count)},
                       'native.runPolicyFailed': {str(bool(count)).lower()}} if enabled else {})


def fixture():
    return 'fixtures:{w:{scope:"worker",automatic:true,setup:async(_,use,info)=>{mark("worker-setup",info);await use(process.pid);mark("worker-teardown",info);}}},'


def test(label, run='', retries=0, **extra):
    fields = ''.join(',' + name + ':' + json.dumps(value) for name, value in extra.items())
    return '{title:' + json.dumps(label) + ',fixtures:["w"],retries:' + str(retries) + fields + ',run:async(f,info)=>{assert.equal(f.w,process.pid);mark("body",info,{label:' + json.dumps(label) + '});' + run + '}}'


def assert_lifetimes(events):
    selected = bodies(events)
    starts = [event for event in events if event['phase'] == 'worker-setup']
    ends = [event for event in events if event['phase'] == 'worker-teardown']
    assert len(starts) == len(ends)
    assert {event['pid'] for event in starts} == {event['pid'] for event in ends} == {event['pid'] for event in selected}
    for pid in {event['pid'] for event in selected}:
        setup = next(i for i, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'worker-setup')
        cleanup = next(i for i, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'worker-teardown')
        assert all(setup < i < cleanup for i, event in enumerate(events) if event['pid'] == pid and event['phase'] == 'body')


def main():
    parser = argparse.ArgumentParser()
    for field in ('executable', 'merge-executable', 'sdk-directory', 'node', 'supervisor', 'directory'):
        parser.add_argument('--' + field, type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true', help='Write fixture bytes and pin inputs; launch no native/Node processes.')
    args = parser.parse_args()
    proof = NativeRepeatProof(args, __file__)
    proof.pins[str(args.merge_executable.resolve())] = digest(args.merge_executable)
    try:
        recover = test('recover', 'if(!info.retry)throw Error("actual retry-zero failure");await info.step("real retry step",()=>{});await info.attach("actual retry artifact",{body:Buffer.from("genuine retry receipt")});', retries=1)
        later = test('later')
        direct = 'export default defineNativeSuite({' + fixture() + 'tests:[' + recover + ',' + later + ']});'
        originals = []
        for enabled in (False, True):
            name = 'direct-policy-' + ('enabled' if enabled else 'absent')
            junit = proof.root / name / 'actual.xml'
            doc, events, case, row = proof.run(name, {'one.spec.mjs': direct}, direct=True, workers=1,
                options=['--max-failures', '1', '--reporter', 'none', '--junit', str(junit)] + (['--fail-on-flaky-tests'] if enabled else []), code=int(enabled))
            originals.append((doc, case / 'actual.json'))
            if not args.prepare_only:
                assert_policy(doc, enabled, 1)
                assert [(event['label'], event['retry']) for event in bodies(events)] == [('recover', 0), ('recover', 1), ('later', 0)]
                attempt = doc['attempts']
                assert [item['status'] for item in attempt] == ['failed', 'passed', 'passed']
                assert [item['expected'] for item in attempt] == [False, True, True]
                assert attempt[0]['testId'] == attempt[1]['testId'] != attempt[2]['testId']
                assert attempt[0]['pid'] != attempt[1]['pid']
                assert len({item['generation'] for item in attempt}) == 3
                assert doc['summary']['expected'] == 2 and len(doc['steps']) == len(doc['attachments']) == 1
                assert_lifetimes(events)
                assert_junit(junit, enabled, 1, 2)

        name = 'enabled-green-pass-expected-failure-and-skip'
        junit = proof.root / name / 'actual.xml'
        green = 'export default defineNativeSuite({' + fixture() + 'tests:[' + test('always') + ',' + test('expected', 'throw Error("actual static expected failure");', expectedFailure=True) + ',' + test('skipped', 'assert.fail("statically skipped body admitted");', skip=True) + ']});'
        doc, events, _, _ = proof.run(name, {'green.spec.mjs': green}, direct=True, workers=1,
            options=['--fail-on-flaky-tests', '--reporter', 'none', '--junit', str(junit)])
        if not args.prepare_only:
            assert_policy(doc, True, 0)
            assert [event['label'] for event in bodies(events)] == ['always', 'expected']
            assert doc['summary']['expected'] == 2 and doc['summary']['skipped'] == 1
            assert [(item['status'], item['expected']) for item in doc['attempts']] == [('passed', True), ('failed', True)]
            assert_lifetimes(events)
            assert_junit(junit, True, 0, 3)

        name = 'enabled-project-parallel-dependencies-and-teardown'
        projects = 'export default defineNativeSuite({' + fixture() + 'projects:[{name:"setup",teardown:"cleanup",tests:[' + recover + ']},{name:"dependent",dependencies:["setup"],tests:[' + test('dependent') + ']},{name:"independent",tests:[' + test('independent') + ']},{name:"cleanup",tests:[' + test('cleanup') + ']}]});'
        doc, events, _, _ = proof.run(name, {'projects.spec.mjs': projects}, direct=True, workers=2,
            options=['--fail-on-flaky-tests', '--max-failures', '1', '--reporter', 'none'], code=1)
        if not args.prepare_only:
            assert_policy(doc, True, 1)
            selected = bodies(events)
            assert sorted((event['label'], event['retry']) for event in selected) == [('cleanup', 0), ('dependent', 0), ('independent', 0), ('recover', 0), ('recover', 1)]
            assert all(project['status'] == 'passed' for project in doc['projects'])
            assert doc['summary']['expected'] == 4 and len({item['generation'] for item in doc['attempts']}) == 5
            recovered = [event for event in selected if event['label'] == 'recover']
            assert recovered[0]['testId'] == recovered[1]['testId'] and recovered[0]['pid'] != recovered[1]['pid']
            assert next(i for i, event in enumerate(selected) if event['label'] == 'recover' and event['retry'] == 1) < next(i for i, event in enumerate(selected) if event['label'] == 'dependent') < next(i for i, event in enumerate(selected) if event['label'] == 'cleanup')
            assert_lifetimes(events)

        name = 'enabled-directory-repeat-serial-group-retirement'
        serial = 'export default defineNativeSuite({repeatEach:2,' + fixture() + 'entries:[{title:"atomic",mode:"serial",entries:[' + test('A', retries=1) + ',' + test('B', 'if(info.repeatEachIndex===0&&!info.retry)throw Error("actual serial repeat-zero failure");await info.step("real serial step",()=>{});') + ',' + test('C') + ']},' + later + ']});'
        other = 'export default defineNativeSuite({repeatEach:2,' + fixture() + 'tests:[' + test('other-file') + ']});'
        doc, events, _, _ = proof.run(name, {'a.spec.mjs': serial, 'b.spec.mjs': other}, workers=2,
            options=['--fail-on-flaky-tests', '--max-failures', '1', '--reporter', 'none'], code=1)
        if not args.prepare_only:
            assert_policy(doc, True, 1)
            assert doc['summary']['expected'] == 10 and len(all_attempts(doc)) == 12
            assert all(segment['status'] == 'passed' for segment in doc['directory']['segments'])
            selected = bodies(events)
            for repeat, wanted in ((0, [('A', 0), ('B', 0), ('A', 1), ('B', 1), ('C', 1)]), (1, [('A', 0), ('B', 0), ('C', 0)])):
                serial_events = [event for event in selected if event['repeatEachIndex'] == repeat and event['label'] in ('A', 'B', 'C')]
                assert [(event['label'], event['retry']) for event in serial_events] == wanted
                for retry in {event['retry'] for event in serial_events}:
                    assert len({event['pid'] for event in serial_events if event['retry'] == retry}) == 1
                if repeat == 0:
                    assert serial_events[0]['pid'] != serial_events[2]['pid']
                    assert serial_events[0]['testId'] == serial_events[2]['testId'] and serial_events[1]['testId'] == serial_events[3]['testId']
            assert sorted((event['label'], event['repeatEachIndex']) for event in selected if event['label'] in ('later', 'other-file')) == [('later', 0), ('later', 1), ('other-file', 0), ('other-file', 1)]
            assert len({event['testId'] for event in selected}) == 10
            assert_lifetimes(events)

        # Real report bytes from the two original native runs are the only merge
        # inputs; this checker never edits counts, outcomes, retries or policy.
        for source_enabled, options, code in ((False, ['--fail-on-flaky-tests'], 1), (True, [], 0)):
            original, path = originals[int(source_enabled)]
            case = proof.root / ('merge-flagged-source-default' if source_enabled else 'merge-native-policy-unflagged-source')
            case.mkdir()
            row = {'name': case.name, 'input': str(path), 'expectedExit': code}
            proof.groups.append(row)
            if args.prepare_only:
                row['preparedOnly'] = True
                continue
            input_sha = digest(path)
            output = case / 'merged.json'
            junit = case / 'merged.xml'
            command = [str(args.supervisor), '--report', str(case / 'supervisor.json'), '--timeout-ms', '15000', '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(args.merge_executable), '--input', 'actual-native-source', str(path), '--output', str(output), '--junit', str(junit), *options]
            row['command'] = command
            with (case / 'stdout').open('wb') as out, (case / 'stderr').open('wb') as err:
                result = subprocess.run(command, stdout=out, stderr=err)
            receipt = json.loads((case / 'supervisor.json').read_text())
            row.update(actualExit=result.returncode, supervisor=receipt)
            proof.save()
            assert result.returncode == code and receipt['childExit'] == {'signaled': False, 'code': code, 'rawWaitpidStatus': code << 8}
            assert receipt['descendantsRetired'] and not receipt['cleanupFailed'] and not receipt['timeout'] and not (case / 'stderr').read_bytes()
            assert digest(path) == input_sha
            merged = json.loads(output.read_text())
            assert merged['sources'][0]['report'] == original
            assert merged['summary']['flaky'] == 1 and merged['summary']['unexpected'] == 0 and merged['summary']['runFailed'] is True
            assert merged['summary']['failedSources'] == int(source_enabled)
            assert merged['sources'][0]['runFailed'] is source_enabled
            if source_enabled:
                assert 'runPolicy' not in merged and 'flakyTests' in merged['sources'][0]['runFailureReasons']
            else:
                assert merged['runPolicy'] == {'failOnFlakyTests': True, 'flakyTests': 1, 'failed': True}
            assert_junit(junit, True, 1, 2)
            row['actualPIDs'], _ = physical_receipts(merged, receipt, 1)
            proof.save()
        proof.save()
        print(json.dumps({'preparedOnly': args.prepare_only, 'groups': len(proof.groups), 'index': str(proof.root / 'index.json')}))
    except BaseException:
        proof.save()
        raise


if __name__ == '__main__':
    main()
