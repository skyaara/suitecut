# SPDX-License-Identifier: MIT
"""Following actual process proof for typed attempt/phase errors; no string dedup or fabricated callbacks."""
import argparse
import json
import pathlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True, type=pathlib.Path)
parser.add_argument('--sdk-directory', required=True, type=pathlib.Path)
parser.add_argument('--supervisor', required=True, type=pathlib.Path)
parser.add_argument('--node', required=True)
parser.add_argument('--directory', required=True, type=pathlib.Path)
a = parser.parse_args()
receipt = a.directory.resolve()
receipt.mkdir(parents=True, exist_ok=True)
invocation = pathlib.Path(tempfile.mkdtemp(prefix='cases-', dir=receipt))
groups = []


def carries(actual, original):
    # The original Error may be the scalar itself or be retained losslessly
    # inside a native explanatory diagnostic. This is an observation assertion,
    # never a production identity or deduplication decision.
    return actual == original or json.dumps(original, ensure_ascii=False) in actual


def absent(pid):
    return not pathlib.Path('/proc', str(pid)).exists()


def run(name, source, expected_exit=1):
    root = invocation / name
    root.mkdir()
    suite = root / 'suite.mjs'
    suite.write_text('import{defineNativeSuite}from ' + json.dumps((a.sdk_directory.resolve() / 'native.js').as_uri())
        + ";import{writeFileSync,readFileSync,existsSync}from'node:fs';const root=" + json.dumps(str(root))
        + ";function remember(key,error){const file=root+'/expected.json',values=existsSync(file)?JSON.parse(readFileSync(file,'utf8')):{};values[key]=String(error.stack??error);writeFileSync(file,JSON.stringify(values));}"
        + source)
    command = [str(a.supervisor.resolve()), '--report', str(root / 'supervisor.json'), '--timeout-ms', '30000',
        '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(a.executable.resolve()),
        '--node', a.node, '--host-script', str(a.sdk_directory.resolve() / 'native-runner-host.js'),
        '--suite', str(suite), '--report', str(root / 'actual.json'),
        '--junit', str(root / 'actual.xml'), '--html', str(root / 'actual.html')]
    result = subprocess.run(command, capture_output=True, timeout=45)
    (root / 'stdout.bin').write_bytes(result.stdout)
    (root / 'stderr.bin').write_bytes(result.stderr)
    physical = json.loads((root / 'supervisor.json').read_text())
    assert result.returncode == expected_exit and not result.stderr, (name, result.returncode, result.stdout, result.stderr)
    assert physical['childExit']['code'] == expected_exit and not physical['childExit']['signaled']
    assert physical['descendantsRetired'] and not physical['cleanupFailed'] and not physical['timeout']
    assert absent(physical['supervisorPID']) and absent(physical['childPID'])
    report = json.loads((root / 'actual.json').read_text())
    expected = json.loads((root / 'expected.json').read_text())
    steps = {step['id']: step for step in report['steps']}
    for attempt in report['attempts']:
        assert absent(attempt['pid'])
        for error in attempt.get('secondaryErrors', []):
            assert all(error[field] == attempt[field] for field in ['testId', 'generation', 'workerIndex', 'pid'])
            assert error['origin'] in ['phaseResult', 'fixtureRaw'] and isinstance(error['error'], str) and error['error']
            for field in ['scopeId', 'scopeGeneration']:
                assert isinstance(error[field], str) and str(int(error[field])) == error[field] and int(error[field]) > 0
            if 'originatingStepId' in error:
                step = steps[error['originatingStepId']]
                assert all(step[field] == attempt[field] for field in ['testId', 'generation', 'workerIndex', 'pid'])
    for output in report['outputs']:
        assert output['writerExitCode'] == 0 and absent(output['writerPid'])
    properties = {item.attrib['name']: item.attrib['value'] for item in ET.parse(root / 'actual.xml').findall('.//property')}
    for attempt_index, attempt in enumerate(report['attempts']):
        for error_index, error in enumerate(attempt.get('secondaryErrors', [])):
            prefix = 'native.attempt' + str(attempt_index) + '.secondaryError' + str(error_index) + '.'
            for field in ['testId', 'generation', 'workerIndex', 'pid', 'scopeId', 'scopeGeneration', 'origin', 'error']:
                assert properties[prefix + field] == str(error[field])
            if 'originatingStepId' in error:
                assert properties[prefix + 'originatingStepId'] == error['originatingStepId']
    groups.append({'name': name, 'report': str(root / 'actual.json'), 'actualExitCode': result.returncode,
        'attempts': len(report['attempts']), 'globalErrors': len(report['globalErrors']),
        'secondaryCounts': [len(row.get('secondaryErrors', [])) for row in report['attempts']],
        'supervisor': str(root / 'supervisor.json')})
    return report, expected, properties, (root / 'actual.html').read_text()


report, expected, _, _ = run('test-fixture-only-preserves-original-error', "const actual=new Error('genuine test fixture teardown');export default defineNativeSuite({fixtures:{cleanup:{setup:async(_,use)=>{await use(1);remember('fixture',actual);throw actual;}}},hooks:{afterEach:[{run:()=>{}}]},tests:[{title:'fixture only',fixtures:['cleanup'],run:()=>{}}]});")
assert not report['globalErrors'] and len(report['attempts']) == 1
attempt = report['attempts'][0]
assert attempt['status'] == 'failed' and not attempt['expected'] and attempt['error'] == expected['fixture']
assert all(error['testId'] == attempt['testId'] for error in attempt.get('secondaryErrors', []))

report, expected, _, _ = run('earlier-body-error-survives-later-distinct-fixture-error', "const primary=new Error('genuine earlier body'),secondary=new Error('genuine later fixture');export default defineNativeSuite({fixtures:{cleanup:{setup:async(_,use)=>{await use(1);remember('fixture',secondary);throw secondary;}}},hooks:{afterEach:[{run:()=>{}}]},tests:[{title:'body before fixture',fixtures:['cleanup'],run:()=>{remember('body',primary);throw primary;}}]});")
assert not report['globalErrors']
attempt = report['attempts'][0]
assert attempt['error'] == expected['body'] and attempt['status'] == 'failed' and not attempt['expected']
assert any(row['origin'] == 'fixtureRaw' and row['error'] == expected['fixture'] for row in attempt.get('secondaryErrors', []))

report, expected, properties, html = run('distinct-primary-and-secondary-real-step-errors', "const primary=new Error('genuine primary step'),secondary=new Error('genuine afterEach step');export default defineNativeSuite({hooks:{afterEach:[{run:async(_,i)=>{remember('secondary',secondary);await i.step('actual secondary step',()=>{throw secondary;});}}]},tests:[{title:'two real step failures',run:async(_,i)=>{remember('body',primary);await i.step('actual primary step',()=>{throw primary;});}}]});")
assert not report['globalErrors']
attempt = report['attempts'][0]
assert attempt['error'] == expected['body']
primary_step = next(row for row in report['steps'] if row['title'] == 'actual primary step')
secondary_step = next(row for row in report['steps'] if row['title'] == 'actual secondary step')
assert attempt['originatingStepId'] == primary_step['id']
secondary = next(row for row in attempt['secondaryErrors'] if row['error'] == expected['secondary'])
assert secondary['originatingStepId'] == secondary_step['id']
# Matching report wiring must retain secondary diagnostics in each format.
assert any('genuine afterEach step' in value for value in properties.values()) and 'genuine afterEach step' in html

report, expected, _, _ = run('identical-error-object-from-two-actual-phase-callbacks', "const shared=new Error('same original object and identical stack');export default defineNativeSuite({hooks:{afterEach:[{run:()=>{remember('secondary',shared);throw shared;}}]},tests:[{title:'same Error twice',run:()=>{remember('body',shared);throw shared;}}]});")
assert not report['globalErrors'] and expected['body'] == expected['secondary']
attempt = report['attempts'][0]
assert attempt['error'] == expected['body'] and len(attempt.get('secondaryErrors', [])) == 1
assert attempt['secondaryErrors'][0]['error'] == expected['secondary']

report, expected, _, _ = run('genuine-worker-teardown-remains-global', "const actual=new Error('genuine unassociated worker teardown');export default defineNativeSuite({fixtures:{workerCleanup:{scope:'worker',automatic:true,setup:async(_,use)=>{await use(1);remember('worker',actual);throw actual;}}},hooks:{afterEach:[{run:()=>{}}]},tests:[{title:'test passed before worker teardown',run:()=>{}}]});")
assert len(report['attempts']) == 1 and report['attempts'][0]['status'] == 'passed' and report['attempts'][0]['expected']
assert report['attempts'][0]['error'] == '' and not report['attempts'][0].get('secondaryErrors', [])
assert report['globalErrors'] and any(carries(error, expected['worker']) for error in report['globalErrors'])

report, expected, _, _ = run('retry-does-not-borrow-old-attempt-errors', "let retry=-1;const primary=new Error('genuine old retry body'),secondary=new Error('genuine old retry fixture');export default defineNativeSuite({fixtures:{cleanup:{setup:async(_,use)=>{await use(1);if(retry===0){remember('fixture',secondary);throw secondary;}}}},hooks:{afterEach:[{run:()=>{}}]},tests:[{title:'distinct retry owner',fixtures:['cleanup'],retries:1,run:(_,i)=>{retry=i.retry;if(!i.retry){remember('body',primary);throw primary;}}}]});", expected_exit=0)
assert not report['globalErrors'] and len(report['attempts']) == 2 and report['summary']['flaky'] == 1
first, last = report['attempts']
assert first['generation'] != last['generation'] and first['error'] == expected['body'] and not first['expected']
assert any(error['origin'] == 'fixtureRaw' and error['error'] == expected['fixture'] for error in first.get('secondaryErrors', []))
assert last['status'] == 'passed' and last['expected'] and last['error'] == '' and not last.get('secondaryErrors', [])

report, expected, _, _ = run('identical-error-bytes-do-not-hide-genuine-worker-global', "const shared=new Error('identical body and global worker Error');export default defineNativeSuite({fixtures:{workerCleanup:{scope:'worker',automatic:true,setup:async(_,use)=>{await use(1);remember('worker',shared);throw shared;}}},hooks:{afterEach:[{run:()=>{}}]},tests:[{title:'same Error in different actual owners',run:()=>{remember('body',shared);throw shared;}}]});")
assert expected['body'] == expected['worker'] and report['attempts'][0]['error'] == expected['body']
assert report['globalErrors'] and any(carries(error, expected['worker']) for error in report['globalErrors'])

(receipt / 'report.json').write_text(json.dumps({'passed': len(groups),
    'scope': 'Following actual native attempt/phase secondary-error owner attribution, exact original primary Errors and independent worker/global failures. Stock observed parity only for fixture-only attribution.',
    'groups': groups}, indent=2) + '\n')
print('PASS', len(groups), 'actual native attempt error attribution scopes')
