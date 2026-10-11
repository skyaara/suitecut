"""Summarize the saved native Twitch status and process samples without rerunning capture."""
import json
from pathlib import Path
import statistics
import sys

directory = Path(sys.argv[1]).resolve()
result = json.loads((directory / 'result.json').read_text())
rows = [json.loads(line) for line in (directory / 'processes.jsonl').read_text().splitlines()]
status = [json.loads(line) for line in (directory / 'output-status.jsonl').read_text().splitlines()]
seen = {}
rss = []
cpu = []
for row in rows:
    rss.append(sum(p['rssBytes'] for p in row['processes']))
    cpu.append(sum(p['cpuPercent'] or 0 for p in row['processes']))
    for process in row['processes']:
        old = seen.setdefault(process['pid'], {
            'min': process['cpuSeconds'], 'max': process['cpuSeconds'], 'role': process['role']})
        old['min'] = min(old['min'], process['cpuSeconds'])
        old['max'] = max(old['max'], process['cpuSeconds'])
previous = 0
missing = []
for row in status:
    total = row['output']['missingAudioSamples']
    if total > previous:
        missing.append({'sampledAtMs': row['atMs'], 'newMissingSamples': total - previous,
                        'newMissingMs': (total - previous) / 48})
    previous = total
final = result['final']
metrics = {
    'sampleCount': len(rows),
    'sampledDurationSeconds': rows[-1]['monotonic'] - rows[0]['monotonic'],
    'peakProcessTreeRssMiB': max(rss) / 2**20,
    'medianProcessTreeRssMiB': statistics.median(rss) / 2**20,
    'processTreeCpuPercentMedian': statistics.median(cpu[1:]),
    'processTreeCpuPercentPeak': max(cpu),
    'observedCpuSeconds': sum(v['max'] - v['min'] for v in seen.values()),
    'observedCpuSecondsByRole': {
        role: sum(v['max'] - v['min'] for v in seen.values() if v['role'] == role)
        for role in sorted({v['role'] for v in seen.values()})},
    'missingAudioSeconds': final['missingAudioSamples'] / 48000,
    'repeatedFrameFraction': final['repeatedFrames'] / final['videoFrames'],
    'browserDroppedFrameDelta': result['after']['droppedFrames'] - result['before']['droppedFrames'],
    'browserTotalFrameDelta': result['after']['decodedFrames'] - result['before']['decodedFrames'],
    'peakVideoQueueBytes': max(r['output']['queuedVideoBytes'] for r in status),
    'peakAudioQueueBytes': max(r['output']['queuedAudioBytes'] for r in status),
    'stackProtectorTerminations': (directory / 'browser-stderr.log').read_text().count('stack smashing'),
    'missingAudioCounterIncrements': missing,
    'limits': 'RSS sums double-count shared pages; sampled CPU excludes unobserved short-lived '
              'subprocesses. Missing audio counters are uncovered sample slots, not proof of an '
              'equivalent audible silence interval. Twitch content naturally contains silence.',
}
(directory / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
print(json.dumps({key: value for key, value in metrics.items()
                  if key != 'missingAudioCounterIncrements'}, indent=2))
