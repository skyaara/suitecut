# SPDX-License-Identifier: MIT
"""Genuine bounded repeatEach callbacks, native planning and physical receipts.

No browser, callback simulation, JavaScript scheduler or huge numeric probes.
The emitted SDK and native activation must match before this checker is run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
for field in ('executable', 'sdk-directory', 'node', 'supervisor', 'directory'):
    parser.add_argument('--' + field, type=Path, required=True)
parser.add_argument('--prepare-only', action='store_true')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='repeat-', dir=args.directory.resolve()))
api = (args.sdk_directory.resolve() / 'native-suite.js').as_uri()
groups = []


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


pins = {str(p.resolve()): digest(p) for p in [Path(__file__), args.executable,
        args.node, args.supervisor, *sorted(args.sdk_directory.rglob('*'))] if p.is_file()}


def save():
    changed = [p for p, value in pins.items() if digest(p) != value]
    (root / 'index.json').write_text(json.dumps({'scope': 'Bounded genuine repeatEach process controls; no browser/numeric stress.',
        'inputs': pins, 'changed': changed, 'groups': groups}, indent=2) + '\n')
    assert not changed, changed


def reports(doc):
    return [s['report'] for s in doc['sources']] if 'sources' in doc else [doc]


def attempts(doc):
    return [t for source in reports(doc) for t in source['attempts']]


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
    host_intervals = {}
    if 'directory' in doc:
        directory = doc['directory']
        budget = directory['nativeBudgetReceipt']
        assert all(budget[k] == 0 for k in ('physicalWorkers', 'physicalIO', 'physicalSteps', 'physicalSoft', 'physicalSources', 'publicationPending'))
        for collection in directory['collections']:
            for host in collection['timing']['hosts']:
                key = (host['workerIndex'], host['pid'])
                host_intervals[key] = (host['admittedMonotonicMs'], host['waitpidCapturedMonotonicMs'])
        for segment in directory['segments']:
            if not segment['sourceId']:
                continue
            source = next(s['report'] for s in doc['sources'] if s['sourceId'] == segment['sourceId'])
            hosts = segment['timing']['hosts']
            assert {(h['workerIndex'], h['pid']) for h in hosts} == {(w['workerIndex'], w['pid']) for w in source['workers']}
            for host in hosts:
                assert host['admittedMonotonicMs'] <= host['waitpidCapturedMonotonicMs']
                key = (host['workerIndex'], host['pid'])
                old = host_intervals.get(key, (host['admittedMonotonicMs'], host['waitpidCapturedMonotonicMs']))
                host_intervals[key] = (min(old[0], host['admittedMonotonicMs']), max(old[1], host['waitpidCapturedMonotonicMs']))
    intervals = [(start, 1) for start, _ in host_intervals.values()] + [(end, -1) for _, end in host_intervals.values()]
    active = peak = 0
    for _, delta in sorted(intervals, key=lambda item: (item[0], item[1])):
        active += delta
        peak = max(peak, active)
    assert active == 0 and peak <= workers, (active, peak)
    for source in reports(doc):
        for worker in source['workers']:
            assert worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None, worker
            assert bytes.fromhex(worker['stderr']['data']) == b''
        for output in source.get('outputs', []):
            assert any(all(t[k] == output[k] for k in ('testId', 'generation', 'workerIndex', 'pid')) for t in source['attempts'])
            assert output['physicalReceiptMonotonicMs'] >= output['writerExitReceiptMonotonicMs']
            if output['status'] == 'ready':
                assert output['readyReceipt'] and not output['writerSignaled'] and output['writerExitCode'] == 0
    assert not [pid for pid in sorted(pids) if Path('/proc', str(pid)).exists()], pids
    return sorted(pids), peak


def repeat_mapping(doc, events):
    """Compare opaque IDs, never derive native authority from their spelling."""
    directory = doc['directory']
    selected = directory['repeatSelection']['selectedSlices']
    ids = [t['logicalId'] for sl in selected for t in sl['tests']]
    assert len(ids) == len(set(ids)), ids
    groups = directory.get('repeatGroups')
    group_by_index = None
    if groups is not None:
        group_by_index = {group['index']: group for group in groups}
        assert len(group_by_index) == len(groups)
        assert all(group['slice'] in {sl['context']['slice'] for sl in selected} for group in groups)
        for sl in selected:
            members = [local_id for group in groups if group['slice'] == sl['context']['slice'] for local_id in group['selectedLocalIds']]
            assert len(members) == len(set(members))
            assert set(members) == {test['localTestId'] for test in sl['tests']}
    source_ids = set()
    for source in doc['sources']:
        context = source['repeat']
        selected_slice = next(s for s in selected if s['context'] == context)
        owner = next(s for s in directory['segments'] if s['sourceId'] == source['sourceId'])
        assert owner['repeat'] == context and owner['nativeRetired']
        admitted_ids = {t['localTestId'] for t in selected_slice['tests']}
        if group_by_index is not None:
            compact = source['repeatGroup']
            group = group_by_index[compact['index']]
            assert compact == {key: group[key] for key in ('index', 'slice', 'mode', 'suiteId')}
            assert compact['slice'] == context['slice'] and owner['repeatGroup'] == compact
            admitted_ids = set(group['selectedLocalIds'])
            assert len(admitted_ids) == len(group['selectedLocalIds'])
        assert set(owner['selectedLocalIds']) == admitted_ids
        assert len(owner['selectedLocalIds']) == len(admitted_ids)
        for attempt in source['report']['attempts']:
            assert attempt['testId'] in admitted_ids
            match = next(t for t in selected_slice['tests'] if t['localTestId'] == attempt['testId'])
            body = [e for e in events if e['phase'] == 'body' and e['pid'] == attempt['pid'] and e['retry'] == attempt['retry'] and e['testId'] == match['logicalId']]
            assert len(body) == 1 and body[0]['testId'] == match['logicalId']
            assert body[0]['repeatEachIndex'] == context['repeatEachIndex']
            source_ids.add(match['logicalId'])
    generations = [t['generation'] for source in doc['sources'] for t in source['report']['attempts']]
    assert len(generations) == len(set(generations)), generations
    output_ids = [o['id'] for source in doc['sources'] for o in source['report'].get('outputs', [])]
    assert len(output_ids) == len(set(output_ids)), output_ids
    for row in doc['tests']:
        assert row['logicalId'] in ids
        selected_slice = next(s for s in selected if s['context'] == row['repeat'])
        assert next(t['logicalId'] for t in selected_slice['tests'] if t['localTestId'] == row['testId']) == row['logicalId']
        if group_by_index is not None:
            compact = row['repeatGroup']
            group = group_by_index[compact['index']]
            assert compact == {key: group[key] for key in ('index', 'slice', 'mode', 'suiteId')}
            assert compact['slice'] == row['repeat']['slice'] and row['testId'] in group['selectedLocalIds']
            if not row.get('nativeSelectionOnly'):
                assert next(source['repeatGroup'] for source in doc['sources'] if source['sourceId'] == row['sourceId']) == compact
        if row.get('nativeSelectionOnly'):
            assert row['sourceId'] is None and row['outcome'] == 'didNotRun' and row['attemptIndices'] == []
        else:
            assert next(source['repeat'] for source in doc['sources'] if source['sourceId'] == row['sourceId']) == row['repeat']
    return source_ids


def run(name, files, *, direct=False, options=(), code=0, workers=2, require_repeat=True):
    case = root / name
    tests = case / 'tests'
    tests.mkdir(parents=True)
    events_path = case / 'events.jsonl'
    prelude = '''import assert from 'node:assert/strict';
import {appendFileSync,writeFileSync,readFileSync,existsSync,readdirSync,readlinkSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {defineNativeSuite} from API;
const control=CONTROL;
const mark=(phase,info,extra={})=>appendFileSync(EVENTS,JSON.stringify({phase,pid:process.pid,testId:info?.testId,repeatEachIndex:info?.repeatEachIndex,retry:info?.retry,workerIndex:info?.workerIndex,project:info?.project?.name,...extra})+'\\n');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const waitFor=async fn=>{while(!fn())await delay(5)};
const aborted=info=>new Promise(resolve=>{if(info.signal.aborted)resolve();else info.signal.addEventListener('abort',resolve,{once:true})});
mark('import');
'''.replace('API', json.dumps(api)).replace('CONTROL', json.dumps(str(case))).replace('EVENTS', json.dumps(str(events_path)))
    for filename, source in files.items():
        (tests / filename).write_text(prelude + source + '\n')
    if args.prepare_only:
        for filename in files:
            # Syntax only; --check neither imports the SDK nor invokes a callback.
            result = subprocess.run([str(args.node), '--check', str(tests / filename)], capture_output=True, timeout=10)
            assert result.returncode == 0 and not result.stderr, result.stderr
        groups.append({'name': name, 'preparedOnly': True, 'fixtures': [str(tests / f) for f in files]})
        save()
        return None, [], case, None
    mode = ['--suite', str(tests / next(iter(files)))] if direct else ['--test-dir', str(tests)]
    command = [str(args.supervisor), '--report', str(case / 'supervisor.json'), '--timeout-ms', '45000',
        '--cleanup-grace-ms', '1000', '--physical-ms', '10000', '--', str(args.node),
        str(args.sdk_directory / 'native-runner-cli.js'), '--executable', str(args.executable),
        *mode, '--report', str(case / 'actual.json'), '--workers', str(workers), *options]
    with (case / 'stdout').open('wb') as out, (case / 'stderr').open('wb') as err:
        actual = subprocess.run(command, stdout=out, stderr=err)
    supervisor = json.loads((case / 'supervisor.json').read_text())
    row = {'name': name, 'command': command, 'actualExit': actual.returncode, 'supervisor': supervisor, 'report': str(case / 'actual.json')}
    groups.append(row)
    save()  # Preserve the first negative before checking its assertions.
    assert actual.returncode == code and supervisor['childExit'] == {'signaled': False, 'code': code, 'rawWaitpidStatus': code << 8}, row
    assert supervisor['descendantsRetired'] and not supervisor['cleanupFailed'] and not supervisor['timeout'], row
    assert (case / 'stderr').read_bytes() == b''
    doc = json.loads((case / 'actual.json').read_text())
    events = [json.loads(line) for line in events_path.read_text().splitlines()]
    row['actualPIDs'], row['peakObservedHosts'] = physical_receipts(doc, supervisor, workers)
    if 'directory' in doc:
        if require_repeat:
            row['observedLogicalIDs'] = sorted(repeat_mapping(doc, events))
        else:
            assert 'repeatSelection' not in doc['directory']
    save()
    return doc, events, case, row


def single(body, **config):
    return 'export default defineNativeSuite({' + ''.join(k + ':' + json.dumps(v) + ',' for k, v in config.items()) + 'tests:[{title:"same actual title",run:async(_,info)=>{' + body + '}}]});'


def body_events(events):
    return [e for e in events if e['phase'] == 'body']


try:
    once = 'assert.equal(info.repeatEachIndex,0);assert.equal(info.testId,"1");mark("body",info);'
    for label, options in [('omitted', []), ('explicit-one', ['--repeat-each', '1']), ('explicit-zero', ['--repeat-each', '0'])]:
        source = single(once)
        if label == 'omitted':
            check_pin = 'const suitePath=fileURLToPath(import.meta.url);for(const fd of readdirSync("/proc/"+process.ppid+"/fd")){try{assert.notEqual(readlinkSync("/proc/"+process.ppid+"/fd/"+fd),suitePath)}catch(error){if(error.code!=="ENOENT")throw error}}mark("pin-closed",info,{nativePid:process.ppid,openSuiteDescriptors:0});'
            source = 'appendFileSync(new URL(import.meta.url),"\\n// genuine benign authored same-suite edit\\n");' + single(check_pin + once)
        doc, events, case, row = run('count-one-direct-' + label, {'one.spec.mjs': source}, direct=True, options=options, workers=1)
        if args.prepare_only:
            continue
        assert 'directory' not in doc and len(doc['workers']) == len(doc['attempts']) == 1
        expected_phases = ['import', 'pin-closed', 'body'] if label == 'omitted' else ['import', 'body']
        assert [e['phase'] for e in events] == expected_phases and body_events(events)[0]['pid'] == doc['workers'][0]['pid']
        assert doc['summary']['expected'] == 1 and doc['globalErrors'] == []
        if label == 'omitted':
            assert 'repeatConfiguration' not in doc
            assert (case / 'tests/one.spec.mjs').read_text().endswith('\n// genuine benign authored same-suite edit\n')
            assert next(e for e in events if e['phase'] == 'pin-closed')['openSuiteDescriptors'] == 0
        else:
            raw_count = 1 if label == 'explicit-one' else 0
            assert doc['repeatConfiguration'] == {'primaryOverride': raw_count, 'root': None, 'projectDeclarations': [],
                'resolved': [{'project': 0, 'role': 'primary', 'declaredRepeatEach': 1, 'effectiveRepeatEach': raw_count, 'executions': 1}]}

    configured = 'export default defineNativeSuite({repeatEach:0,projects:[{name:"configured",repeatEach:1,tests:[{title:"same actual title",run:(_,info)=>{assert.equal(info.repeatEachIndex,0);assert.equal(info.testId,"1");mark("body",info);}}]}]});'
    doc, events, _, _ = run('configured-root-zero-project-one-direct', {'one.spec.mjs': configured}, direct=True, workers=1)
    if not args.prepare_only:
        assert 'directory' not in doc and len(doc['workers']) == len(doc['attempts']) == 1
        assert [e['phase'] for e in events] == ['import', 'body'] and body_events(events)[0]['pid'] == doc['workers'][0]['pid']
        assert doc['summary']['expected'] == 1 and doc['globalErrors'] == []
        assert doc['repeatConfiguration'] == {'primaryOverride': None, 'root': 0, 'projectDeclarations': [{'project': 1, 'repeatEach': 1}],
            'resolved': [{'project': 1, 'role': 'primary', 'declaredRepeatEach': 1, 'effectiveRepeatEach': 1, 'executions': 1}]}

    selected_setup = 'export default defineNativeSuite({projects:[{name:"setup",repeatEach:1,tests:[{title:"selected setup",run:(_,info)=>{assert.equal(info.repeatEachIndex,0);assert.equal(info.testId,"1");mark("body",info);}}]},{name:"body",dependencies:["setup"],tests:[{title:"unselected dependent body",run:()=>{throw Error("Unselected dependent body was admitted");}}]}]});'
    doc, events, _, _ = run('directory-primary-cli-zero-keeps-outer-role', {'one.spec.mjs': selected_setup}, options=['--project', 'setup', '--repeat-each', '0'], workers=1, require_repeat=False)
    if not args.prepare_only:
        bodies = body_events(events)
        assert len(bodies) == 1 and bodies[0]['project'] == 'setup' and bodies[0]['repeatEachIndex'] == bodies[0]['retry'] == 0
        assert 'repeatSelection' not in doc['directory'] and len(doc['sources']) == 1
        assert next(p for p in doc['directory']['projects'] if p['name'] == 'setup')['role'] == 'primary'
        assert doc['summary']['tests'] == doc['summary']['attempts'] == doc['summary']['expected'] == 1
        source = doc['sources'][0]['report']
        assert source['globalErrors'] == [] and len(source['attempts']) == 1 and source['attempts'][0]['pid'] == bodies[0]['pid']
        assert source['repeatConfiguration'] == {'primaryOverride': 0, 'root': None, 'projectDeclarations': [{'project': 1, 'repeatEach': 1}],
            'resolved': [{'project': 1, 'role': 'primary', 'declaredRepeatEach': 1, 'effectiveRepeatEach': 0, 'executions': 1}]}

    fixture = 'fixtures:{worker:{scope:"worker",automatic:true,setup:async(_,use,info)=>{mark("worker-setup",info);await use(42);mark("worker-teardown",info);}}},'
    retry_source = 'export default defineNativeSuite({repeatEach:2,' + fixture + 'tests:[{title:"same actual title",retries:1,run:async(_,info)=>{mark("body",info,{snapshot:info.snapshotPath("same.txt"),output:info.outputDir});writeFileSync(info.outputPath("actual.txt"),"repeat "+info.repeatEachIndex+" retry "+info.retry);if(info.repeatEachIndex===0&&info.retry===0)throw Error("actual repeat-zero first failure");}}]});'
    doc, events, case, row = run('real-retry-worker-output-snapshot-retention', {'one.spec.mjs': retry_source}, direct=True, options=['--preserve-output', 'failures-only'])
    if not args.prepare_only:
        bodies = body_events(events)
        assert sorted((e['repeatEachIndex'], e['retry']) for e in bodies) == [(0, 0), (0, 1), (1, 0)]
        first = next(e for e in bodies if (e['repeatEachIndex'], e['retry']) == (0, 0))
        retry = next(e for e in bodies if (e['repeatEachIndex'], e['retry']) == (0, 1))
        other = next(e for e in bodies if (e['repeatEachIndex'], e['retry']) == (1, 0))
        assert bodies.index(first) < bodies.index(retry)
        assert first['testId'] == retry['testId'] != other['testId']
        assert len({e['pid'] for e in bodies}) == len({e['output'] for e in bodies}) == 3
        assert len({e['snapshot'] for e in bodies}) == 1
        assert {e['pid'] for e in events if e['phase'] == 'worker-setup'} == {e['pid'] for e in bodies} == {e['pid'] for e in events if e['phase'] == 'worker-teardown'}
        assert doc['summary']['flaky'] == 1 and doc['summary']['expected'] == 2
        retention = [r for source in reports(doc) for r in source['outputRetention']]
        assert sorted(r['status'] for r in retention) == ['kept', 'removed', 'removed']
        kept = next(r for r in retention if r['status'] == 'kept')
        assert Path(kept['originalPath'], 'actual.txt').read_text() == 'repeat 0 retry 0'
        assert all(not Path(r['originalPath']).exists() for r in retention if r['status'] == 'removed')

    projects = 'export default defineNativeSuite({repeatEach:3,projects:[{name:"setup",repeatEach:1,teardown:"cleanup",tests:[{title:"setup",run:(_,info)=>mark("body",info)}]},{name:"body",dependencies:["setup"],tests:[{title:"body",run:(_,info)=>mark("body",info)}]},{name:"cleanup",repeatEach:1,tests:[{title:"cleanup",run:(_,info)=>mark("body",info)}]}]});'
    doc, events, _, _ = run('primary-cli-override-support-own-count', {'one.spec.mjs': projects}, options=['--project', 'body', '--repeat-each', '2'])
    if not args.prepare_only:
        bodies = body_events(events)
        assert sorted((e['project'], e['repeatEachIndex']) for e in bodies) == sorted([('setup', 0), ('body', 0), ('body', 1), ('cleanup', 0)])
        assert max(i for i, e in enumerate(bodies) if e['project'] == 'setup') < min(i for i, e in enumerate(bodies) if e['project'] == 'body')
        assert max(i for i, e in enumerate(bodies) if e['project'] == 'body') < min(i for i, e in enumerate(bodies) if e['project'] == 'cleanup')
        units = doc['directory']['repeatSelection']['units']
        assert sorted((u['declaredRepeatEach'], u['effectiveRepeatEach'], u['executions']) for u in units) == [(1, 1, 1), (1, 1, 1), (3, 2, 2)]

    for parallel in (False, True):
        for shard in (1, 2):
            source = 'export default defineNativeSuite({repeatEach:3,fullyParallel:' + str(parallel).lower() + ',tests:[{title:"A",run:(_,info)=>mark("body",info,{label:"A"})},{title:"B",run:(_,info)=>mark("body",info,{label:"B"})}]});'
            doc, events, _, _ = run(('parallel' if parallel else 'grouped') + '-shard-' + str(shard), {'one.spec.mjs': source}, options=['--shard', str(shard) + '/2'])
            if not args.prepare_only:
                expected = ([('A', 0), ('B', 0), ('A', 1)] if shard == 1 else [('B', 1), ('A', 2), ('B', 2)]) if parallel else ([('A', 0), ('B', 0), ('A', 1), ('B', 1)] if shard == 1 else [('A', 2), ('B', 2)])
                observed = [(e['label'], e['repeatEachIndex']) for e in body_events(events)]
                assert sorted(observed) == sorted(expected), (observed, expected)
                assert doc['summary']['expected'] == len(expected)

    barrier = 'mark("body",info);writeFileSync(control+"/entered-"+process.pid,"");await waitFor(()=>readdirSync(control).filter(f=>f.startsWith("entered-")).length>=2);'
    doc, events, _, row = run('shared-workers-two-across-repeated-files', {n + '.spec.mjs': single(barrier, repeatEach=2) for n in ('a', 'b')})
    if not args.prepare_only:
        assert len(body_events(events)) == 4 and row['peakObservedHosts'] == 2
        assert doc['directory']['activeFileLimit'] == 2

    doc, events, _, _ = run('original-global-deadline-not-per-repeat', {'one.spec.mjs': single('mark("body",info);await info.setTimeout(0);await aborted(info);mark("aborted",info);', repeatEach=3)}, direct=True, options=['--global-timeout', '2000'], code=1, workers=1)
    if not args.prepare_only:
        assert [(e['repeatEachIndex'], e['retry']) for e in body_events(events)] == [(0, 0)]
        assert any(e['phase'] == 'aborted' for e in events)
        assert len(attempts(doc)) == 1 and attempts(doc)[0]['status'] == 'interrupted'
        assert any('original global timeout of 2000ms exceeded' in error for source in reports(doc) for error in source['globalErrors'])
        assert all(row['sourceId'] is None and row['outcome'] == 'didNotRun' and row['nativeSelectionOnly'] for row in doc['tests'] if row['repeat']['repeatEachIndex'] > 0)

    fail = single('mark("body",info);assert.equal(info.repeatEachIndex,0);await waitFor(()=>existsSync(control+"/held"));throw Error("actual first repeat failure");', repeatEach=2)
    held = single('mark("body",info);assert.equal(info.repeatEachIndex,0);writeFileSync(control+"/held","");await aborted(info);mark("aborted",info);', repeatEach=2)
    doc, events, _, _ = run('global-max-failures-stops-later-repeats', {'a.spec.mjs': fail, 'b.spec.mjs': held}, options=['--max-failures', '1'], code=1)
    if not args.prepare_only:
        assert len(body_events(events)) == 2 and all(e['repeatEachIndex'] == 0 for e in body_events(events))
        assert any(e['phase'] == 'aborted' for e in events)
        assert sorted(t['status'] for t in attempts(doc)) == ['failed', 'interrupted']
        assert any('actual first repeat failure' in t['error'] for t in attempts(doc))
    save()
    print(json.dumps({'preparedOnly': args.prepare_only, 'groups': len(groups), 'index': str(root / 'index.json')}))
except BaseException:
    save()
    print('Preserved actual first negative:', root / 'index.json', flush=True)
    raise
