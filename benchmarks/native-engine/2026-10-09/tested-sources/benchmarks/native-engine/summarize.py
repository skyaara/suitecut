"""Summarize saved native runs without decoding media or changing raw evidence."""
import collections
import json
import pathlib
import statistics
import sys

folder = pathlib.Path(sys.argv[1])
run = json.loads((folder / 'result.json').read_text())
validation = json.loads((folder / 'media-validation.json').read_text())
node = [json.loads(line) for line in (folder / 'node-memory.jsonl').read_text().splitlines()]
start = run['runStartMonotonicSeconds']
end = start + run['activeMs'] / 1000
profiles = [json.loads(line) for line in (folder / 'processes.jsonl').read_text().splitlines()]
profiles = [row for row in profiles if start <= row['monotonic'] <= end]
by_pid = collections.defaultdict(list)
for row in profiles:
    for process in row['processes']:
        by_pid[process['pid']].append(process)
observed = profiles[-1]['monotonic'] - profiles[0]['monotonic']
role_cpu = collections.defaultdict(float)
for rows in by_pid.values():
    role_cpu[rows[0]['role']] += max(0, rows[-1]['cpuSeconds'] - rows[0]['cpuSeconds'])

def median(rows):
    return statistics.median(rows) if rows else None

def role_memory(role, lower, upper):
    return median([sum(p['rssBytes'] for p in row['processes'] if p['role'] == role) / 1048576
                   for row in profiles if lower <= row['monotonic'] - start < upper])

def stat(text):
    return {key: int(value) for key, value in (line.split() for line in text.splitlines())}

host_first = stat(profiles[0]['host']['cpu.stat'])
host_last = stat(profiles[-1]['host']['cpu.stat'])
summary = {
    'controllerStatus': run['status'],
    'decodedMediaStatus': validation['status'],
    'activeSeconds': run['activeMs'] / 1000,
    'profileObservedSeconds': observed,
    'profileMaximumSampleMs': max(row['sampleDurationMs'] for row in profiles),
    'hostMeanCpuCores': (host_last['usage_usec'] - host_first['usage_usec']) / 1e6 / observed,
    'hostCpuStatDelta': {key: host_last[key] - value for key, value in host_first.items()},
    'roleCpu': {role: {'seconds': seconds, 'meanCores': seconds / observed}
                for role, seconds in role_cpu.items()},
    'rssMiB': {role: {'minutes3to8Median': role_memory(role, 180, 480),
                     'last5MinutesMedian': role_memory(role, run['activeMs'] / 1000 - 300,
                                                      run['activeMs'] / 1000)}
               for role in role_cpu},
    'rssCaveat': 'Role sums double count shared mappings. RSS is not private allocation or a leak proof.',
    'nodeFinalGc': run['afterCleanupGc'],
    'nodeArrayBufferMiB': {'first5MinutesMedian': median([x['arrayBuffers'] / 1048576 for x in node if x['atMs'] < 300000]),
                          'last5MinutesMedian': median([x['arrayBuffers'] / 1048576 for x in node if x['atMs'] >= run['activeMs'] - 300000])},
    'outputs': {},
}
if 'epochMs' in node[0] and 'monotonicMs' in node[0]:
    summary['wallMinusSteadyElapsedMs'] = ((node[-1]['epochMs'] - node[0]['epochMs'])
                                         - (node[-1]['monotonicMs'] - node[0]['monotonicMs']))
for name, value in validation['outputs'].items():
    summary['outputs'][name] = {
        'status': value['status'],
        'longestSilenceSeconds': value['audio']['longestSilentInterval']['durationSeconds'],
        'distinctFps': value['video']['distinctActiveFps'],
        'longestContentHoldSeconds': value['video']['longestContentHold']['durationSeconds'],
        'maximumPresentationGapSeconds': value['video']['maximumPresentationGapSeconds'],
        'syncP95Ms': [window['p95AbsoluteOffsetMs'] for window in value['syncWindows']],
        'nativeCounters': run['recordStatus' if name == 'recording' else 'streamStatus'],
    }
(folder / 'findings.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
