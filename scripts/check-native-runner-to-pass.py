#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Genuine callback/step/file/host evidence for native-owned toPass.

The author may change a condition; only native RUN chooses another invocation.
This driver never supplies a raw or physical settlement packet.
"""
import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from native_repeat_process_proof import digest, process_identity
from native_to_pass_fixtures import PRELUDE, SCOPES

OWNER = ('testId', 'generation', 'callbackToken', 'scopeId', 'scopeGeneration', 'workerIndex', 'pid', 'callbackKind')


def events_at(path):
    if not path.exists():
        return []
    # Observation may see the final in-flight write. Final verification requires
    # the terminating newline separately after the real supervisor has exited.
    return [json.loads(row) for row in path.read_bytes().split(b'\n')[:-1] if row]


def check_rows(doc):
    rows = doc['callbackAssertions']
    assert rows and len({row['id'] for row in rows}) == len(rows)
    steps = doc['steps']
    owned = set()
    for row in rows:
        for key in ('id', 'requestId', 'testId', 'generation', 'callbackToken', 'scopeId', 'scopeGeneration', 'parentStepId'):
            assert isinstance(row[key], str) and row[key].isdigit(), (key, row)
        assert int(row['id']) > 0 and int(row['requestId']) > 0
        assert row['callbackKind'] in ('body', 'beforeEach', 'afterEach')
        assert any(all(attempt[key] == row[key] for key in ('testId', 'generation', 'workerIndex', 'pid')) for attempt in doc['attempts'])
        assert any(worker['workerIndex'] == row['workerIndex'] and worker['pid'] == row['pid'] for worker in doc['workers'])
        assert row['status'] in ('passed', 'timedOut', 'canceled', 'infrastructure') and row['receipt'] in ('settled', 'workerExited')
        assert isinstance(row['error'], str) and isinstance(row['attempts'], int) and row['attempts'] > 0
        for key in ('startedMonotonicMs', 'deadlineMonotonicMs', 'receiptMonotonicMs'):
            assert isinstance(row[key], (float, int)) and math.isfinite(row[key])
        assert row['startedMonotonicMs'] <= row['receiptMonotonicMs']
        if 'ownDeadlineMonotonicMs' in row:
            assert row['ownDeadlineMonotonicMs'] >= row['startedMonotonicMs']
            assert row['deadlineMonotonicMs'] <= row['ownDeadlineMonotonicMs']
        iterations = [step for step in steps if step.get('callbackAssertion', {}).get('id') == row['id']]
        assert len(iterations) == row['attempts']
        iterations.sort(key=lambda step: int(step['callbackAssertion']['invocation']))
        assert iterations[0]['id'] == row['id'] and iterations[-1]['callbackAssertion']['invocation'] == row['lastInvocation']
        nonces = [int(step['callbackAssertion']['invocation']) for step in iterations]
        assert all(nonce > 0 for nonce in nonces) and nonces == sorted(set(nonces))
        for step in iterations:
            assert all(step[key] == row[key] for key in OWNER) and step['parentId'] == row['parentStepId']
            assert step['id'] not in owned
            owned.add(step['id'])
            assert step['startedMonotonicMs'] <= step['receiptMonotonicMs'] <= row['receiptMonotonicMs']
            assert step['receipt'] in ('settled', 'workerExited')
            if step['receipt'] == 'settled':
                assert 'callbackReceiptMonotonicMs' in step
                assert step['startedMonotonicMs'] <= step['callbackReceiptMonotonicMs'] <= step['receiptMonotonicMs']
            else:
                assert row['receipt'] == 'workerExited' and row['status'] != 'passed'
        for previous, current in zip(iterations, iterations[1:]):
            assert previous['receipt'] == 'settled'
            assert current['startedMonotonicMs'] >= previous['receiptMonotonicMs']
        if row['parentStepId'] != '0':
            assert any(step['id'] == row['parentStepId'] and all(step[key] == row[key] for key in OWNER) for step in steps)
        if row['status'] == 'passed':
            assert row['receipt'] == 'settled' and iterations[-1]['status'] == 'passed'
    assert owned == {step['id'] for step in steps if 'callbackAssertion' in step}
    return rows


def bodies(events):
    return [event for event in events if event['phase'] == 'body']


def body_row(doc, event):
    attempt = next(attempt for attempt in doc['attempts'] if attempt['pid'] == event['pid'] and attempt['retry'] == event['retry'] and attempt['testId'] == event['testId'] and attempt['workerIndex'] == event['workerIndex'])
    rows = [row for row in doc['callbackAssertions'] if row['generation'] == attempt['generation'] and row['callbackKind'] == 'body']
    assert len(rows) == 1, rows
    return rows[0]


def check_scope(name, doc, events, case):
    rows = check_rows(doc)
    assert all(event['phase'] != 'MUST_NOT_RUN' for event in events)
    if name == 'callbacks-hooks-retry-and-stale':
        assert doc['summary']['expected'] == 3 and doc['summary']['skipped'] == 1 and doc['summary']['flaky'] == 1 and doc['summary']['unexpected'] == 0
        assert [(attempt['retry'], attempt['status']) for attempt in doc['attempts']] == [(0, 'failed'), (1, 'passed'), (0, 'passed'), (0, 'passed'), (0, 'skipped')]
        assert doc['attempts'][0]['pid'] != doc['attempts'][1]['pid']
        for event in bodies(events):
            row = body_row(doc, event)
            label = event['label']
            assert row['status'] == 'passed'
            assert row['attempts'] == {'retry': 4, 'saved': 1, 'attachment': 2, 'append-skip': 2}[label]
            invocation_label = 'body' if label == 'retry' else 'saved-later-owner' if label == 'saved' else label
            actual_callbacks = [beacon for beacon in events if beacon['phase'] == 'invocation' and beacon.get('label') == invocation_label and all(beacon[key] == event[key] for key in ('testId', 'pid', 'retry', 'workerIndex'))]
            assert len(actual_callbacks) == row['attempts']
            if label == 'retry':
                parent = next(step for step in doc['steps'] if step['id'] == row['parentStepId'])
                assert parent['title'] == 'authored inner'
                iterations = sorted([step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == row['id']], key=lambda step: int(step['callbackAssertion']['invocation']))
                for index, (previous, current) in enumerate(zip(iterations, iterations[1:])):
                    assert current['startedMonotonicMs'] >= previous['receiptMonotonicMs'] + (10 if index == 0 else 20)
        assert all(row['attempts'] == 2 and row['status'] == 'passed' for row in rows if row['callbackKind'] != 'body')
        assert sum(event['phase'] == 'stale-rejected' for event in events) == 1
        attachment_event = next(event for event in bodies(events) if event['label'] == 'attachment')
        attachment_row = body_row(doc, attachment_event)
        iterations = sorted([step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == attachment_row['id']], key=lambda step: int(step['callbackAssertion']['invocation']))
        nested_event = next(event for event in events if event['phase'] == 'nested-attachment-raw')
        nested = next(step for step in doc['steps'] if step['id'] == nested_event['stepId'])
        assert nested['parentId'] == iterations[0]['id'] and all(nested[key] == attachment_row[key] for key in OWNER)
        assert 'callbackAssertion' not in nested and nested['status'] == 'failed' and nested['callbackReceiptOK'] and nested['receiptOK'] and nested['receipt'] == 'settled' and nested['attachmentError']
        assert iterations[0]['status'] == 'failed' and nested['callbackReceiptMonotonicMs'] <= nested['receiptMonotonicMs'] <= iterations[0]['receiptMonotonicMs'] <= iterations[1]['startedMonotonicMs']
        assert not any(record['name'] == 'actual nested missing input' for record in doc['attachments'])
        # Plain stepInfo.attach failure has genuine failed-step/file-fence
        # history; it never invents an append declaration or committed artifact.
        committed = {record['name']: record for record in doc['attachments'] if record.get('callbackAssertion', {}).get('id') == attachment_row['id']}
        assert set(committed) == {'actual test-owned file', 'actual captured-step file'}
        assert 'stepId' not in committed['actual test-owned file']
        captured = next(step for step in doc['steps'] if step['title'] == 'public captured attachment owner')
        assert captured['id'] == attachment_row['parentStepId'] and captured['status'] == 'passed'
        assert committed['actual captured-step file']['stepId'] == captured['id'] and 'callbackAssertion' not in captured
        for label, value in [('actual test-owned file', b'ACTUAL_TEST_OWNED_BYTES'), ('actual captured-step file', b'ACTUAL_CAPTURED_STEP_BYTES')]:
            record = committed[label]
            path = Path(record['path']);stat = path.stat()
            assert path.read_bytes() == value and stat.st_dev == int(record['device']) and stat.st_ino == int(record['inode'])
            assert record['callbackAssertion'] == {'id': attachment_row['id'], 'invocation': '2', 'stepId': iterations[1]['id']}
        append_event = next(event for event in bodies(events) if event['label'] == 'append-skip')
        append_row = body_row(doc, append_event)
        append_iterations = sorted([step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == append_row['id']], key=lambda step: int(step['callbackAssertion']['invocation']))
        provenance = {'id': append_row['id'], 'invocation': '1', 'stepId': append_iterations[0]['id']}
        declarations = [record for record in doc['attachmentDeclarations'] if record.get('callbackAssertion', {}).get('id') == append_row['id']]
        assert len(declarations) == 5 and all(record['callbackAssertion'] == provenance for record in declarations)
        assert {record['name'] for record in declarations if record['receipt'] == 'committed'} == {f'append-slot-{index}' for index in range(4)}
        rejected = [record for record in declarations if record['receipt'] == 'failed']
        assert len(rejected) == 1 and rejected[0]['name'] == 'append-slot-4' and rejected[0]['error']
        assert append_iterations[0]['status'] == 'failed' and append_iterations[0]['receiptMonotonicMs'] <= append_iterations[1]['startedMonotonicMs']
        append_artifacts = [record for record in doc['attachments'] if record.get('callbackAssertion', {}).get('id') == append_row['id']]
        assert len(append_artifacts) == 4 and all(record['callbackAssertion'] == provenance for record in append_artifacts)
        for record in append_artifacts:
            path = Path(record['path']);stat = path.stat()
            assert path.read_bytes() == b'x' and stat.st_dev == int(record['device']) and stat.st_ino == int(record['inode'])
        skip_event = next(event for event in events if event['phase'] == 'local-skip-enter')
        local = next(step for step in doc['steps'] if step['id'] == skip_event['stepId'])
        assert local['status'] == 'skipped' and local['stepSkip']['requestId'] == local['receiptStepSkipRequestId'] and local['receipt'] == 'settled'
        assert any(event['phase'] == 'after-local-skip' for event in events)
        assert doc['attempts'][-1]['expected'] and doc['attempts'][-1]['skipReason'] == 'actual test skip after recovery'
    elif name == 'fixed-explicit-deadline':
        assert len(rows) == 1 and rows[0]['status'] == 'timedOut' and rows[0]['attempts'] == 1
        assert abs(rows[0]['ownDeadlineMonotonicMs'] - rows[0]['startedMonotonicMs'] - 120) < 0.02
        assert doc['attempts'][0]['expected'] and doc['summary']['flaky'] == 0
        assert next(event for event in events if event['phase'] == 'fixed-caught')['sameObject']
    elif name == 'raw-held-logical-timeout':
        assert len(rows) == 1 and rows[0]['status'] == 'timedOut' and rows[0]['receipt'] == 'settled'
        step = next(step for step in doc['steps'] if step.get('callbackAssertion', {}).get('id') == rows[0]['id'])
        assert step['callbackReceiptMonotonicMs'] > rows[0]['ownDeadlineMonotonicMs']
        assert next(event for event in events if event['phase'] == 'held-caught')['active']
        phases = [event['phase'] for event in events]
        assert phases.index('held-caught') < phases.index('raw-settled') < phases.index('after-raw-fence')
        assert phases.index('raw-settled') < next(index for index, event in enumerate(events) if event.get('label') == 'after-held')
        assert doc['summary']['unexpected'] == doc['summary']['flaky'] == 0
    elif name == 'original-author-budget-default-and-zero':
        assert len(rows) == 2 and all('ownDeadlineMonotonicMs' not in row for row in rows)
        assert all(attempt['status'] == 'timedOut' and not attempt['expected'] for attempt in doc['attempts'])
        assert doc['summary']['flaky'] == 0 and doc['summary']['unexpected'] == 2
        assert len([event for event in events if event['phase'] == 'raw-settled']) == 2
    elif name == 'original-global-budget-and-cleanup':
        assert all('ownDeadlineMonotonicMs' not in row for row in rows)
        assert doc['attempts'][0]['status'] == 'interrupted' and not doc['attempts'][0]['expected']
        assert doc['globalErrors'] == ['Native suite original global timeout of 1000ms exceeded']
        phases = [event['phase'] for event in events]
        assert phases.index('raw-settled') < phases.index('after-raw-fence') < phases.index('cleanup-done')
        assert any(row['callbackKind'] == 'afterEach' and row['status'] == 'passed' for row in rows)
    else:
        assert len(rows) == 1 and rows[0]['receipt'] == 'workerExited' and rows[0]['status'] != 'passed'
        assert not doc['attempts'][0]['expected'] and doc['summary']['unexpected'] == 1
        assert all('callbackReceiptMonotonicMs' not in step for step in doc['steps'] if 'callbackAssertion' in step)
        if name == 'never-raw-actual-worker-retirement':
            # A genuine exhausted physical cleanup is an independent native
            # infrastructure failure, even though the author caught RESULT.
            assert doc['globalErrors']
    if name not in ('original-global-budget-and-cleanup', 'never-raw-actual-worker-retirement'):
        assert doc['globalErrors'] == []


def run(args):
    args.directory.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='to-pass-', dir=args.directory.resolve()))
    sdk = args.sdk_directory.resolve()
    node = Path(shutil.which(args.node) or args.node).resolve()
    inputs = [Path(__file__).resolve(), Path(__file__).with_name('native_to_pass_fixtures.py'), Path(__file__).with_name('native_repeat_process_proof.py'), node]
    if not args.prepare_only:
        inputs += [args.executable.resolve(), args.supervisor.resolve()]
    inputs += [path for path in sdk.rglob('*') if path.is_file()]
    before = {str(path.resolve()): digest(path) for path in sorted(set(inputs))}
    (root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
    results = []
    try:
        for name, specification in SCOPES.items():
            if args.case and name not in args.case:
                continue
            case = root / name;case.mkdir()
            suite = case / 'suite.mjs';event_file = case / 'events.jsonl'
            source = PRELUDE.replace('API;', json.dumps((sdk / 'native.js').as_uri()) + ';').replace('EVENTS', json.dumps(str(event_file))) + specification['body'].replace('MISSING', json.dumps(str(case / 'genuinely-absent-input'))).replace('RELEASE', json.dumps(str(case / 'release')))
            suite.write_text(source + '\n')
            item = {'name': name, 'suite': str(suite), 'suiteSHA256': digest(suite)};results.append(item)
            if args.prepare_only:
                item['preparedOnly'] = True
                continue
            command = [str(args.supervisor.resolve()), '--report', str(case / 'supervisor.json'), '--timeout-ms', '45000', '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(args.executable.resolve()), '--node', str(node), '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(case / 'actual.json'), '--workers', '1', *specification.get('options', [])]
            item['command'] = command
            with (case / 'stdout').open('wb') as out, (case / 'stderr').open('wb') as err:
                child = subprocess.Popen(command, stdout=out, stderr=err)
                observer_error = None
                if specification.get('release'):
                    try:
                        until = time.monotonic() + 15
                        while child.poll() is None and time.monotonic() < until:
                            marker = next((event for event in events_at(event_file) if event['phase'] == specification['release']), None)
                            if marker:
                                identity = process_identity(marker['pid'])
                                assert marker['active'] and identity['state'] != 'Z'
                                assert Path('/proc', str(identity['ppid']), 'exe').resolve() == args.executable.resolve()
                                item['actualHeldWorkerIdentity'] = identity
                                break
                            time.sleep(0.005)
                        assert 'actualHeldWorkerIdentity' in item, 'Actual raw-held logical rejection not observed'
                    except BaseException as error:
                        observer_error = repr(error)
                    finally:
                        # Always allow the real callback/supervisor to retire,
                        # then reject observation failure without leaked jobs.
                        (case / 'release').write_text('Actual external observer releases raw callback\n')
                exit_code = child.wait()
            receipt = json.loads((case / 'supervisor.json').read_text())
            item.update(actualExit=exit_code, supervisor=receipt, observerError=observer_error)
            assert observer_error is None, item
            assert exit_code == specification['exit'] and receipt['childExit'] == {'signaled': False, 'code': exit_code, 'rawWaitpidStatus': exit_code << 8}, item
            assert receipt['descendantsRetired'] and not receipt['cleanupFailed'] and not receipt['timeout'] and not (case / 'stderr').read_bytes(), item
            doc = json.loads((case / 'actual.json').read_text())
            pids = {receipt['supervisorPID'], receipt['childPID']}
            def collect(value):
                if isinstance(value, dict):
                    for key, part in value.items():
                        if key in ('pid', 'writerPid', 'rollbackPid', 'helperPid', 'coordinatorPID') and isinstance(part, int) and part > 0:
                            pids.add(part)
                        collect(part)
                elif isinstance(value, list):
                    for part in value:
                        collect(part)
            collect(doc)
            item['actualPIDs'] = sorted(pids)
            assert not [pid for pid in pids if Path('/proc', str(pid)).exists()]
            assert all(worker['exitStatusKnown'] and worker['stderr']['byteLength'] == 0 for worker in doc['workers'])
            if 'signal' in specification:
                assert any(worker['signal'] == specification['signal'] for worker in doc['workers'])
            elif 'hostExit' in specification:
                assert any(worker['exitCode'] == specification['hostExit'] and worker['signal'] is None for worker in doc['workers'])
            else:
                assert all(worker['signal'] is None and worker['exitCode'] == 0 for worker in doc['workers'])
            assert not event_file.exists() or event_file.read_bytes().endswith(b'\n')
            events = events_at(event_file)
            assert digest(suite) == item['suiteSHA256']
            check_scope(name, doc, events, case)
            item['report'] = str(case / 'actual.json')
    finally:
        after = {path: digest(path) for path in before}
        (root / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
        (root / 'index.json').write_text(json.dumps({'inputs': before, 'changed': [path for path in before if before[path] != after[path]], 'scopes': results, 'scope': 'Source prepared only' if args.prepare_only else 'Actual native callback/step/file/host evidence; no manufactured settlements'}, indent=2) + '\n')
        assert before == after
    print('Prepared' if args.prepare_only else 'PASS', len(results), 'native toPass scopes', root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--sdk-directory', type=Path, required=True)
    parser.add_argument('--supervisor', type=Path, required=True)
    parser.add_argument('--node', default='node')
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--case', action='append')
    args = parser.parse_args()
    assert not args.case or len(args.case) == len(set(args.case)) and all(name in SCOPES for name in args.case), 'Unknown/duplicate scope'
    run(args)
