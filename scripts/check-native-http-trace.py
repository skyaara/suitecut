# SPDX-License-Identifier: MIT
"""Independently check exact native HTTP trace ZIP records, CRCs and bytes.

Run after verify-native-http-trace.mjs, passing its output directory as argv[1].
Uses Python's standard ZIP/JSON/SHA readers, with no browser-driver code.
"""
import base64
import hashlib
import json
import pathlib
import sys
import zipfile

directory = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.suitecut/native-http-trace')
report = json.loads((directory / 'report.json').read_text())
assert report['result'] == 'pass' and report['nativeStderrBytes'] == 0
assert report['nativeShutdown'] == 'clean'
archives = {}
for name in ['browser', 'api', 'binary', 'held-first', 'held-second', 'deadline']:
    with zipfile.ZipFile(directory / (name + '.zip')) as archive:
        entries = archive.infolist()
        assert len(entries) <= 16384 and sum(entry.file_size for entry in entries) <= 96 * 1048576
        assert archive.testzip() is None
        assert len({entry.filename for entry in entries}) == len(entries)
        events = [json.loads(line) for line in archive.read('trace.trace').splitlines() if line]
        network = [json.loads(line) for line in archive.read('trace.network').splitlines() if line]
        resources = {entry.filename: archive.read(entry) for entry in entries}
        archives[name] = events, network, resources

browser_events, browser_network, _ = archives['browser']
api_events, api_network, api_resources = archives['api']
assert any(event.get('method') == 'goto' for event in browser_events)
assert not any('/simple.json' in event['snapshot']['request']['url'] for event in browser_network)
assert any('/index.html' in event['snapshot']['request']['url'] for event in browser_network)
actions = [event for event in api_events if event.get('class') == 'APIRequestContext']
assert len(actions) == 1 and actions[0]['method'] == 'fetch'
assert actions[0]['apiName'] == 'POST ' + report['origin'].split('://')[1] + '/simple.json'
assert not any(event.get('method') == 'goto' for event in api_events)
assert len(api_network) == 1
entry = api_network[0]['snapshot']
assert entry['_apiRequestRef'].startswith('request-context@')
assert not entry.get('_frameref')
assert entry['request']['_headersSource'] == 'native-configured'
assert entry['request']['postData']['_encoding'] == 'base64'
assert base64.b64decode(entry['request']['postData']['text']) == b'{"foo":"bar"}'
assert api_resources[entry['request']['postData']['_file']] == b'{"foo":"bar"}'
body = entry['response']['content']['text'].encode()
assert json.loads(body) == {'actualMethod': 'POST', 'actualBody': '{"foo":"bar"}'}
assert api_resources['resources/' + entry['response']['content']['_sha1']] == body
caller = pathlib.Path(report['caller']).read_bytes()
sources = [name for name in api_resources if name.startswith('src/')]
assert len(sources) == 1 and api_resources[sources[0]] == caller
assert pathlib.Path(report['caller']).as_posix() in api_resources['trace.stacks'].decode()
_, binary_network, binary_resources = archives['binary']
entry = binary_network[0]['snapshot']
assert base64.b64decode(entry['response']['content']['text']) == bytes(range(256))
assert binary_resources['resources/' + entry['response']['content']['_sha1']] == bytes(range(256))
first_events, first_network, _ = archives['held-first']
second_events, second_network, _ = archives['held-second']
assert [event['type'] for event in first_events] == ['context-options', 'before']
assert [event['type'] for event in second_events] == ['context-options']
assert not first_network and not second_network
deadline_events, deadline_network, _ = archives['deadline']
assert [event['type'] for event in deadline_events] == ['context-options', 'before', 'after']
assert deadline_events[-1]['error']['message']
assert deadline_network[0]['snapshot']['response']['_failureText']
result = {'result': 'pass', 'browserAPIIndependent': True,
          'actualPOSTBodyAndResponseHash': True, 'exactCallerSourceSHA1': hashlib.sha1(caller).hexdigest(),
          'exactIndependentBinary256': True, 'latePreviousChunkExcluded': True,
          'original80msDeadlineRecorded': True, 'nativeStderrBytes': 0}
(directory / 'archive-decode-report.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result))
