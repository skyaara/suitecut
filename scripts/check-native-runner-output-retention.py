# SPDX-License-Identifier: MIT
"""Actual native assigned-output retention. No browser, author counters or fabricated file receipts."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True, type=pathlib.Path)
parser.add_argument('--sdk-directory', required=True, type=pathlib.Path)
parser.add_argument('--node', required=True)
parser.add_argument('--supervisor', required=True, type=pathlib.Path)
parser.add_argument('--directory', required=True, type=pathlib.Path)
a = parser.parse_args()
receipt = a.directory.resolve()
receipt.mkdir(parents=True, exist_ok=True)
out = pathlib.Path(tempfile.mkdtemp(prefix='cases-', dir=receipt))
sdk = a.sdk_directory.resolve()
groups = []
common = ('import{defineNativeSuite}from ' + json.dumps((sdk / 'native.js').as_uri()) + ';'
    "import{strict as assert}from'node:assert';import{writeFileSync,appendFileSync,mkdirSync,symlinkSync}from'node:fs';")

def absent(pid):
    return not pathlib.Path('/proc', str(pid)).exists()

def run(name, policy, suite_source, code=0, mode='native'):
    root = out / name
    root.mkdir()
    suite = root / 'suite.mjs'
    suite.write_text(common + 'const root=' + json.dumps(str(root)) + ';' + suite_source)
    report = root / 'actual.json'
    options = {'executablePath': str(a.executable.resolve()), 'suitePath': str(suite),
        'reportPath': str(report), 'junitPath': str(root / 'actual.xml'),
        'htmlPath': str(root / 'actual.html'), 'preserveOutput': policy}
    if mode == 'api':
        launch = root / 'launch.mjs'
        launch.write_text('import{runNativeSuite}from ' + json.dumps((sdk / 'native.js').as_uri())
            + ';const result=await runNativeSuite(' + json.dumps(options)
            + ');if(result.signal!==null)throw Error("Unexpected native signal");process.exitCode=result.exitCode??1;')
        child = [a.node, str(launch)]
    elif mode == 'cli':
        child = [a.node, str(sdk / 'native-runner-cli.js'), '--executable', str(a.executable.resolve()),
            '--suite', str(suite), '--report', str(report), '--junit', str(root / 'actual.xml'),
            '--html', str(root / 'actual.html'), '--preserve-output', policy]
    else:
        child = [str(a.executable.resolve()), '--node', a.node, '--host-script', str(sdk / 'native-runner-host.js'),
            '--suite', str(suite), '--report', str(report), '--junit', str(root / 'actual.xml'),
            '--html', str(root / 'actual.html'), '--preserve-output', policy]
    supervised = root / 'supervisor.json'
    command = [str(a.supervisor.resolve()), '--report', str(supervised), '--timeout-ms', '45000',
        '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', *child]
    result = subprocess.run(command, capture_output=True, timeout=60)
    (root / 'stdout.bin').write_bytes(result.stdout)
    (root / 'stderr.bin').write_bytes(result.stderr)
    physical = json.loads(supervised.read_text())
    assert result.returncode == code and not result.stderr, (name, result.returncode, result.stdout, result.stderr)
    assert physical['childExit']['code'] == code and not physical['childExit']['signaled']
    assert physical['descendantsRetired'] and not physical['cleanupFailed'] and not physical['timeout']
    assert absent(physical['supervisorPID']) and absent(physical['childPID'])
    data = json.loads(report.read_text())
    assert not data['globalErrors'], data
    assert data['preserveOutput'] == policy
    original = {row['id']: row for row in data['outputs'] if row['status'] == 'ready' and 'outputDir' in row}
    rows = data['outputRetention']
    assert len(rows) == len(original) == len(data['attempts'])
    seen = set()
    for row in rows:
        assert row['outputId'] not in seen
        seen.add(row['outputId'])
        source = original[row['outputId']]
        attempt = next(item for item in data['attempts'] if item['generation'] == row['generation'])
        assert all(row[key] == source[key] == attempt[key] for key in ['testId', 'generation', 'workerIndex', 'pid'])
        assert row['originalPath'] == source['outputDir'] and row['policy'] == policy
        assert row['attemptStatus'] == attempt['status'] and row['expected'] == attempt['expected']
        assert row['expectedStatus'] == attempt['expectedStatus'] and row['retry'] == attempt['retry']
        keep = policy == 'always' or policy == 'failures-only' and not attempt['expected']
        path = pathlib.Path(row['originalPath'])
        assert path.exists() == keep
        if keep:
            assert row['status'] == 'kept' and row['availability'] == 'notObserved'
            assert 'helperPid' not in row and 'physicalReceiptMonotonicMs' not in row
            assert (path / 'actual.txt').read_text() == attempt['title'] + ' retry' + str(attempt['retry'])
        else:
            assert row['status'] == 'removed' and row['availability'] == 'originalLocationAbsent'
            assert row['helperExitCode'] == 0 and not row['helperSignaled'] and absent(row['helperPid'])
            assert row['physicalReceiptMonotonicMs'] < row['cleanupDeadlineMonotonicMs']
            assert row['observedRemovedDirectories'] >= 1
        assert absent(source['writerPid']) and absent(attempt['pid'])
    for artifact in data['attachments']:
        path = pathlib.Path(artifact['path'])
        actual = path.stat()
        assert actual.st_ino == int(artifact['inode']) and actual.st_dev == int(artifact['device'])
        assert path.read_bytes() == b'actual immutable attachment'
        assert artifact['writerExitConfirmed'] and artifact['writerExitCode'] == 0 and absent(artifact['writerPid'])
    properties = {item.attrib['name']: item.attrib['value'] for item in ET.parse(root / 'actual.xml').findall('.//property')}
    assert properties['native.preserveOutput'] == policy and json.loads(properties['native.outputRetentionJSON']) == rows
    assert 'Native output retention' in (root / 'actual.html').read_text()
    groups.append({'name': name, 'policy': policy, 'actualExitCode': result.returncode,
        'attempts': len(data['attempts']), 'report': str(report), 'supervisor': str(supervised),
        'retentionStatus': [row['status'] for row in rows], 'immutableArtifacts': len(data['attachments']),
        'reportSHA256': hashlib.sha256(report.read_bytes()).hexdigest()})
    return data, root

body = "const write=i=>writeFileSync(i.outputPath('actual.txt'),i.title+' retry'+i.retry);"
for mode in ['native', 'api', 'cli']:
    run('always-real-root-' + mode, 'always', body + "export default defineNativeSuite({tests:[{title:'actual kept root',run:(_,i)=>write(i)}]});", mode=mode)

report, root = run('never-keeps-independent-immutable-artifact', 'never', body + "export default defineNativeSuite({tests:[{title:'actual removed output',run:async(_,i)=>{write(i);mkdirSync(i.outputPath('nested'));writeFileSync(i.outputPath('nested','data'),'actual nested bytes');writeFileSync(root+'/outside','genuine outside bytes');symlinkSync(root+'/outside',i.outputPath('link'));await i.attach('independent',{body:'actual immutable attachment'});}}]});")
assert (root / 'outside').read_text() == 'genuine outside bytes' and len(report['attachments']) == 1

report, root = run('failures-only-native-final-attempt-facts', 'failures-only', body + "export default defineNativeSuite({fixtures:{actualTeardown:{setup:async(_,use)=>{await use(1);throw Error('actual fixture teardown failure')}}},hooks:{afterEach:[{run:(_,i)=>writeFileSync(i.outputPath('actual-afterEach'),'actual cleanup callback')}],afterAll:[{run:()=>writeFileSync(root+'/afterAll','actual afterAll returned')}]},tests:[{title:'expected failure',expectedFailure:true,run:(_,i)=>{write(i);throw Error('actual expected failure')}},{title:'unexpected pass',expectedFailure:true,run:(_,i)=>write(i)},{title:'retry owns each output',retries:1,run:(_,i)=>{write(i);if(!i.retry)throw Error('actual first retry failure')}},{title:'test fixture teardown failure',fixtures:['actualTeardown'],run:(_,i)=>write(i)}]});", code=1)
assert len(report['attempts']) == 5 and (root / 'afterAll').read_text() == 'actual afterAll returned'
expected = {('expected failure', 0): 'removed', ('unexpected pass', 0): 'kept',
    ('retry owns each output', 0): 'kept', ('retry owns each output', 1): 'removed',
    ('test fixture teardown failure', 0): 'kept'}
for row in report['outputRetention']:
    attempt = next(item for item in report['attempts'] if item['generation'] == row['generation'])
    assert row['status'] == expected[(attempt['title'], attempt['retry'])]
    if row['status'] == 'kept':
        assert (pathlib.Path(row['originalPath']) / 'actual-afterEach').read_text() == 'actual cleanup callback'

(receipt / 'report.json').write_text(json.dumps({'passed': len(groups),
    'scope': 'Actual native preserveOutput assigned-output-tree policy only. Independent immutable attachment roots remain retained; no path template/media retention claim.',
    'groups': groups}, indent=2) + '\n')
print('PASS', len(groups), 'actual native output-retention public scopes')
