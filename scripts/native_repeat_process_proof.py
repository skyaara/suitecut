# SPDX-License-Identifier: MIT
"""Import-safe process harness for actual native repeated-group controls."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def reports(doc):
    return [source['report'] for source in doc['sources']] if 'sources' in doc else [doc]


def bodies(events):
    return [event for event in events if event['phase'] == 'body']


def process_identity(pid):
    raw = Path('/proc', str(pid), 'stat').read_text()
    fields = raw[raw.rfind(')') + 2:].split()
    return {'pid': pid, 'state': fields[0], 'ppid': int(fields[1]),
            'start': int(fields[19])}


def physical_receipts(doc, supervisor, workers):
    pids = {supervisor['supervisorPID'], supervisor['childPID']}
    def walk(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key in ('pid', 'writerPid', 'rollbackPid', 'helperPid', 'coordinatorPID') and isinstance(item, int) and item > 0:
                    pids.add(item)
                walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)
    walk(doc)
    directory = doc.get('directory')
    intervals = {}
    if directory is not None:
        budget = directory['nativeBudgetReceipt']
        assert all(budget[key] == 0 for key in ('physicalWorkers', 'physicalIO', 'physicalSteps', 'physicalSoft', 'physicalSources', 'publicationPending')), budget
        for collection in directory['collections']:
            for host in collection['timing']['hosts']:
                intervals[(host['workerIndex'], host['pid'])] = (host['admittedMonotonicMs'], host['waitpidCapturedMonotonicMs'])
        for segment in directory['segments']:
            if not segment['sourceId']:
                continue
            assert segment['nativeRetired']
            source = next(source['report'] for source in doc['sources'] if source['sourceId'] == segment['sourceId'])
            hosts = segment['timing']['hosts']
            assert {(host['workerIndex'], host['pid']) for host in hosts} == {(worker['workerIndex'], worker['pid']) for worker in source['workers']}
            for host in hosts:
                start, end = host['admittedMonotonicMs'], host['waitpidCapturedMonotonicMs']
                assert start <= end
                key = (host['workerIndex'], host['pid'])
                old = intervals.get(key, (start, end))
                intervals[key] = min(old[0], start), max(old[1], end)
    active = peak = 0
    edges = [(start, 1) for start, _ in intervals.values()] + [(end, -1) for _, end in intervals.values()]
    for _, delta in sorted(edges, key=lambda item: (item[0], item[1])):
        active += delta
        peak = max(peak, active)
    assert active == 0 and peak <= workers, (active, peak)
    for source in reports(doc):
        for worker in source['workers']:
            assert worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None, worker
            assert bytes.fromhex(worker['stderr']['data']) == b''
        for output in source.get('outputs', []):
            assert any(all(attempt[key] == output[key] for key in ('testId', 'generation', 'workerIndex', 'pid')) for attempt in source['attempts'])
            assert output['physicalReceiptMonotonicMs'] >= output['writerExitReceiptMonotonicMs']
            if output['status'] == 'ready':
                assert output['readyReceipt'] and not output['writerSignaled'] and output['writerExitCode'] == 0
    assert not [pid for pid in sorted(pids) if Path('/proc', str(pid)).exists()], sorted(pids)
    return sorted(pids), peak if directory is not None else None


def group_mapping(doc, events):
    """Check the native partition and opaque public-ID mapping without parsing IDs."""
    directory = doc['directory']
    slices = {item['context']['slice']: item for item in directory['repeatSelection']['selectedSlices']}
    groups = directory['repeatGroups']
    by_index = {group['index']: group for group in groups}
    assert len(by_index) == len(groups)
    selected = {}
    for group in groups:
        assert isinstance(group['suiteId'], str) and group['mode'] in ('default', 'serial', 'parallel')
        assert len(group['selectedLocalIds']) == len(set(group['selectedLocalIds']))
        selected.setdefault(group['slice'], []).extend(group['selectedLocalIds'])
    for index, selection in slices.items():
        expected = [test['localTestId'] for test in selection['tests']]
        actual = selected.get(index, [])
        assert sorted(actual) == sorted(expected) and len(actual) == len(set(actual)), (actual, expected)
    source_ids = set()
    generations = []
    output_ids = []
    for source in doc['sources']:
        context = source['repeat']
        selection = slices[context['slice']]
        assert context == selection['context']
        compact = source['repeatGroup']
        group = by_index[compact['index']]
        assert compact == {key: group[key] for key in ('index', 'slice', 'mode', 'suiteId')}
        assert compact['slice'] == context['slice']
        segment = next(segment for segment in directory['segments'] if segment['sourceId'] == source['sourceId'])
        assert segment['repeat'] == context and segment['repeatGroup'] == compact and segment['nativeRetired']
        assert segment['selectedLocalIds'] == group['selectedLocalIds']
        mapping = {test['localTestId']: test['logicalId'] for test in selection['tests']}
        for key in ('unrun', 'skipped', 'runtimeSkipped'):
            assert all(row['testId'] in group['selectedLocalIds'] for row in source['report'].get(key, []))
        for step in source['report']['steps']:
            assert step['testId'] in group['selectedLocalIds']
            assert any(all(step[key] == attempt[key] for key in ('testId', 'generation', 'workerIndex', 'pid')) for attempt in source['report']['attempts'])
        for attempt in source['report']['attempts']:
            assert attempt['testId'] in group['selectedLocalIds']
            matching = [event for event in bodies(events) if event['pid'] == attempt['pid'] and event['retry'] == attempt['retry'] and event['testId'] == mapping[attempt['testId']]]
            assert len(matching) == 1 and matching[0]['repeatEachIndex'] == context['repeatEachIndex'], attempt
            generations.append(attempt['generation'])
        output_ids.extend(output['id'] for output in source['report'].get('outputs', []))
        source_ids.add(source['sourceId'])
    assert len(generations) == len(set(generations)) and len(output_ids) == len(set(output_ids))
    selected_ids = {test['logicalId'] for selection in slices.values() for test in selection['tests']}
    assert len(selected_ids) == sum(len(selection['tests']) for selection in slices.values())
    for row in doc['tests']:
        selection = slices[row['repeat']['slice']]
        assert row['logicalId'] == next(test['logicalId'] for test in selection['tests'] if test['localTestId'] == row['testId'])
        compact = row['repeatGroup']
        group = by_index[compact['index']]
        assert compact == {key: group[key] for key in ('index', 'slice', 'mode', 'suiteId')}
        assert row['testId'] in group['selectedLocalIds'] and compact['slice'] == row['repeat']['slice']
        if row.get('nativeSelectionOnly'):
            assert row['sourceId'] is None and row['outcome'] == 'didNotRun' and row['attemptIndices'] == []
        else:
            assert row['sourceId'] in source_ids
            source = next(source for source in doc['sources'] if source['sourceId'] == row['sourceId'])
            assert source['repeatGroup'] == compact and source['repeat'] == row['repeat']
    assert {row['logicalId'] for row in doc['tests']} == selected_ids


class NativeRepeatProof:
    def __init__(self, args, checker):
        self.args = args
        args.directory.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix='repeat-groups-', dir=args.directory.resolve()))
        self.groups = []
        self.pins = {str(path.resolve()): digest(path) for path in [Path(checker), Path(__file__), args.executable, args.node, args.supervisor, *sorted(args.sdk_directory.rglob('*'))] if path.is_file()}

    def save(self):
        changed = [path for path, value in self.pins.items() if not Path(path).is_file() or digest(path) != value]
        (self.root / 'index.json').write_text(json.dumps({'scope': 'Actual native repeated groups; host occupancy is observed through waitpid, final budgets prove retirement.', 'inputs': self.pins, 'changed': changed, 'groups': self.groups}, indent=2) + '\n')
        assert not changed, changed

    def run(self, name, files, *, options=(), code=0, workers=2, release_after=0, direct=False):
        case = self.root / name
        tests = case / 'tests'
        tests.mkdir(parents=True)
        events_path = case / 'events.jsonl'
        api = (self.args.sdk_directory.resolve() / 'native-suite.js').as_uri()
        prelude = '''import assert from 'node:assert/strict';
import {appendFileSync,writeFileSync,readFileSync,existsSync} from 'node:fs';
import {defineNativeSuite} from API;
const control=CONTROL;
const mark=(phase,info,extra={})=>appendFileSync(EVENTS,JSON.stringify({phase,pid:process.pid,testId:info?.testId,repeatEachIndex:info?.repeatEachIndex,retry:info?.retry,workerIndex:info?.workerIndex,project:info?.project?.name,declaredRepeatEach:info?.project?.repeatEach,...extra})+'\\n');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const waitFor=async fn=>{while(!fn())await delay(5)};
const aborted=info=>new Promise(resolve=>{if(info.signal.aborted)resolve();else info.signal.addEventListener('abort',resolve,{once:true})});
mark('import');
'''.replace('API', json.dumps(api)).replace('CONTROL', json.dumps(str(case))).replace('EVENTS', json.dumps(str(events_path)))
        for filename, source in files.items():
            (tests / filename).write_text(prelude + source + '\n')
        row = {'name': name, 'fixtures': {str(tests / filename): digest(tests / filename) for filename in files}}
        self.groups.append(row)
        if self.args.prepare_only:
            row['preparedOnly'] = True
            self.save()
            return None, [], case, row
        mode = ['--suite', str(tests / next(iter(files)))] if direct else ['--test-dir', str(tests)]
        command = [str(self.args.supervisor), '--report', str(case / 'supervisor.json'), '--timeout-ms', '45000', '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(self.args.node), str(self.args.sdk_directory / 'native-runner-cli.js'), '--executable', str(self.args.executable), *mode, '--report', str(case / 'actual.json'), '--workers', str(workers), *options]
        row['command'] = command
        observed = []
        observer_error = None
        with (case / 'stdout').open('wb') as out, (case / 'stderr').open('wb') as err:
            process = subprocess.Popen(command, stdout=out, stderr=err)
            if release_after:
                try:
                    end = time.monotonic() + 15
                    while process.poll() is None and time.monotonic() < end:
                        events = self.read_events(events_path)
                        entered = [event for event in bodies(events) if event.get('held')]
                        if len(entered) >= release_after:
                            admitted = entered[:release_after]
                            identities = [process_identity(event['pid']) for event in admitted]
                            assert len({item['pid'] for item in identities}) == release_after
                            assert all(item['state'] != 'Z' and Path('/proc', str(item['ppid']), 'exe').resolve() == self.args.executable.resolve() for item in identities), identities
                            observed = identities
                            break
                        time.sleep(0.005)
                    assert len(observed) == release_after, 'Required actual simultaneous native body admission not observed'
                except BaseException as error:
                    observer_error = repr(error)
                finally:
                    # Always release and wait the actual supervisor before asserting.
                    (case / 'release').write_text('actual external observer release\n')
            actual = process.wait()
        supervisor = json.loads((case / 'supervisor.json').read_text())
        row.update(actualExit=actual, supervisor=supervisor, observedHeldBodyIdentities=observed, observerError=observer_error, report=str(case / 'actual.json'))
        self.save()
        assert actual == code and supervisor['childExit'] == {'signaled': False, 'code': code, 'rawWaitpidStatus': code << 8}, row
        assert supervisor['descendantsRetired'] and not supervisor['cleanupFailed'] and not supervisor['timeout'], row
        assert not (case / 'stderr').read_bytes()
        assert all(digest(path) == value for path, value in row['fixtures'].items())
        doc = json.loads((case / 'actual.json').read_text())
        assert not events_path.exists() or events_path.read_bytes().endswith(b'\n'), 'Final callback beacon must be complete'
        events = self.read_events(events_path)
        row['actualPIDs'], row['peakObservedHosts'] = physical_receipts(doc, supervisor, workers)
        self.save()
        assert observer_error is None, observer_error
        if 'directory' in doc:
            group_mapping(doc, events)
        self.save()
        return doc, events, case, row

    @staticmethod
    def read_events(path):
        if not path.exists():
            return []
        # Ignore only the last in-flight partial write during observation.
        data = path.read_bytes()
        return [json.loads(line) for line in data.split(b'\n')[:-1] if line]
