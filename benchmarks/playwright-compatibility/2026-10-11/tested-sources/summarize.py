"""Preserve every compatibility case, failure, and skip without changing results."""
import collections
import csv
import hashlib
import json
import pathlib
import re
import sys

directory = pathlib.Path(sys.argv[1]).resolve()
ansi = re.compile(r'\x1b\[[0-9;]*m')


def cases(suite):
    for spec in suite.get('specs', []):
        for test in spec['tests']:
            results = test.get('results', [])
            result = results[-1] if results else {}
            errors = result.get('errors', []) or [result.get('error', {})]
            messages = [ansi.sub('', error.get('message', '')) for error in errors]
            message = '\n'.join(value for value in messages if value)
            status = result.get('status', 'not-run')
            yield {
                'file': spec.get('file', ''), 'line': spec.get('line'),
                'title': spec['title'], 'status': status,
                'durationMs': result.get('duration'), 'error': message,
                'launcherOutsideHarness': 'independent browser launching/connecting' in message,
                'annotations': test.get('annotations', []),
                'attachments': result.get('attachments', []),
            }
    for child in suite.get('suites', []):
        yield from cases(child)


runs = {}
for path in sorted([*directory.glob('*/results.json'), *directory.glob('*/playwright-results.json')]):
    raw = json.loads(path.read_text())
    items = list(cases(raw))
    by_file = {}
    for item in items:
        key = item['file'].split('/tests/')[-1]
        by_file.setdefault(key, collections.Counter())[item['status']] += 1
    runs[path.parent.name] = {
        'resultsSha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'rawStats': raw.get('stats'),
        'counts': dict(collections.Counter(item['status'] for item in items)),
        'perFile': {name: dict(counts) for name, counts in by_file.items()},
        'launcherOutsideHarness': sum(item['launcherOutsideHarness'] for item in items),
        'cases': items,
    }
summary = {'schemaVersion': 1, 'runs': runs}
(directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
with (directory / 'cases.csv').open('w', newline='') as file:
    writer = csv.DictWriter(file, fieldnames=['run', 'file', 'line', 'title', 'status',
                                             'durationMs', 'launcherOutsideHarness', 'error'])
    writer.writeheader()
    for name, run in runs.items():
        for item in run['cases']:
            writer.writerow({'run': name, **{key: item[key] for key in writer.fieldnames if key != 'run'}})
print(json.dumps({name: {'counts': run['counts'], 'launcherOutsideHarness': run['launcherOutsideHarness']}
                  for name, run in runs.items()}, indent=2))
