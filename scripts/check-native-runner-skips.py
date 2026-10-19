# SPDX-License-Identifier: MIT
"""Independent decoders check declared skips and actual callback admission."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

p = argparse.ArgumentParser()
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--host-script', type=Path, required=True)
p.add_argument('--suite-api', type=Path, required=True)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--node', default=shutil.which('node'))
args = p.parse_args()
root = args.directory.resolve()
root.mkdir(parents=True, exist_ok=True)
results = []


class Html(HTMLParser):
    def __init__(self):
        super().__init__()
        self.skipped = []
        self.sections = 0
        self.scripts = 0

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        if tag == 'details' and 'declared-skip' in data.get('class', '').split():
            self.skipped.append(data['data-test-id'])
        if tag == 'section' and data.get('class') == 'attempt':
            self.sections += 1
        if tag == 'script':
            self.scripts += 1


def execute(name, extra, skipped, bodies, before_failure=False, retry=False):
    folder = root / name
    folder.mkdir(exist_ok=True)
    events = folder / 'callbacks.jsonl'
    events.unlink(missing_ok=True)
    suite = folder / 'suite.mjs'
    title = 'Skipped <script>bad()</script>\ud800'
    reason = 'Actual declared reason <>&\x00\udc00'
    declarations = [
        {'title':'boolean skip', 'skip':True},
        {'title':'empty reason still skips', 'skip':''},
        {'title':title, 'skip':reason},
        {'title':'false runs', 'skip':False},
        {'title':'absent runs'},
    ]
    if retry:
        declarations[3]['retries'] = 1
    suite.write_text('import fs from "node:fs";import process from "node:process";'
        'import {defineNativeSuite} from ' + json.dumps(args.suite_api.resolve().as_uri()) + ';'
        'const mark=(kind,info)=>fs.appendFileSync(' + json.dumps(str(events)) +
        ',JSON.stringify({kind,id:info.testId??null,pid:process.pid})+"\\n");'
        'export default defineNativeSuite({fixtures:{value:{automatic:true,setup:async({},use,info)=>{'
        'mark("setup",info);await use(42);mark("teardown",info)}}},hooks:{'
        'beforeAll:[{run:async(_,info)=>{mark("beforeAll",info);' +
        ('throw Error("Actual beforeAll failure");' if before_failure else '') + '} }],'
        'afterAll:[{run:async(_,info)=>mark("afterAll",info)}]},tests:' +
        json.dumps(declarations) + '.map(test=>({...test,run:async(_,info)=>{mark("body",info);'
        'if(test.skip===true||typeof test.skip==="string")throw Error("Skipped callback admitted");' +
        ('if(info.testId==="4"&&info.retry===0)throw Error("Actual retry failure");' if retry else '') + '}}))});')
    output = folder / 'result.json'
    command = [str(args.executable), '--node', args.node, '--host-script', str(args.host_script),
        '--suite', str(suite), '--report', str(output), '--junit', str(folder/'result.xml'),
        '--html', str(folder/'result.html')] + extra
    actual = subprocess.run(command, capture_output=True, timeout=15)
    (folder/'stdout').write_bytes(actual.stdout)
    (folder/'stderr').write_bytes(actual.stderr)
    assert actual.returncode == (1 if before_failure else 0), (name,actual.returncode,actual.stderr)
    assert actual.stderr == b'', (name,actual.stderr)
    data = json.loads(output.read_text())
    assert [test['testId'] for test in data['skipped']] == skipped, data
    assert data['summary']['skipped'] == len(skipped), data
    assert data['summary']['didNotRun'] == (1 if before_failure else 0), data
    assert [test['testId'] for test in data['unrun']] == (['5'] if before_failure else [])
    for test in data['skipped']:
        assert set(test) == {'testId','title','reason'}, test
        if test['testId'] == '3':
            assert test['title'] == title and test['reason'] == reason
    attempts = [a['testId'] for a in data['attempts']]
    assert attempts == (['4'] if before_failure else bodies), data
    if retry:
        assert data['attempts'][0]['status'] == 'failed' and data['attempts'][1]['status'] == 'passed'
        assert data['attempts'][0]['pid'] != data['attempts'][1]['pid']
    observed = [json.loads(line) for line in events.read_text().splitlines()] if events.exists() else []
    assert [event['id'] for event in observed if event['kind']=='body'] == bodies
    if not bodies and not before_failure:
        assert observed == [], observed
    assert not any(event['id'] in skipped for event in observed), observed
    xml = ET.parse(folder/'result.xml').getroot()
    declared = [case for case in xml.findall('.//testcase') if case.find('skipped[@type="declared"]') is not None]
    assert len(declared) == len(skipped)
    for case in declared:
        assert 'time' not in case.attrib
        properties = {item.attrib['name']:item.attrib['value'] for item in case.findall('properties/property')}
        assert properties['native.declaredSkip'] == 'true' and properties['native.noAttempt'] == 'true'
        assert 'native.pid' not in properties and 'native.workerIndex' not in properties
    html = Html()
    html.feed((folder/'result.html').read_text())
    assert html.skipped == skipped and html.sections == len(attempts) and html.scripts == 0
    assert data['summary']['tests'] == len(skipped)+len(set(attempts))+len(data['unrun'])
    for worker in data['workers']:
        assert worker['exitStatusKnown'] is True and worker['exitCode'] == 0
        try:
            os.kill(worker['pid'],0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError(('Actual worker alive after report',worker))
    results.append({'name':name,'passed':True,'report':str(output)})


execute('retry-does-not-duplicate-skips', [], ['1','2','3'], ['4','4','5'], retry=True)
execute('mixed-original-identities', [], ['1','2','3'], ['4','5'])
execute('all-skipped-shard-no-callbacks', ['--shard','1/3'], ['1','2'], [])
execute('exact-skip-lossless-invalid-utf16', ['--test-id','3'], ['3'], [])
execute('filter-before-shard', ['--test-id','4','--test-id','3','--shard','1/2'], ['3'], [])
execute('skips-distinct-from-beforeAll-unrun', [], ['1','2','3'], [], before_failure=True)
(root/'report.json').write_text(json.dumps({'passed':len(results),'groups':results},indent=2)+'\n')
print(json.dumps({'passed':len(results),'directory':str(root)}))
