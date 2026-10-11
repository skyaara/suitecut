"""Read a compact live snapshot without decoding media or walking private memory."""
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
result = json.loads((root / 'result.json').read_text())
progress_path = root / 'progress.json'
progress = json.loads(progress_path.read_text()) if progress_path.exists() else {}
out = {'status': result['status'], 'requestedMinutes': result['requestedMs'] / 60000,
       'latestStatusMinutes': progress.get('atMs', 0) / 60000,
       'scrolls': progress.get('scrolls', 0)}
for name, final_name in [('record', 'recordStatus'), ('stream', 'streamStatus')]:
    status = result.get(final_name, progress.get(name))
    if status:
        out[name] = {'uncoveredInputMs': status['missingAudioSamples'] / 48,
                     'lateSamples': status['lateAudioSamples'],
                     'overflowSamples': status['droppedAudioSamples'],
                     'bufferMs': status.get('bufferedDelayMs'),
                     'maximumEncodeMs': status['maximumEncodeMs']}
profile = root / 'processes.jsonl'
if profile.exists() and profile.stat().st_size:
    with profile.open('rb') as stream:
        first = json.loads(stream.readline())
        stream.seek(max(0, profile.stat().st_size - 65536))
        lines = stream.read().splitlines()
    last = next(json.loads(line) for line in reversed(lines) if line.endswith(b'}'))
    def cpu_stat(sample):
        return {key: int(value) for key, value in
                (line.split() for line in sample['host']['cpu.stat'].splitlines())}
    baseline, current = cpu_stat(first), cpu_stat(last)
    out['throttlingEvents'] = current['nr_throttled'] - baseline['nr_throttled']
    out['throttledCPUSeconds'] = (current['throttled_usec'] - baseline['throttled_usec']) / 1e6
    out['latestRSSMiB'] = sum(p['rssBytes'] for p in last['processes']
                              if p['role'] not in ['node', 'receiver', 'sampler']) / 1048576
    out['elapsedMinutes'] = (last['monotonic'] - result.get('runStartMonotonicSeconds',
                                                        first['monotonic'])) / 60
    out['samplingCostMs'] = last['sampleDurationMs']
print(json.dumps(out, indent=2))
