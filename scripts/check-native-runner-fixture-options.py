#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual native-selected fixture constants; no browser or JavaScript planner."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--sdk-directory', type=Path, required=True)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--node', default='node')
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--case', action='append')
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='fixture-options-', dir=args.directory.resolve()))
sdk = args.sdk_directory.resolve()
node = shutil.which(args.node)
assert node
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = [args.executable.resolve(), Path(node).resolve(), Path(__file__).resolve()] + [p for p in sdk.rglob('*') if p.is_file()]
before = {str(p): sha(p) for p in inputs}
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
prelude = """import assert from 'node:assert/strict';import process from 'node:process';
import {defineNativeSuite} from SUITE;
import {extendNativeFixtures} from LAYERS;
const emit=value=>process.stdout.write(value+'\\n');
""".replace('SUITE', json.dumps((sdk / 'native-suite.js').as_uri())).replace('LAYERS', json.dumps((sdk / 'native-fixture-layers.js').as_uri()))
cases = {
    'literal-data': r"""
export default defineNativeSuite({fixtures:{undefinedValue:{value:undefined},nullValue:{value:null},falseValue:{value:false},zeroValue:{value:0},negativeZero:{value:-0},empty:{value:''},data:{value:{text:'\ud800\0\udfff',nested:[true,17,'π😀']}}},tests:[{title:'distinct native literal data',fixtures:['undefinedValue','nullValue','falseValue','zeroValue','negativeZero','empty','data'],run:f=>{assert.ok(Object.hasOwn(f,'undefinedValue'));assert.equal(f.undefinedValue,undefined);assert.equal(f.nullValue,null);assert.equal(f.falseValue,false);assert.equal(f.zeroValue,0);assert.ok(Object.is(f.negativeZero,-0));assert.equal(f.empty,'');assert.deepEqual(f.data,{text:'\ud800\0\udfff',nested:[true,17,'π😀']});emit('LITERAL_DATA')}}]});
""",
    'configured-callback': r"""
export default defineNativeSuite({fixtures:{option:{option:true,dependencies:['unavailable'],setup:()=>{throw new Error('CONFIGURED_CALLBACK_RAN')}},consumer:{dependencies:['option'],setup:async({option},use,i)=>{assert.equal(i.scope,'test');assert.equal(option,'native-selected');emit('CONSUMER_SETUP');await use(option);emit('CONSUMER_TEARDOWN')}}},fixtureOptions:{option:'native-selected'},tests:[{title:'configured callback and dependencies omitted',fixtures:['consumer'],run:({consumer})=>{assert.equal(consumer,'native-selected');emit('CONFIGURED_BODY')}}]});
""",
    'previous-literal': r"""
const fixtures=extendNativeFixtures({item:{value:'base',scope:'worker',option:true}},{item:{dependencies:['item'],setup:async({item},use,i)=>{assert.equal(i.scope,'worker');emit('OVERRIDE_SETUP');await use(item+'-callback');emit('OVERRIDE_TEARDOWN')}}});
export default defineNativeSuite({fixtures,tests:[1,2].map(n=>({title:'previous literal '+n,fixtures:['item'],run:({item})=>{assert.equal(item,'base-callback');emit('PREVIOUS_'+n)}}))});
""",
    'configured-layer': r"""
const fixtures=extendNativeFixtures({item:{value:'base',scope:'worker',option:true}},{item:{dependencies:['item'],setup:()=>{throw new Error('SELECTED_OVERRIDE_CALLBACK_RAN')}}});
export default defineNativeSuite({fixtures,fixtureOptions:{item:undefined},tests:[{title:'effective inherited option explicit undefined',fixtures:['item'],run:({item})=>{assert.equal(item,undefined);emit('CONFIGURED_UNDEFINED')}}]});
""",
    'automatic-hook': r"""
export default defineNativeSuite({fixtures:{option:{value:1,option:true},automatic:{automatic:true,dependencies:['option'],setup:async({option},use)=>{assert.equal(option,2);emit('AUTO_SETUP');await use('active');emit('AUTO_TEARDOWN')}}},fixtureOptions:{option:2},hooks:{beforeEach:[{fixtures:['option'],run:({option})=>{assert.equal(option,2);emit('BEFORE')}}],afterEach:[{fixtures:['option'],run:({option})=>{assert.equal(option,2);emit('AFTER')}}]},tests:[{title:'shared genuine test scope',fixtures:['option'],run:({option})=>{assert.equal(option,2);emit('HOOK_BODY')}}]});
""",
    'retry': r"""
export default defineNativeSuite({fixtures:{item:{value:{selected:3},option:true}},fixtureOptions:{item:{selected:7}},tests:[{title:'native configured retry',retries:1,fixtures:['item'],run:({item},i)=>{assert.deepEqual(item,{selected:7});emit('RETRY_'+i.retry);assert.equal(i.retry,1)}}]});
""",
    'non-option-rejected': r"""
export default defineNativeSuite({fixtures:{item:{value:1}},fixtureOptions:{item:2},tests:[{title:'nonoption configuration',fixtures:['item'],run:()=>emit('INVALID_BODY')}]});
""",
    'unknown-option-rejected': r"""
export default defineNativeSuite({fixtures:{item:{value:1,option:true}},fixtureOptions:{unknown:2},tests:[{title:'unknown configuration',run:()=>emit('INVALID_BODY')}]});
""",
    'option-false-rejected': r"""
const fixtures=extendNativeFixtures({item:{value:1,option:true}},{item:{value:2,option:false}});
export default defineNativeSuite({fixtures,tests:[{title:'native inherited option flag',fixtures:['item'],run:()=>emit('INVALID_BODY')}]});
""",
}
if args.case:
    assert len(set(args.case)) == len(args.case) and all(name in cases for name in args.case), 'Unknown/duplicate fixture option scope'
    cases = {name: cases[name] for name in args.case}
results = []
try:
    for name, source in cases.items():
        folder = root / name
        folder.mkdir()
        suite = folder / 'suite.mjs'
        suite.write_text(prelude + source)
        if args.prepare_only:
            syntax = subprocess.run([node, '--check', str(suite)], capture_output=True, timeout=10)
            assert syntax.returncode == 0 and syntax.stderr == b'', (name, syntax.stderr)
            results.append(dict(name=name, prepared=True))
            continue
        command = [str(args.executable.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(folder / 'report.json')]
        (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        actual = subprocess.run(command, capture_output=True, timeout=15)
        (folder / 'stdout.bin').write_bytes(actual.stdout)
        (folder / 'stderr.bin').write_bytes(actual.stderr)
        (folder / 'exit.json').write_text(json.dumps(dict(returncode=actual.returncode)) + '\n')
        report = json.loads((folder / 'report.json').read_bytes())
        workers = report['workers']
        assert actual.stderr == b'' and workers and all(w['exitStatusKnown'] and w['stderr']['byteLength'] == 0 for w in workers), (name, actual.stderr, report)
        assert all(not Path('/proc', str(w['pid'])).exists() for w in workers), (name, workers)
        stdout = ''.join(bytes.fromhex(w['stdout']['data']).decode() for w in workers)
        if name.endswith('-rejected'):
            assert actual.returncode == 1 and report['globalErrors'] and not report['attempts'], (name, report)
            assert all(w['exitCode'] is None and w['signal'] == 9 for w in workers), (name, workers)
            expected = {'non-option-rejected':'only option fixtures','unknown-option-rejected':'Unknown native fixture option','option-false-rejected':'cannot be overridden as false'}[name]
            assert any(expected in error for error in report['globalErrors']) and 'INVALID_BODY' not in stdout, (name, report)
        else:
            assert actual.returncode == 0 and not report['globalErrors'], (name, report)
            assert all(w['exitCode'] == 0 and w['signal'] is None for w in workers), (name, workers)
            assert all(a['expected'] for a in report['attempts']) if name != 'retry' else [a['status'] for a in report['attempts']] == ['failed','passed'], report
            if name == 'configured-callback':
                assert stdout.index('CONSUMER_SETUP') < stdout.index('CONFIGURED_BODY') < stdout.index('CONSUMER_TEARDOWN'), stdout
            if name == 'previous-literal':
                assert stdout.count('OVERRIDE_SETUP') == stdout.count('OVERRIDE_TEARDOWN') == 1 and len(workers) == 1, stdout
                assert stdout.index('OVERRIDE_SETUP') < stdout.index('PREVIOUS_1') < stdout.index('PREVIOUS_2') < stdout.index('OVERRIDE_TEARDOWN'), stdout
            if name == 'automatic-hook':
                assert [stdout.index(event) for event in ['AUTO_SETUP','BEFORE','HOOK_BODY','AFTER','AUTO_TEARDOWN']] == sorted(stdout.index(event) for event in ['AUTO_SETUP','BEFORE','HOOK_BODY','AFTER','AUTO_TEARDOWN']), stdout
            if name == 'retry':
                assert len(workers) == 2 and len({w['pid'] for w in workers}) == 2 and 'RETRY_0' in stdout and 'RETRY_1' in stdout, report
        results.append(dict(name=name, returncode=actual.returncode, workers=[w['pid'] for w in workers], attempts=len(report['attempts'])))
finally:
    after = {str(p): sha(p) for p in inputs}
    (root / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (root / 'summary.json').write_text(json.dumps(dict(scopes=results, inputsUnchanged=before == after), indent=2) + '\n')
    assert before == after, 'Fixture option proof inputs changed'
print(f'PASS {len(results)} ' + ('fixture option fixture syntax preparations' if args.prepare_only else 'actual native fixture literal/option process groups'))
