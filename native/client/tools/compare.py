#!/usr/bin/env python3
"""Sequential matched C++/Node SDK verifier; no browser runtime is bundled in C++.

Run under measurement.lock and the repository proof supervisor. /proc sampling is
optional and perturbs timing; default action timings avoid periodic PSS scans.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import statistics
import subprocess
import time


def digest(path):
    result = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def identity(pid):
    try:
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return {'pid': pid, 'start': int(fields[19]), 'state': fields[0]}
    except (OSError, ValueError, IndexError):
        return None


def process_sample(pid):
    item = identity(pid)
    if not item:
        return None
    try:
        for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
            key, _, value = line.partition(':')
            if key in ('Pss', 'Rss'):
                item[key.lower() + 'KiB'] = int(value.split()[0])
    except (OSError, ValueError) as error:
        item['memoryReadError'] = str(error)
    try:
        item['fds'] = len(list(Path(f'/proc/{pid}/fd').iterdir()))
    except OSError as error:
        item['fdReadError'] = str(error)
    return item if (identity(pid) or {}).get('start') == item['start'] else None


def tree_sample(pid):
    descendants, pending, seen = [], [pid], set()
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        try:
            for task in Path(f'/proc/{current}/task').iterdir():
                try:
                    pending.extend(int(value) for value in (task / 'children').read_text().split())
                except OSError:
                    pass
        except OSError:
            pass
        value = process_sample(current)
        if value:
            descendants.append(value)
    return {'client': next((v for v in descendants if v['pid'] == pid), None),
            'browserProcesses': [v for v in descendants if v['pid'] != pid],
            'browserPssKiB': (sum(v['pssKiB'] for v in descendants if v['pid'] != pid)
                              if all('pssKiB' in v for v in descendants if v['pid'] != pid) else None),
            'browserMemoryUnavailableCount': sum('pssKiB' not in v for v in descendants if v['pid'] != pid)}


def run(command, output, timeout, interval):
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    samples, events, first_ready, identities = [], [], None, {}
    with (output / 'stderr.log').open('wb') as stderr, (output / 'stdout.jsonl').open('w') as stdout:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=stderr, bufsize=0)
        initial = identity(child.pid)
        selector = selectors.DefaultSelector()
        selector.register(child.stdout, selectors.EVENT_READ)
        pending, next_sample = b'', started
        timed_out = False
        while selector.get_map() or child.poll() is None:
            now = time.monotonic()
            if now - started > timeout and child.poll() is None:
                timed_out = True
                child.terminate()
                try:
                    child.wait(5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            if interval > 0 and now >= next_sample and len(samples) < 1200:
                sample = tree_sample(child.pid)
                sample['elapsedMs'] = (now - started) * 1000
                samples.append(sample)
                for process in [sample['client'], *sample['browserProcesses']]:
                    if process:
                        identities[(process['pid'], process['start'])] = process
                next_sample = now + interval
            for key, _ in selector.select(.05):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                pending += chunk
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    decoded = line.decode('utf8', errors='replace')
                    stdout.write(decoded + '\n')
                    stdout.flush()
                    try:
                        event = json.loads(decoded)
                    except ValueError:
                        continue
                    events.append(event)
                    if event.get('event') == 'ready' and first_ready is None:
                        first_ready = (time.monotonic() - started) * 1000
                    print(json.dumps({'run': output.name, **event}), flush=True)
        exit_code = child.wait()
    receipt = {'command': command, 'pidIdentity': initial, 'exitCode': exit_code, 'timedOut': timed_out,
               'execToReadyMs': first_ready, 'execToExitMs': (time.monotonic() - started) * 1000,
               'events': events, 'profileSamples': samples,
               'observedPIDIdentities': list(identities.values()),
               'survivingObservedProcesses': [v for v in identities.values() if (identity(v['pid']) or {}).get('start') == v['start'] and (identity(v['pid']) or {}).get('state') != 'Z'],
               'adoptedZombieProcessesForOuterSupervisor': [v for v in identities.values() if (identity(v['pid']) or {}).get('start') == v['start'] and (identity(v['pid']) or {}).get('state') == 'Z']}
    result = output / 'results.json'
    receipt['result'] = json.loads(result.read_text()) if result.exists() else None
    receipt['passed'] = exit_code == 0 and bool(receipt['result'] and receipt['result']['passed'])
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def summarize(receipts):
    summary = {}
    for client in ('native-cpp', 'node-sdk'):
        relevant = [r for r in receipts if r['result'] and r['result']['client'] == client]
        good = [r for r in relevant if r['passed']]
        values = {'attempts': len(relevant), 'passed': len(good)}
        if good:
            for field in ('launchToReadyMs', 'closeMs', 'totalMs', 'clientCPUMs'):
                values[field] = statistics.median(r['result'][field] for r in good)
            values['execToReadyMs'] = statistics.median(r['execToReadyMs'] for r in good)
            values['operations'] = {}
            for operation in good[0]['result']['timings']:
                data = [v for r in good for v in r['result']['timings'][operation]['samplesMs']]
                values['operations'][operation] = {'count': len(data), 'medianMs': statistics.median(data),
                    'p95Ms': sorted(data)[int((len(data) - 1) * .95)]}
            values['clientWarmPssKiB'] = statistics.median(next(s['pssKiB'] for s in r['result']['samples'] if s['label'] == 'warm') for r in good)
            values['clientFinalPssKiB'] = statistics.median(r['result']['samples'][-1]['pssKiB'] for r in good)
            values['clientPeakRssKiB'] = max(s['peakRssKiB'] for r in good for s in r['result']['samples'])
            values['endurance'] = []
            for r in good:
                late = [s for s in r['result']['samples'] if s['label'].startswith('cycle-')]
                if late:
                    first, last = late[len(late) // 2], late[-1]
                    count = int(last['label'][6:]) - int(first['label'][6:])
                    values['endurance'].append({'latePssDeltaKiB': last['pssKiB'] - first['pssKiB'],
                        'lateFdDelta': last['fds'] - first['fds'], 'cyclesCompared': count,
                        'latePssKiBPerCycle': (last['pssKiB'] - first['pssKiB']) / count if count else None})
            sampled = [s for r in good for s in r['profileSamples']]
            if sampled:
                complete = [s['browserPssKiB'] for s in sampled if s['browserPssKiB'] is not None and s['browserProcesses']]
                values['sampledPeakBrowserPssKiB'] = max(complete) if complete else None
                values['samplesWithUnreadableBrowserMemory'] = sum(bool(s['browserMemoryUnavailableCount']) for s in sampled)
        summary[client] = values
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpp', default='.suitecut/native-client/build/native-client-real-browser')
    parser.add_argument('--node', default='node')
    parser.add_argument('--browser', default='.suitecut/engine-performance-fixes/runtime/suitecut-browser')
    parser.add_argument('--sdk', default='packages/browser-control/dist/browser.js')
    parser.add_argument('--fixture', default='native/client/tools/fixture.html')
    parser.add_argument('--output', default='.suitecut/native-client/comparison')
    parser.add_argument('--iterations', type=int, default=30)
    parser.add_argument('--cycles', type=int, default=50)
    parser.add_argument('--repetitions', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=90)
    parser.add_argument('--sample-interval', type=float, default=0, help='0: no periodic resource profiling; >0: seconds, perturbs action latency')
    args = parser.parse_args()
    assert 1 <= args.repetitions <= 8 and args.sample_interval >= 0
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = [Path(v).resolve() for v in (args.cpp, args.browser, args.sdk, args.fixture, __file__, 'scripts/native-client-node-baseline.mjs', 'native/client/tests/integration/real_browser.cpp')]
    # Hash all emitted SDK modules, not just the entrypoint.
    paths.extend(Path(args.sdk).resolve().parent.rglob('*.js'))
    cef_library = Path(args.browser).resolve().parent / 'libcef.so'
    if cef_library.exists():
        paths.append(cef_library.resolve())
    paths.extend(Path('native/client/include').rglob('*.hpp'))
    paths.extend(Path('native/client/src').rglob('*.cpp'))
    inputs = {str(p): digest(p) for p in dict.fromkeys(paths)}
    receipts = []
    for repetition in range(args.repetitions):
        order = ('cpp', 'node') if repetition % 2 == 0 else ('node', 'cpp')
        for client in order:
            directory = output / f'rep-{repetition}-{client}'
            common = ['--browser', str(Path(args.browser).resolve()), '--fixture', str(Path(args.fixture).resolve()),
                      '--iterations', str(args.iterations), '--cycles', str(args.cycles), '--output', str(directory / 'results.json')]
            command = ([str(Path(args.cpp).resolve())] if client == 'cpp' else
                       [args.node, '--expose-gc', str(Path('scripts/native-client-node-baseline.mjs').resolve()), '--sdk', str(Path(args.sdk).resolve())]) + common
            receipts.append(run(command, directory, args.timeout, args.sample_interval))
    changed = [p for p, before in inputs.items() if digest(p) != before]
    report = {'scope': 'Deterministic matched public C++ and current Node clients driving the same CEF binary. No AI task planning or video. Client CPU excludes browser CPU.',
        'timingConditions': 'Periodic process-tree PSS profiling enabled; timings include profiler perturbation.' if args.sample_interval else 'No periodic process-tree sampler; client memory sampled between workload phases.',
        'config': vars(args), 'inputsSha256': inputs, 'inputsChanged': changed,
        'passed': all(r['passed'] for r in receipts) and not changed, 'summary': summarize(receipts),
        'receiptPaths': [str(output / f'rep-{repetition}-{client}' / 'receipt.json') for repetition in range(args.repetitions) for client in ('cpp', 'node')]}
    (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'event': 'comparison-complete', 'passed': report['passed'], 'summary': report['summary']}), flush=True)
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
