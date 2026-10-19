#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Genuine fixture callbacks and native clocks, without browsers or a JS scheduler."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--executable', type=Path, required=True)
p.add_argument('--sdk-directory', type=Path, required=True)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--node', default='node')
p.add_argument('--case', action='append')
p.add_argument('--prepare-only', action='store_true')
a = p.parse_args()
a.directory.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='fixture-clocks-', dir=a.directory.resolve()))
sdk = a.sdk_directory.resolve()
node = shutil.which(a.node)
assert node
paths = [a.executable.resolve(), Path(node).resolve(), Path(__file__).resolve()] + [x for x in sdk.rglob('*') if x.is_file()]
pins = lambda: {str(x): hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
before = pins()
(root / 'inputs-before.json').write_text(json.dumps(before, indent=2) + '\n')
prelude = """import assert from 'node:assert/strict';
import {defineNativeSuite} from SUITE;
import {extendNativeFixtures} from LAYERS;
const emit=s=>process.stdout.write(s+'\\n');
const work=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const aborted=signal=>signal.aborted?Promise.resolve():new Promise(resolve=>signal.addEventListener('abort',resolve,{once:true}));
""".replace('SUITE', json.dumps((sdk / 'native-suite.js').as_uri())).replace('LAYERS', json.dumps((sdk / 'native-fixture-layers.js').as_uri()))
cases = {
 'independent-setup-teardown': """
export default defineNativeSuite({timeout:100,fixtures:{foo:{timeout:1000,setup:async(_,use,i)=>{assert.equal(i.timeout,1000);await work(300);emit('SETUP');await use(17);await work(300);emit('TEARDOWN')}}},tests:[{title:'custom intervals excluded',fixtures:['foo'],run:({foo},i)=>{assert.equal(foo,17);assert.equal(i.timeout,100);emit('BODY')}}]});
""",
 'project-worker-default': """
export default defineNativeSuite({timeout:2000,fixtures:{foo:{scope:'worker',setup:async(_,use,i)=>{assert.equal(i.timeout,1000);assert.equal(i.project.name,'actual project');await work(300);emit('WORKER_SETUP');await use(17);emit('WORKER_TEARDOWN')}}},projects:[{name:'actual project',timeout:1000,tests:[{title:'selected test hundred',timeout:100,fixtures:['foo'],run:({foo},i)=>{assert.equal(foo,17);assert.equal(i.timeout,100);assert.equal(i.project.name,'actual project');emit('BODY')}}]}]});
""",
 'inherited-override': """
const fixtures=extendNativeFixtures({foo:{timeout:1000,setup:()=>{throw Error('UNSELECTED')}}},{foo:{setup:async(_,use,i)=>{assert.equal(i.timeout,1000);await work(300);emit('INHERITED_SETUP');await use(17)}}});
export default defineNativeSuite({timeout:100,fixtures,tests:[{title:'native inherited version slot',fixtures:['foo'],run:({foo})=>{assert.equal(foo,17);emit('BODY')}}]});
""",
 'zero-independent': """
export default defineNativeSuite({timeout:100,fixtures:{foo:{timeout:0,setup:async(_,use,i)=>{assert.equal(i.timeout,0);await work(300);await use(17)}}},tests:[{title:'zero only disables actual fixture slot',fixtures:['foo'],run:()=>emit('BODY')}]});
""",
 'amendment-persists': """
export default defineNativeSuite({timeout:100,fixtures:{foo:{timeout:1000,setup:async(_,use,i)=>{await i.setTimeout(500);assert.equal(i.timeout,500);emit('SETUP_ACK');await work(200);await use(17);assert.equal(i.timeout,500);await work(200);emit('TEARDOWN_ACK')}}},tests:[{title:'actual fixture mutation ACK',fixtures:['foo'],run:()=>emit('BODY')}]});
""",
 'setup-local-expiry': """
export default defineNativeSuite({timeout:2000,fixtures:{foo:{title:'Actual fixture title',timeout:80,setup:async(_,use,i)=>{emit('SETUP_ARMED');await aborted(i.signal);assert.equal(i.signal.aborted,true);emit('SETUP_ABORTED');throw i.signal.reason}}},tests:[{title:'actual setup expiry',fixtures:['foo'],run:()=>emit('INVALID_BODY')}]});
""",
 'teardown-local-expiry': """
export default defineNativeSuite({timeout:2000,fixtures:{foo:{timeout:80,setup:async(_,use,i)=>{await use(17);emit('TEARDOWN_ARMED');await aborted(i.signal);emit('TEARDOWN_ABORTED');throw i.signal.reason}}},tests:[{title:'actual teardown expiry',fixtures:['foo'],run:()=>emit('BODY')}]});
""",
 'ordinary-budget': """
export default defineNativeSuite({timeout:100,fixtures:{foo:{setup:async(_,use,i)=>{assert.equal(i.timeout,100);emit('SETUP_ARMED');await aborted(i.signal);emit('SETUP_ABORTED');throw i.signal.reason}}},tests:[{title:'ordinary test fixture charged',fixtures:['foo'],run:()=>emit('INVALID_BODY')}]});
""",
 'real-hook-phases': """
export default defineNativeSuite({timeout:150,fixtures:{foo:{timeout:1000,setup:async(_,use)=>{await work(250);emit('SETUP');await use(17);await work(250);emit('TEARDOWN')}}},hooks:{beforeEach:[{fixtures:['foo'],run:({foo})=>{assert.equal(foo,17);emit('BEFORE')}}],afterEach:[{fixtures:['foo'],run:()=>emit('AFTER')}]},tests:[{title:'genuine hook composition',fixtures:['foo'],run:()=>emit('BODY')}]});
""",
 'zero-global-cap': """
export default defineNativeSuite({timeout:0,fixtures:{foo:{timeout:0,setup:async(_,use,i)=>{emit('SETUP_ARMED');await aborted(i.signal);emit('SETUP_ABORTED');throw i.signal.reason}}},tests:[{title:'fixture zero cannot disable native suite cap',fixtures:['foo'],run:()=>emit('INVALID_BODY')}]});
""",
}
if a.case:
 assert len(set(a.case)) == len(a.case) and all(x in cases for x in a.case)
 cases = {x: cases[x] for x in a.case}
results = []
try:
 for name, source in cases.items():
  folder = root / name
  folder.mkdir()
  suite = folder / 'suite.mjs'
  suite.write_text(prelude + source)
  command = [node, '--check', str(suite)] if a.prepare_only else [str(a.executable.resolve()), '--node', node, '--host-script', str(sdk / 'native-runner-host.js'), '--suite', str(suite), '--report', str(folder / 'actual.json'), '--global-timeout', '1500' if name == 'zero-global-cap' else '10000']
  (folder / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
  actual = subprocess.run(command, capture_output=True, timeout=20)
  (folder / 'stdout.bin').write_bytes(actual.stdout)
  (folder / 'stderr.bin').write_bytes(actual.stderr)
  (folder / 'exit.json').write_text(json.dumps({'returncode':actual.returncode}) + '\n')
  if a.prepare_only:
   assert actual.returncode == 0 and not actual.stderr, (name, actual.stderr)
   results.append({'name':name,'prepared':True})
   continue
  report = json.loads((folder / 'actual.json').read_bytes())
  assert not actual.stderr and report['workers'], (name, actual.stderr, report)
  for worker in report['workers']:
   assert worker['exitStatusKnown'] and worker['exitCode'] == 0 and worker['signal'] is None and worker['stderr']['byteLength'] == 0, (name, worker)
   assert not Path('/proc', str(worker['pid'])).exists(), (name, worker)
  stdout = ''.join(bytes.fromhex(w['stdout']['data']).decode() for w in report['workers'])
  assert len(report['attempts']) == 1, (name, report)
  attempt = report['attempts'][0]
  assert attempt['wallDurationMs'] >= attempt['durationMs'], (name, attempt)
  if name in ('setup-local-expiry','teardown-local-expiry','ordinary-budget','zero-global-cap'):
   assert actual.returncode == 1 and not attempt['expected'], (name, report)
   assert attempt['status'] == ('interrupted' if name == 'zero-global-cap' else 'timedOut'), (name, report)
   assert 'INVALID_BODY' not in stdout and '_ABORTED' in stdout, (name, stdout)
   if name == 'zero-global-cap':
    assert len(report['globalErrors']) == 1 and 'global timeout' in report['globalErrors'][0], report
   else:
    assert not report['globalErrors'], (name, report)
    assert ('teardown' if name == 'teardown-local-expiry' else 'setup') in attempt['error'], (name, attempt)
    if name == 'setup-local-expiry':
     assert 'Actual fixture title' in attempt['error'] and '> ' in attempt['error'] and 'defineNativeSuite' in attempt['error'], attempt
  else:
   assert actual.returncode == 0 and not report['globalErrors'] and attempt['status'] == 'passed' and attempt['expected'], (name, report)
   assert 'BODY' in stdout and attempt['durationMs'] < (150 if name == 'real-hook-phases' else 100), (name, report)
   assert attempt['wallDurationMs'] >= (500 if name in ('independent-setup-teardown','real-hook-phases') else 250 if name != 'amendment-persists' else 400), (name, attempt)
   if name == 'real-hook-phases':
    events = ['SETUP','BEFORE','BODY','AFTER','TEARDOWN']
    assert [stdout.index(x) for x in events] == sorted(stdout.index(x) for x in events), stdout
  results.append({'name':name,'returncode':actual.returncode,'workers':[w['pid'] for w in report['workers']],'status':attempt['status']})
finally:
 after = pins()
 (root / 'inputs-after.json').write_text(json.dumps(after, indent=2) + '\n')
 (root / 'summary.json').write_text(json.dumps({'scopes':results,'inputsUnchanged':before == after}, indent=2) + '\n')
 assert before == after, 'Fixture clock proof inputs changed'
print(f'PASS {len(results)} ' + ('fixture clock fixture preparations' if a.prepare_only else 'actual native fixture clock scopes'))
