"""Render standalone figures from completed, saved observations."""
import collections
import json
import pathlib
import statistics
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

folder = pathlib.Path(sys.argv[1])
run = json.loads((folder / 'result.json').read_text())
findings = json.loads((folder / 'findings.json').read_text())
node = [json.loads(line) for line in (folder / 'node-memory.jsonl').read_text().splitlines()]
profiles = [json.loads(line) for line in (folder / 'processes.jsonl').read_text().splitlines()]
start = run['runStartMonotonicSeconds']
profiles = [row for row in profiles if start <= row['monotonic'] <= start + run['activeMs'] / 1000]
fig, axes = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
minutes = [row['atMs'] / 60000 for row in node]
axes[0, 0].plot(minutes, [row['arrayBuffers'] / 1048576 for row in node], color='#116d9c')
axes[0, 0].set(title='Node ArrayBuffers', ylabel='MiB')
for name in ['rss', 'heapUsed']:
    axes[0, 1].plot(minutes, [row[name] / 1048576 for row in node], label=name)
axes[0, 1].set(title='Node process observations', ylabel='MiB')
axes[0, 1].legend()
for role in ['suitecut-browser', 'cef-renderer', 'cef-gpu']:
    grouped = collections.defaultdict(list)
    for row in profiles:
        grouped[int((row['monotonic'] - start) / 60)].append(
            sum(p['rssBytes'] for p in row['processes'] if p['role'] == role) / 1048576)
    times = sorted(grouped)
    axes[1, 0].plot(times, [statistics.median(grouped[t]) for t in times], label=role)
axes[1, 0].set(title='Native role RSS (minute medians; shared pages counted)', ylabel='MiB')
axes[1, 0].legend()
for name, row in findings['outputs'].items():
    axes[1, 1].plot([5 / 60, 15, (run['requestedMs'] / 1000 - 20) / 60],
                    row['syncP95Ms'], marker='o', label=name)
axes[1, 1].axhline(200, linestyle='--', color='#b33f3f', label='Failure threshold')
axes[1, 1].set(title='Decoded A/V synchronization', ylabel='p95 absolute offset (ms)')
axes[1, 1].legend()
for ax in axes.flat:
    ax.set_xlabel('Active minutes')
    ax.grid(alpha=.2)
fig.suptitle('SuiteCut C++ engine: 30-minute simultaneous recording and streaming')
fig.savefig(folder / 'resources-and-sync.png', dpi=160)
fig.savefig(folder / 'resources-and-sync.svg')
plt.close(fig)

root = pathlib.Path(__file__).resolve().parents[2]
obs = json.loads((root / 'benchmarks/obs-cef/2026-10-09-obs-long-control/media-validation.json').read_text())
initial = json.loads((folder.parent / 'soak/media-validation.json').read_text())
new = json.loads((folder / 'media-validation.json').read_text())
# The stored OBS validation groups results by pipeline, then output.
obs_outputs = obs['obs']
series = [('OBS external Chromium', obs_outputs), ('Initial C++ run (competing work)', initial['outputs']),
          ('Corrected C++ run', new['outputs'])]
fig, axes = plt.subplots(1, 3, figsize=(14, 5), constrained_layout=True)
labels = [name for name, _ in series]
for index, (metric, title) in enumerate([
    ('distinctFps', 'Distinct visual frames/s'), ('hold', 'Longest content hold (s)'),
    ('silence', 'Longest analyzed silence (s)'),
]):
    for out_index, name in enumerate(['recording', 'receiver']):
        values = []
        for _, outputs in series:
            row = outputs[name]
            values.append(row['video']['distinctActiveFps'] if metric == 'distinctFps'
                          else row['video']['longestContentHold']['durationSeconds'] if metric == 'hold'
                          else row['audio']['longestSilentInterval']['durationSeconds'])
        positions = [i + (out_index - .5) * .34 for i in range(len(series))]
        axes[index].bar(positions, values, width=.32, label=name)
    axes[index].set_title(title)
    axes[index].set_xticks(range(len(labels)), labels, rotation=18, ha='right')
    axes[index].grid(axis='y', alpha=.2)
    axes[index].legend()
fig.suptitle('Saved browser-flow controls: capture paths, codecs and run times differ')
fig.savefig(folder / 'continuity-comparison.png', dpi=160)
fig.savefig(folder / 'continuity-comparison.svg')
plt.close(fig)
